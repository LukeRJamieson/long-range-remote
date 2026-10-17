// hw.cpp - see hw.h
#include "hw.h"
#include "config.h"
#include "ir_tx.h"
#include "log.h"
#include <Arduino.h>
#include <math.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace hw {
namespace {
struct Key { uint8_t pin, bit; };
const Key KEYS[] = {
  {KEY_UP_PIN, KEYBIT_UP}, {KEY_DOWN_PIN, KEYBIT_DOWN}, {KEY_LEFT_PIN, KEYBIT_LEFT},
  {KEY_RIGHT_PIN, KEYBIT_RIGHT}, {KEY_SELECT_PIN, KEYBIT_SELECT}, {KEY_BACK_PIN, KEYBIT_BACK},
};

uint8_t g_keys = 0;            // debounced state
uint8_t g_raw = 0;             // last raw reading
uint32_t g_raw_at = 0;         // when the raw reading last changed
uint8_t g_stuck = 0;           // held since power-up
bool g_stuck_checked = false;
float g_volts = NAN;
uint32_t g_volts_at = 0;
float g_scr = 0;
uint32_t g_scr_at = 0;
SemaphoreHandle_t g_adc_mux = nullptr;   // the UI task and the IR task both read the ADC

// Average of 16 calibrated ADC readings, in volts at the pin.
float adcVolts(int pin) {
  if (g_adc_mux) xSemaphoreTake(g_adc_mux, portMAX_DELAY);
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(pin);
  if (g_adc_mux) xSemaphoreGive(g_adc_mux);
  return mv / 16 / 1000.0f;
}

// Percent from a voltage table (descending volts), linear between points.
int fromTable(float v, const float (*t)[2], int n) {
  if (v >= t[0][0]) return (int)t[0][1];
  for (int i = 1; i < n; i++)
    if (v >= t[i][0])
      return (int)lroundf(t[i][1] + (v - t[i][0]) * (t[i - 1][1] - t[i][1]) / (t[i - 1][0] - t[i][0]));
  return 0;
}

uint8_t readRaw() {
  uint8_t v = 0;
  for (const Key &k : KEYS)
    if (digitalRead(k.pin) == LOW) v |= (uint8_t)(1u << k.bit);
  return v;
}
}  // namespace

void reclaimKeyPin(int pin) {
  // GPIO43/44 are UART0 after reset: pinMode() hands them to the GPIO matrix (the USB-C port is the
  // ESP32's own USB, so the serial log is unaffected).
  pinMode(pin, INPUT_PULLUP);
  // the same pull-up while light-sleeping, in case the IDF switches pins to their sleep settings
  gpio_sleep_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
  gpio_sleep_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
}

void begin() {
  if (!g_adc_mux) g_adc_mux = xSemaphoreCreateMutex();
  for (const Key &k : KEYS) reclaimKeyPin(k.pin);
  delay(2);                                           // let the pull-ups charge the ribbon
  g_raw = g_keys = 0;                                 // a key held at power-up is ignored until released
  g_stuck = readRaw();
  g_raw_at = millis();
  analogSetAttenuation(ADC_11db);                     // 0..~3.1 V at the pin
}

uint8_t stuckKeys() { return g_stuck_checked ? g_stuck : 0; }

void prepareSleep() {
  for (const Key &k : KEYS) gpio_wakeup_enable((gpio_num_t)k.pin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
}

void afterSleep() {
  for (const Key &k : KEYS) gpio_wakeup_disable((gpio_num_t)k.pin);
  // the wake-up itself was the key press: take it now rather than after the debounce time
  g_raw = g_keys = readRaw();
  g_raw_at = millis();
  g_volts_at = 0;                                     // fresh battery reading
}

uint8_t keys() {
  uint32_t now = millis();
  uint8_t v = readRaw();
  if (v != g_raw) { g_raw = v; g_raw_at = now; }
  else if (v != g_keys && now - g_raw_at >= KEY_DEBOUNCE_MS) g_keys = v;
  // keys held since power-up stay masked until released (so a stuck key can't drive the UI);
  // after 2 s they are reported as stuck
  if (g_stuck) {
    g_stuck &= v;
    if (!g_stuck_checked && now > 2000) {
      g_stuck_checked = true;
      if (g_stuck) logf("Keys: held since power-up (stuck or shorted?): 0x%02x\n", g_stuck);
    }
  }
  return (uint8_t)(g_keys & ~g_stuck);
}

float packVolts() {
  uint32_t now = millis();
  if (g_volts_at != 0 && now - g_volts_at <= 2000) return g_volts;
  if (g_volts_at != 0 && irtx::isSending()) return g_volts;      // keep the resting value
  g_volts_at = now ? now : 1;
  float v = adcVolts(PACK_ADC_PIN) * PACK_ADC_DIVIDER;
  g_volts = v < PACK_OFF_V ? 0.0f : v;                           // switched off / no cells in
  return g_volts;
}

bool packOn() {
  return adcVolts(PACK_ADC_PIN) * PACK_ADC_DIVIDER >= PACK_OFF_V;
}

int packPercent() {
  float v = packVolts();
  if (isnan(v) || v < 1.0f) return -1;
  // NiMH, volts per cell at light load -> % left. Rough: NiMH is flat for most of its discharge.
  static const float T[][2] = {{1.38f, 100}, {1.30f, 90}, {1.25f, 75}, {1.20f, 45},
                               {1.15f, 20}, {1.10f, 8}, {1.00f, 0}};
  return fromTable(v / PACK_CELLS, T, sizeof(T) / sizeof(T[0]));
}

float screenVolts() {
  uint32_t now = millis();
  if (g_scr_at == 0 || now - g_scr_at > 5000) {
    g_scr_at = now ? now : 1;
    float v = adcVolts(SCREEN_BATT_ADC_PIN) * SCREEN_BATT_DIVIDER;
    g_scr = v < 2.0f ? 0.0f : v;
  }
  return g_scr;
}

int screenPercent() {
  float v = screenVolts();
  if (v < 2.0f) return -1;
  // single Li-ion cell at light load -> % left (rough)
  static const float T[][2] = {{4.15f, 100}, {4.00f, 85}, {3.85f, 65}, {3.75f, 45},
                               {3.65f, 25}, {3.50f, 10}, {3.30f, 0}};
  return fromTable(v, T, sizeof(T) / sizeof(T[0]));
}
}  // namespace hw
