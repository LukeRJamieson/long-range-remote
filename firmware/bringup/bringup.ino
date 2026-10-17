/*
 * Long-range remote - bring-up / example firmware
 * ---------------------------------------------------------------------------------------------
 * Board:  Waveshare ESP32-S3-RLCD-4.2, Arduino-ESP32 core 3.x (uses the 3.x RMT API)
 * Wiring: control board J2 <-> Waveshare 2x8 header via 16-way ribbon (pin n = pin n)
 *
 *   GPIO2  ARM       out  high = AA pack connected to the LEDs (Q6 -> Q5 P-FET on the
 *                         control board; 100k pull-down keeps it off while the ESP32 boots)
 *   GPIO17 IR_DRV    out  38 kHz carrier from the RMT peripheral, idle LOW (100k pull-down), through R6 47R
 *                         straight into Q2 (AOD424, a logic-level MOSFET); keep the default drive strength (2)
 *   Keys, each shorting its pin to GND (ESP32 internal pull-ups, no resistors on the board):
 *     GPIO18 UP   GPIO3 DOWN   GPIO44 LEFT (U0RXD)   GPIO43 RIGHT (U0TXD, via 1k)   GPIO0 SELECT
 *     GPIO14 BACK (the display board's I2C SCL - its own pull-up; this sketch doesn't use I2C)
 *   GPIO1 = AA pack voltage / 2 (470k / 470k on the control board, from the pack side of the power gate,
 *           so it reads at any time); with no cells in it reads ~0 V and nothing is sent.
 *
 * Keys - menu driven:
 *   UP / DOWN       step through the code list
 *   LEFT / RIGHT    step through the device list
 *   SELECT          send the selected code (held = NEC repeat frames); prints battery voltage
 *   BACK            back to the first code of the first device (and proves GPIO14 reads)
 * Hook your display menu into printSelection() / handleButtons() - the Serial prints mark the spots.
 *
 * Safety: the LEDs only light while ARM and IR_DRV are both high. ARM is raised for each transmission
 * (the carrier starts ARM_SETTLE_MS later, once the LED supply is up) and dropped afterwards; IR_DRV is
 * always carrier-modulated, never held high.
 * The LEDs run straight off 4 x AA NiMH through 24R resistors: up to ~100 mA per string on fresh cells.
 * 66 x SFH 4545 at up to 100 mA is an eye hazard at short range - never look into the head.
 */
#include <Arduino.h>

// ------------------------------------------------------------------------------- pins / addresses
constexpr int PIN_ARM = 2;
constexpr int PIN_IR_DRV = 17;
constexpr int PIN_PACK = 1;              // AA pack / 2

// ------------------------------------------------------------------------------- IR timing (NEC)
constexpr uint32_t RMT_TICK_HZ = 1000000;   // 1 us per RMT tick
constexpr uint32_t CARRIER_HZ = 38000;
constexpr float CARRIER_DUTY = 0.33f;       // 1/3 duty: standard for IR, keeps average LED current down
constexpr uint16_t NEC_HDR_MARK = 9000, NEC_HDR_SPACE = 4500;
constexpr uint16_t NEC_BIT_MARK = 562, NEC_ONE_SPACE = 1688, NEC_ZERO_SPACE = 562;
constexpr uint16_t NEC_RPT_SPACE = 2250;
constexpr uint32_t NEC_FRAME_PERIOD_MS = 108;

// Arming: Q5's gate is slowed on purpose (C8 gate-source, C7 soft start), so the LED supply takes
// 5-10 ms to come up. Wait before the first carrier pulse.
constexpr uint32_t ARM_SETTLE_MS = 30;
constexpr float PACK_OFF_V = 3.5f;          // below this (at rest): no cells in
constexpr float LOW_BATT_V = 4.6f;          // "low": put in charged cells

// ------------------------------------------------------------------------------- code table
// Example NEC codes - replace with your own (capture them with any IR receiver / IRremoteESP8266).
struct IrCode { const char *name; uint8_t address; uint8_t command; };
struct Device { const char *name; const IrCode *codes; size_t count; };

const IrCode kAmpCodes[] = {
  {"Power", 0x10, 0x0C}, {"Vol +", 0x10, 0x10}, {"Vol -", 0x10, 0x11}, {"Mute", 0x10, 0x0D},
};
const IrCode kTvCodes[] = {
  {"Power", 0x04, 0x08}, {"Input", 0x04, 0x0B}, {"Vol +", 0x04, 0x02}, {"Vol -", 0x04, 0x03},
};
const Device kDevices[] = {
  {"Amplifier", kAmpCodes, sizeof(kAmpCodes) / sizeof(kAmpCodes[0])},
  {"TV", kTvCodes, sizeof(kTvCodes) / sizeof(kTvCodes[0])},
};
constexpr size_t kDeviceCount = sizeof(kDevices) / sizeof(kDevices[0]);

size_t g_device = 0, g_code = 0;

// ------------------------------------------------------------------------------- button state
enum Btn : uint8_t { BTN_UP = 1, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT, BTN_BACK };
const uint8_t kKeyPin[] = {0, 18, 3, 44, 43, 0, 14};                // indexed by Btn (0 unused)
uint8_t g_btn_prev = 0;                    // bit set = pressed

// Returns pressed keys as a bitmask (bit n = Btn n pressed). The 30 ms loop poll debounces.
uint8_t readPressed() {
  uint8_t v = 0;
  for (uint8_t b = BTN_UP; b <= BTN_BACK; b++)
    if (digitalRead(kKeyPin[b]) == LOW) v |= (uint8_t)(1u << b);
  return v;
}

// ------------------------------------------------------------------------------- battery

// The AA pack through the 1/2 divider on GPIO1.
float readBatteryVolts() { return analogReadMilliVolts(PIN_PACK) * 2 / 1000.0f; }

// ------------------------------------------------------------------------------- LED supply arming
void ledArm() {
  digitalWrite(PIN_ARM, HIGH);
  delay(ARM_SETTLE_MS);
}

void ledDisarm() { digitalWrite(PIN_ARM, LOW); }

// ------------------------------------------------------------------------------- NEC encoder
// One rmt_data_t = (mark, space). level 1 = carrier on (LEDs pulsing), level 0 = off.
// Fields are set one by one: rmt_data_t is a union of bit-fields, so brace-initialising it from
// run-time values would be a narrowing error in C++.
rmt_data_t sym(uint16_t markUs, uint16_t spaceUs) {
  rmt_data_t s;
  s.val = 0;
  s.duration0 = markUs;
  s.level0 = 1;
  s.duration1 = spaceUs;               // a 0 duration ends the transmission
  s.level1 = 0;
  return s;
}

size_t buildNecFrame(rmt_data_t *s, uint8_t address, uint8_t command) {
  size_t n = 0;
  s[n++] = sym(NEC_HDR_MARK, NEC_HDR_SPACE);
  uint32_t bits = (uint32_t)address | ((uint32_t)(uint8_t)~address << 8) |
                  ((uint32_t)command << 16) | ((uint32_t)(uint8_t)~command << 24);
  for (int i = 0; i < 32; i++) {                      // LSB first
    bool one = bits & (1UL << i);
    s[n++] = sym(NEC_BIT_MARK, one ? NEC_ONE_SPACE : NEC_ZERO_SPACE);
  }
  s[n++] = sym(NEC_BIT_MARK, 0);                       // stop bit
  return n;                                            // 34 symbols
}

void sendNec(uint8_t address, uint8_t command, uint8_t holdMask) {
  rmt_data_t frame[34];
  size_t n = buildNecFrame(frame, address, command);
  ledArm();
  uint32_t start = millis();
  rmtWrite(PIN_IR_DRV, frame, n, RMT_WAIT_FOR_EVER);
  // NEC repeat frames while the send button is held (one every 108 ms)
  rmt_data_t repeat[2] = {sym(NEC_HDR_MARK, NEC_RPT_SPACE), sym(NEC_BIT_MARK, 0)};
  while (holdMask && (readPressed() & holdMask)) {
    uint32_t next = start + NEC_FRAME_PERIOD_MS;
    while ((int32_t)(millis() - next) < 0) delay(1);
    start = next;
    rmtWrite(PIN_IR_DRV, repeat, 2, RMT_WAIT_FOR_EVER);
  }
  ledDisarm();
}

// ------------------------------------------------------------------------------- UI
void printSelection() {
  const Device &d = kDevices[g_device];
  Serial.printf("[%s] %s  (addr 0x%02X cmd 0x%02X)\n", d.name, d.codes[g_code].name,
                d.codes[g_code].address, d.codes[g_code].command);
}

void handleButtons(uint8_t pressed) {
  uint8_t down = pressed & ~g_btn_prev;              // newly pressed this time
  g_btn_prev = pressed;
  const Device &d = kDevices[g_device];
  if (down & (1 << BTN_UP))    { g_code = (g_code + d.count - 1) % d.count; printSelection(); }
  if (down & (1 << BTN_DOWN))  { g_code = (g_code + 1) % d.count; printSelection(); }
  if (down & (1 << BTN_LEFT))  { g_device = (g_device + kDeviceCount - 1) % kDeviceCount; g_code = 0; printSelection(); }
  if (down & (1 << BTN_RIGHT)) { g_device = (g_device + 1) % kDeviceCount; g_code = 0; printSelection(); }
  if (down & (1 << BTN_BACK))  { g_device = 0; g_code = 0; Serial.print("BACK: "); printSelection(); }
  const uint8_t sendMask = 1 << BTN_SELECT;
  if (down & sendMask) {
    const IrCode &c = kDevices[g_device].codes[g_code];
    float vb = readBatteryVolts();
    if (vb < PACK_OFF_V) {                            // no cells in: nothing to send with
      Serial.println("AA pack out (no cells in)");
      return;
    }
    Serial.printf("TX %s / %s   (battery %.2f V)\n", kDevices[g_device].name, c.name, vb);
    if (vb < LOW_BATT_V) Serial.println("  low battery - put in charged cells");
    sendNec(c.address, c.command, down & sendMask);  // repeats while that button is held
    g_btn_prev = readPressed();                      // button released while sending
  }
}

// ------------------------------------------------------------------------------- setup / loop
void setup() {
  // Outputs low first: both LED interlocks off
  pinMode(PIN_ARM, OUTPUT);
  digitalWrite(PIN_ARM, LOW);
  for (uint8_t b = BTN_UP; b <= BTN_BACK; b++) pinMode(kKeyPin[b], INPUT_PULLUP);

#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);   // native USB: don't stall when no terminal is reading (output is just dropped)
#endif
  Serial.begin(115200);
  delay(200);
  Serial.println("\nLong-range remote bring-up");
  Serial.printf("Battery %.2f V\n", readBatteryVolts());

  // RMT transmitter on IR_DRV: 1 us ticks, 38 kHz carrier on 'mark' symbols, idle LOW
  if (!rmtInit(PIN_IR_DRV, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_TICK_HZ)) {
    Serial.println("ERROR: RMT init failed");
  }
  rmtSetCarrier(PIN_IR_DRV, true, true, CARRIER_HZ, CARRIER_DUTY);
  rmtSetEOT(PIN_IR_DRV, LOW);

  printSelection();
}

void loop() {
  // poll the keys every 30 ms (longer than a tactile switch bounces)
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll >= 30) {
    lastPoll = millis();
    handleButtons(readPressed());
  }
  delay(2);
}

/*
 * Deep sleep (optional): GPIO0, GPIO3 and GPIO18 are RTC GPIOs, so SELECT, DOWN or UP can wake it:
 *     ledDisarm();
 *     esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 3) | (1ULL << 18), ESP_EXT1_WAKEUP_ANY_LOW);
 *     esp_deep_sleep_start();
 * (keep the pull-ups on in deep sleep with rtc_gpio_pullup_en() for each pin.)
 */
