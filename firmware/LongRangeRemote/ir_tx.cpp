// ir_tx.cpp - RMT based IR transmitter (ESP-IDF 5.x "new" RMT driver, as bundled with Arduino-ESP32 3.x).
//
// Long-Range Remote: before the first frame of a key press the LED supply is armed (LED_ARM_PIN high:
// the control board's P-FET connects the LED head to the pack), and it is
// disarmed again as soon as nothing more is queued. The carrier itself goes out on IR_TX_PIN.
#include "ir_tx.h"
#include "ir_db.h"
#include "config.h"
#include "hw.h"
#include "sound.h"

#include <Arduino.h>
#include <string.h>
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace irtx {
namespace {

// ---------------------------------------------------------------------------
// Queue messages
// ---------------------------------------------------------------------------
// Only key presses go through the queue. Keep-alive and release are lock-free shared
// state, so they can never be lost when the queue is busy:
//   every press gets a generation number; holdStop() marks the newest generation released.
enum MsgType : uint8_t { MSG_TAP, MSG_HOLD };
struct Msg {
  MsgType type;
  uint32_t gen;             // press generation
  int code;                 // >= 0: database code, -1: use params
  IrCodeParams params;
  char label[64];
};

QueueHandle_t g_queue = nullptr;
SemaphoreHandle_t g_last_mux = nullptr;
LastSent g_last = {};
volatile bool g_sending = false;
volatile uint32_t g_gen_posted = 0;    // generation of the most recent press
volatile uint32_t g_gen_released = 0;  // presses with gen <= this are released
volatile uint32_t g_keepalive_ms = 0;  // last keep-alive (millis)

rmt_channel_handle_t g_chan = nullptr;
rmt_encoder_handle_t g_copy = nullptr;
uint32_t g_carrier = 0;
uint8_t g_duty = 0;

// Symbol buffer: 2 durations per RMT symbol, 15-bit each (max 32767 us at 1 MHz).
const size_t MAX_SYMBOLS = 1600;
rmt_symbol_word_t g_syms[MAX_SYMBOLS];
IrSignal g_sig;            // encoder output (static: ~3 KB)
bool g_toggle = false;

// ---------------------------------------------------------------------------
// Timing -> RMT symbols
// ---------------------------------------------------------------------------
struct SymWriter {
  size_t n = 0;            // symbols complete
  bool half = false;       // true if first half of g_syms[n] is filled
  bool overflow = false;

  void put(bool level, uint32_t dur) {
    while (dur > 0) {
      uint32_t d = dur > 32767 ? 32767 : dur;
      // don't leave a tiny remainder (a 0-length half would end the transmission early)
      if (dur > 32767 && dur - 32767 < 2) d = dur - 2;
      dur -= d;
      if (n >= MAX_SYMBOLS) { overflow = true; return; }
      if (!half) {
        g_syms[n].level0 = level; g_syms[n].duration0 = d; half = true;
      } else {
        g_syms[n].level1 = level; g_syms[n].duration1 = d; half = false; n++;
      }
    }
  }
  // Close the last symbol: a half-filled symbol gets its (space) split in two.
  size_t finish() {
    if (half) {
      uint32_t d = g_syms[n].duration0;
      if (d >= 2) {
        g_syms[n].duration0 = d / 2;
        g_syms[n].level1 = g_syms[n].level0;
        g_syms[n].duration1 = d - d / 2;
      } else {
        g_syms[n].level1 = 0; g_syms[n].duration1 = 1;
      }
      half = false; n++;
    }
    return n;
  }
};

size_t buildSymbols(const uint32_t *t, uint16_t count) {
  SymWriter w;
  for (uint16_t i = 0; i < count; i++) w.put((i & 1) == 0, t[i]);
  return w.finish();
}

// Raw signals are stored as uint16 durations in flash.
size_t buildRawSymbols(int raw_index) {
  const IrRawSig &r = irdb::raw(raw_index);
  SymWriter w;
  for (uint16_t i = 0; i < r.count; i++) {
    uint32_t d = irdb::rawSample(r, i);
    bool mark = (i & 1) == 0;
    if (!mark && i == r.count - 1 && d < 40000) d = 40000;   // guarantee an inter-frame gap
    if (d == 0) d = 1;
    w.put(mark, d);
  }
  return w.finish();
}

int g_power = 0;                                   // LED power level (index into LED_POWER_PCT)

void applyCarrier(uint32_t hz, uint8_t duty) {
  static const uint8_t pct[LED_POWER_LEVELS] = LED_POWER_PCT;
  if (duty > LED_DUTY_MAX_PCT) duty = LED_DUTY_MAX_PCT;  // cap first (LED average current), then scale
  uint32_t d = (uint32_t)duty * pct[g_power] / 100;      // the power level scales the carrier's on-time
  duty = d < 4 ? 4 : (uint8_t)d;
  if (hz == g_carrier && duty == g_duty) return;
  rmt_carrier_config_t cc = {};
  cc.frequency_hz = hz;
  cc.duty_cycle = duty / 100.0f;
  cc.flags.polarity_active_low = false;
  cc.flags.always_on = false;
  if (rmt_apply_carrier(g_chan, &cc) == ESP_OK) {
    g_carrier = hz; g_duty = duty;
  }
}

bool transmit(size_t nsyms) {
  if (nsyms == 0) return false;
  rmt_transmit_config_t tc = {};
  tc.loop_count = 0;
  if (rmt_transmit(g_chan, g_copy, g_syms, nsyms * sizeof(rmt_symbol_word_t), &tc) != ESP_OK) return false;
  return rmt_tx_wait_all_done(g_chan, 2000) == ESP_OK;
}

void setLast(const Msg &m, const IrCodeParams &p, uint32_t carrier) {
  xSemaphoreTake(g_last_mux, portMAX_DELAY);
  strncpy(g_last.label, m.label, sizeof(g_last.label) - 1);
  g_last.label[sizeof(g_last.label) - 1] = 0;
  g_last.proto = p.proto; g_last.d = p.d; g_last.s = p.s; g_last.f = p.f;
  g_last.carrier_hz = carrier;
  g_last.frames = 0;
  g_last.at_ms = millis();
  g_last.seq++;
  xSemaphoreGive(g_last_mux);
}

void addFrames(uint32_t n) {
  xSemaphoreTake(g_last_mux, portMAX_DELAY);
  g_last.frames += n;
  xSemaphoreGive(g_last_mux);
}

// Send one key action. Returns a message that interrupted it (new key) via `pending`.
bool runAction(const Msg &m, Msg &pending) {
  IrCodeParams p = (m.code >= 0) ? irdb::codeParams(m.code) : m.params;
  const bool is_raw = (p.proto == IRP_RAW);
  bool holding = (m.type == MSG_HOLD);
  uint32_t start = millis();

  size_t n_first = 0, n_rep = 0;
  uint16_t min_frames = 1;
  static rmt_symbol_word_t rep_syms[MAX_SYMBOLS / 2];   // repeat frame kept separately

  g_toggle = !g_toggle;                                  // new key press -> flip RC5/RC6 toggle
  if (is_raw) {
    const IrRawSig &r = irdb::raw(p.d);
    applyCarrier(r.freq ? r.freq : 38000, r.duty ? r.duty : 33);
    n_first = buildRawSymbols(p.d);
    setLast(m, p, r.freq);
  } else {
    if (!irEncode(p, g_toggle, g_sig)) return false;
    applyCarrier(g_sig.carrier_hz, g_sig.duty_pct);
    min_frames = g_sig.min_frames;
    n_rep = buildSymbols(g_sig.repeat, g_sig.n_repeat);
    if (n_rep > MAX_SYMBOLS / 2) n_rep = MAX_SYMBOLS / 2;
    memcpy(rep_syms, g_syms, n_rep * sizeof(rmt_symbol_word_t));
    n_first = buildSymbols(g_sig.first, g_sig.n_first);
    setLast(m, p, g_sig.carrier_hz);
  }

  g_sending = true;
  transmit(n_first);
  uint32_t frames = 1;
  if (is_raw) {
    n_rep = n_first;                   // raw: repeat = whole capture (already in g_syms)
  } else {
    memcpy(g_syms, rep_syms, n_rep * sizeof(rmt_symbol_word_t));
  }

  bool interrupted = false;
  for (;;) {
    // A newer key press interrupts this one (after the protocol's minimum frames).
    if (!interrupted && xQueueReceive(g_queue, &pending, 0) == pdTRUE) { interrupted = true; holding = false; }
    uint32_t now = millis();
    uint32_t ka = g_keepalive_ms;
    if ((int32_t)(ka - start) < 0) ka = start;
    if (holding && (g_gen_released >= m.gen || now - ka > IR_HOLD_TIMEOUT_MS || now - start > IR_HOLD_MAX_MS))
      holding = false;
    if (!holding && frames >= min_frames) break;
    if (interrupted && frames >= min_frames) break;
    // Cells pulled out (or flat) mid-send: stop as soon as the pack reading drops.
    if (!hw::packOn()) break;
    if (!transmit(n_rep)) break;
    frames++;
  }
  addFrames(frames);
  g_sending = false;
  return interrupted;
}

void arm(bool on) {
#if defined(LED_ARM_PIN) && LED_ARM_PIN >= 0
  digitalWrite(LED_ARM_PIN, on ? HIGH : LOW);
  if (on) delay(LED_ARM_SETTLE_MS);
#else
  (void)on;
#endif
}

void txTask(void *) {
  Msg m, pending;
  for (;;) {
    if (xQueueReceive(g_queue, &m, portMAX_DELAY) != pdTRUE) continue;
    float rest = hw::packVolts();                 // cached resting value (0 = no cells at the last read)
    bool flat = rest > 0.5f && rest < PACK_CUTOFF_V;
    if (!hw::packOn() || flat) {
      // No cells in, or flat (sending would only drain NiMH cells further): drop this press and
      // anything queued behind it; the screen says "AA out" / "flat", and the speaker buzzes.
      while (xQueueReceive(g_queue, &m, 0) == pdTRUE) {}
      sound::play(sound::ERROR);
      continue;
    }
    arm(true);
    for (;;) {
      while (runAction(m, pending)) m = pending;   // a new key interrupted the previous one
      // keep the supply armed for a key pressed right after (e.g. stepping through a code search)
      if (xQueueReceive(g_queue, &m, pdMS_TO_TICKS(30)) != pdTRUE) break;
    }
    arm(false);
  }
}

void post(MsgType t, int code, const IrCodeParams *p, const char *label) {
  if (!g_queue) return;
  Msg m = {};
  m.type = t;
  m.code = code;
  m.gen = g_gen_posted + 1;
  g_gen_posted = m.gen;
  g_keepalive_ms = millis();
  if (p) m.params = *p;
  if (label) strncpy(m.label, label, sizeof(m.label) - 1);
  xQueueSend(g_queue, &m, pdMS_TO_TICKS(50));
}

void codeLabel(int c, char *out, size_t n) {
  int p = irdb::codeProfile(c);
  snprintf(out, n, "%s: %s", irdb::brandName(irdb::profileBrand(p)), irdb::codeName(c));
}

char g_out_desc[64] = "not started";

}  // namespace

const char *outputDesc() { return g_out_desc; }

bool begin(int gpio, bool active_low, bool open_drain, int drive_strength) {
#if defined(LED_ARM_PIN) && LED_ARM_PIN >= 0
  digitalWrite(LED_ARM_PIN, LOW);            // disarmed (the board's 100k pull-down holds it there at boot)
  pinMode(LED_ARM_PIN, OUTPUT);
#endif
  g_queue = xQueueCreate(8, sizeof(Msg));
  g_last_mux = xSemaphoreCreateMutex();

  rmt_tx_channel_config_t cfg = {};
  cfg.gpio_num = (gpio_num_t)gpio;
  cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz = 1000000;              // 1 tick = 1 us
  cfg.mem_block_symbols = 96;               // 2 blocks on ESP32-S3, refilled by ISR
  cfg.trans_queue_depth = 2;
  // RMT level 1 = LED on. Inverted, every carrier pulse drives the pin LOW and idle is HIGH (in
  // open-drain: released). Not inverted, pulses go HIGH (open-drain: released) and idle is LOW.
  cfg.flags.invert_out = active_low ? 1 : 0;
  cfg.flags.io_od_mode = open_drain ? 1 : 0;
  cfg.flags.init_level = 0;                 // idle = LED off (after the inversion above)
  if (rmt_new_tx_channel(&cfg, &g_chan) != ESP_OK) return false;
  if (drive_strength < 0) drive_strength = 0;
  if (drive_strength > 3) drive_strength = 3;
  gpio_set_drive_capability((gpio_num_t)gpio, (gpio_drive_cap_t)drive_strength);
  // No internal pull resistors on the IR pin: in open-drain mode a pull-down (~45k) would let a
  // trickle of current through an LED wired from 3V3 to the pin, and a pull-up would fight it.
  gpio_pullup_dis((gpio_num_t)gpio);
  gpio_pulldown_dis((gpio_num_t)gpio);
  snprintf(g_out_desc, sizeof(g_out_desc), "GPIO%d \xC2\xB7 %s", gpio,      // "GPIO17 · open drain"
           open_drain ? (active_low ? "open drain, GND = on" : "open drain, GND = off")
                      : (active_low ? "active low" : "active high"));

  rmt_copy_encoder_config_t ec = {};
  if (rmt_new_copy_encoder(&ec, &g_copy) != ESP_OK) return false;
  applyCarrier(38000, 33);
  if (rmt_enable(g_chan) != ESP_OK) return false;

  xTaskCreatePinnedToCore(txTask, "irtx", 6144, nullptr, 5, nullptr, 1);
  return true;
}

void tap(int c) {
  char l[64]; codeLabel(c, l, sizeof(l));
  post(MSG_TAP, c, nullptr, l);
}
void holdStart(int c) {
  char l[64]; codeLabel(c, l, sizeof(l));
  post(MSG_HOLD, c, nullptr, l);
}
void tapParams(const IrCodeParams &p, const char *label)       { post(MSG_TAP, -1, &p, label); }
void holdStartParams(const IrCodeParams &p, const char *label) { post(MSG_HOLD, -1, &p, label); }
void holdKeepAlive() { g_keepalive_ms = millis(); }
void holdStop()      { g_gen_released = g_gen_posted; }

LastSent last() {
  LastSent l;
  xSemaphoreTake(g_last_mux, portMAX_DELAY);
  l = g_last;
  xSemaphoreGive(g_last_mux);
  return l;
}
bool isSending() { return g_sending; }

void setPower(int level) { g_power = level < 0 ? 0 : (level >= LED_POWER_LEVELS ? LED_POWER_LEVELS - 1 : level); }
int  power() { return g_power; }

}  // namespace irtx
