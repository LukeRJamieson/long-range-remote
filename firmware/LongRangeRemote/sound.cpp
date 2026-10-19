// sound.cpp - see sound.h
#include "sound.h"
#include "config.h"
#include "hw.h"
#include "log.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <driver/i2s_std.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static_assert(SOUND_I2C_SCL_PIN == KEY_BACK_PIN, "codec set-up hands the I2C clock pin back to the BACK key");

namespace sound {
namespace {

constexpr uint32_t RATE = 16000;            // Hz; MCLK = 256 x RATE = 4.096 MHz
constexpr int BLOCK = 128;                  // stereo frames per write (8 ms)
constexpr float PEAK = 16000.0f;            // loudest level: about -6 dB of full scale
constexpr float TWO_PI_F = 6.2831853f;

// ---- sounds: a list of tones, synthesised on the fly ----
enum Shape : uint8_t { SINE, BUZZ };        // BUZZ adds a third harmonic: carries better on a small speaker
struct Tone {
  uint16_t hz;                              // 0 = silence
  uint16_t ms;
  uint16_t decay_ms;                        // exponential decay time constant, 0 = steady
  Shape shape;
};
const Tone T_CLICK[] = {{2400, 12, 3, SINE}};
const Tone T_LONG[]  = {{1200, 45, 30, SINE}, {900, 70, 40, SINE}};
const Tone T_ERROR[] = {{440, 110, 0, BUZZ}, {0, 50, 0, SINE}, {440, 110, 0, BUZZ}};
const Tone T_HELLO[] = {{1047, 80, 60, SINE}, {1319, 80, 60, SINE}, {1568, 160, 90, SINE}};
struct CueDef { const Tone *t; int n; };
const CueDef CUES[] = {
  {T_CLICK, 1}, {T_LONG, 2}, {T_ERROR, 3}, {T_HELLO, 3},
};

i2s_chan_handle_t g_tx = nullptr;
QueueHandle_t g_q = nullptr;                // one slot: a newer sound replaces a waiting one
SemaphoreHandle_t g_io = nullptr;           // held while touching the I2S channel or the amplifier
volatile int g_volume = SOUND_VOLUME_DEFAULT;
volatile bool g_muted = false;
volatile bool g_codec = false;              // ES8311 found and set up
bool g_running = false;                     // I2S channel enabled (false while light-sleeping)
bool g_amp = false;                         // amplifier on
uint32_t g_amp_on_at = 0;                   // millis() when it was switched on
uint32_t g_codec_at = 0;                    // millis() when the codec was set up
int g_tries = 0;
uint32_t g_next_try = 0;
char g_status[64] = "not started";

// ---- ES8311 over I2C ----
bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int rd(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

// Slave mode, clocks from the MCLK pin (256 x fs = 4.096 MHz), 16 kHz, 16-bit I2S in, DAC only (the
// ADC side stays muted: nothing records). The same writes, in the same order, as Espressif's es8311
// driver (esp-adf esp_codec_dev: open, set_fs for 16 kHz / 16 bit, start, unmute), with its clock
// coefficients for 4.096 MHz / 16 kHz.
struct RegVal { uint8_t reg, val; };
const RegVal ES8311_INIT[] = {
  // open
  {0x0D, 0xFA},
  {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
  {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
  {0x00, 0x80},                 // chip state machine on, slave
  {0x01, 0x3F},                 // all clocks on, MCLK from the MCLK pin, not inverted
  {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A},
  {0x44, 0x08},                 // no DAC -> ADC reference loop-back
  // set_fs: I2S format, 16 bit; clocks for 4.096 MHz / 16 kHz
  {0x09, 0x0C}, {0x0A, 0x4C},   // serial data in / out: I2S, 16 bit; out stays muted (bit 6)
  {0x02, 0x00},                 // pre-divide 1, pre-multiply 1
  {0x05, 0x00},                 // ADC / DAC clock divide 1
  {0x03, 0x10},                 // single speed, ADC OSR 16
  {0x04, 0x20},                 // DAC OSR 32
  {0x07, 0x00}, {0x08, 0xFF},   // LRCK divider (only used in master mode)
  {0x06, 0x03},                 // SCLK not inverted; divider 4 (master mode only)
  // start
  {0x00, 0x80}, {0x01, 0x3F},
  {0x09, 0x0C},                 // serial data in unmuted
  {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40},
  {0x37, 0x08}, {0x45, 0x00},
  // unmute, volume
  {0x31, 0x00},                 // DAC unmuted
  {0x32, 0xBF},                 // DAC volume 0 dB (the volume setting scales the samples)
};

// Borrows GPIO13/14 for the I2C bus and hands GPIO14 back to the BACK key. Call only from the
// loop task (the one that reads the keys), never while BACK is held.
bool codecSetup() {
  if (!Wire.begin(SOUND_I2C_SDA_PIN, SOUND_I2C_SCL_PIN, 100000)) {
    snprintf(g_status, sizeof(g_status), "I2C start failed");
    return false;
  }
  Wire.setTimeOut(20);
  bool ok = false;
  // Espressif's driver writes this twice first: the ES8311 sometimes misses the first I2C write.
  wr(0x44, 0x08);
  wr(0x44, 0x08);
  int id1 = rd(0xFD), id2 = rd(0xFE);
  if (id1 < 0) {
    snprintf(g_status, sizeof(g_status), "no codec found (ES8311 at 0x%02X)", ES8311_ADDR);
  } else {
    if (id1 != 0x83 || id2 != 0x11)
      logf("Sound: unexpected codec ID %02X %02X (ES8311 is 83 11) - setting it up anyway\n", id1, id2);
    ok = wr(0x00, 0x1F);                       // reset
    delay(20);
    ok = ok && wr(0x00, 0x00);
    for (const RegVal &r : ES8311_INIT) ok = ok && wr(r.reg, r.val);
    snprintf(g_status, sizeof(g_status), ok ? "ready" : "codec set-up failed (I2C)");
  }
  Wire.end();
  pinMode(SOUND_I2C_SDA_PIN, INPUT);           // SDA: back to the board's pull-up
  hw::reclaimKeyPin(SOUND_I2C_SCL_PIN);        // SCL: the BACK key again
  return ok;
}

// ---- I2S ----
bool i2sStart() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4;
  cc.dma_frame_num = BLOCK;
  cc.auto_clear_after_cb = true;               // silence when there is nothing to play
  if (i2s_new_channel(&cc, &g_tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)SOUND_MCLK_PIN,
      .bclk = (gpio_num_t)SOUND_BCLK_PIN,
      .ws = (gpio_num_t)SOUND_LRCK_PIN,
      .dout = (gpio_num_t)SOUND_DOUT_PIN,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
    },
  };
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(g_tx, &sc) != ESP_OK || i2s_channel_enable(g_tx) != ESP_OK) {
    i2s_del_channel(g_tx);
    g_tx = nullptr;
    return false;
  }
  g_running = true;
  return true;
}

bool writeBlock(const int16_t *buf) {
  size_t done = 0;
  return i2s_channel_write(g_tx, buf, BLOCK * 2 * sizeof(int16_t), &done, pdMS_TO_TICKS(100)) == ESP_OK;
}

void amp(bool on) {
  if (on && !g_amp) g_amp_on_at = millis();
  digitalWrite(SOUND_AMP_PIN, on ? HIGH : LOW);
  g_amp = on;
}

// Wait (in the sound task) until both the codec and the amplifier are ready to make a sound. Silence
// keeps going out of the I2S meanwhile (auto-clear), so nothing pops.
void waitReady() {
  for (;;) {
    uint32_t now = millis();
    int32_t a = (int32_t)(g_amp_on_at + SOUND_AMP_START_MS - now);
    int32_t c = (int32_t)(g_codec_at + SOUND_CODEC_SETTLE_MS - now);
    int32_t w = a > c ? a : c;
    if (w <= 0) return;
    vTaskDelay(pdMS_TO_TICKS(w) + 1);
  }
}

// Plays one sound; returns early if a newer one is waiting (it replaces this one).
void render(Cue c) {
  static int16_t buf[BLOCK * 2];
  const CueDef &d = CUES[c];
  int v = g_volume;
  float gain = PEAK * powf(10.0f, -(float)(SOUND_VOLUME_MAX - v) * 3.0f / 20.0f);
  int fill = 0;
  float phase = 0;
  for (int s = 0; s < d.n; s++) {
    const Tone &t = d.t[s];
    int len = (int)(t.ms * RATE / 1000);
    float w = TWO_PI_F * t.hz / RATE;
    float k = t.decay_ms ? expf(-1000.0f / (t.decay_ms * (float)RATE)) : 1.0f;   // decay per sample
    float env = 1.0f;
    const int ramp = RATE / 1000 * 2;          // 2 ms fade in and out: no clicks at the edges
    for (int i = 0; i < len; i++) {
      float y = 0;
      if (t.hz) {
        float edge = 1.0f;
        if (i < ramp) edge = (float)i / ramp;
        else if (len - i < ramp) edge = (float)(len - i) / ramp;
        y = sinf(phase);
        if (t.shape == BUZZ) y = 0.75f * (y + sinf(3 * phase) / 3);
        y *= env * edge;
        phase += w;
        if (phase >= TWO_PI_F) phase -= TWO_PI_F;
        env *= k;
      }
      int16_t q = (int16_t)lroundf(y * gain);
      buf[fill * 2] = buf[fill * 2 + 1] = q;
      if (++fill == BLOCK) {
        fill = 0;
        if (!writeBlock(buf)) return;
        if (uxQueueMessagesWaiting(g_q)) return;
      }
    }
  }
  if (fill) {
    for (int i = fill * 2; i < BLOCK * 2; i++) buf[i] = 0;
    writeBlock(buf);
  }
}

void soundTask(void *) {
  Cue c;
  for (;;) {
    TickType_t wait = g_amp ? pdMS_TO_TICKS(SOUND_AMP_HOLD_MS) : portMAX_DELAY;
    if (xQueueReceive(g_q, &c, wait) != pdTRUE) {          // quiet for a while: amplifier off
      xSemaphoreTake(g_io, portMAX_DELAY);
      amp(false);
      xSemaphoreGive(g_io);
      continue;
    }
    if (!g_codec || g_muted || g_volume <= 0 || c >= sizeof(CUES) / sizeof(CUES[0])) continue;
    xSemaphoreTake(g_io, portMAX_DELAY);
    if (g_running) {
      amp(true);                                           // (no-op if it is already on)
      waitReady();                                         // ~30 ms amplifier start-up, codec settling
      if (uxQueueMessagesWaiting(g_q)) xQueueReceive(g_q, &c, 0);   // a newer sound came in meanwhile
      render(c);
    }
    xSemaphoreGive(g_io);
  }
}

void trySetup(bool at_start) {
  // GPIO14 is the BACK key too: while it is held, SCL is stuck low - try again later.
  if (digitalRead(SOUND_I2C_SCL_PIN) == LOW) {
    snprintf(g_status, sizeof(g_status), "waiting for BACK to be released");
    g_next_try = millis() + 500;
    return;
  }
  g_tries++;
  g_codec = codecSetup();
  g_codec_at = millis();
  logf("Sound: %s\n", g_status);
  if (g_codec) {
    if (at_start && SOUND_STARTUP_CHIME) play(HELLO);
  } else {
    g_next_try = millis() + 2000;
  }
}
}  // namespace

void begin() {
#if SOUND_ENABLED
  pinMode(SOUND_AMP_PIN, OUTPUT);
  amp(false);
  g_q = xQueueCreate(1, sizeof(Cue));
  g_io = xSemaphoreCreateMutex();
  if (!g_q || !g_io || !i2sStart()) {
    snprintf(g_status, sizeof(g_status), "I2S start failed");
    logln("Sound: I2S start failed");
    return;
  }
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 2, nullptr, 0);
  trySetup(true);
#else
  snprintf(g_status, sizeof(g_status), "off in config.h");
#endif
}

void loop() {
  if (g_codec || !g_tx || g_tries >= 3 || (int32_t)(millis() - g_next_try) < 0) return;
  trySetup(false);
}

void play(Cue c) {
  if (!g_q || !g_codec || g_muted || g_volume <= 0) return;
  xQueueOverwrite(g_q, &c);
}

void setVolume(int level) {
  g_volume = level < 0 ? 0 : level > SOUND_VOLUME_MAX ? SOUND_VOLUME_MAX : level;
}
int volume() { return g_volume; }
void setMuted(bool m) { g_muted = m; }
bool muted() { return g_muted; }
bool ready() { return g_codec; }
const char *status() { return g_status; }

void prepareSleep() {
  if (!g_tx) return;
  xSemaphoreTake(g_io, portMAX_DELAY);          // waits for a sound that is playing to finish
  amp(false);
  if (g_running) i2s_channel_disable(g_tx);
  g_running = false;
  xSemaphoreGive(g_io);
}

void afterSleep() {
  if (!g_tx) return;
  xSemaphoreTake(g_io, portMAX_DELAY);
  if (!g_running) g_running = i2s_channel_enable(g_tx) == ESP_OK;
  // the key that woke the remote clicks straight away: start the amplifier now, so it is (nearly) ready
  if (g_running && g_codec && !g_muted && g_volume > 0) amp(true);
  xSemaphoreGive(g_io);
}

}  // namespace sound
