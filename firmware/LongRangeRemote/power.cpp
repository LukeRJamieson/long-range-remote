// power.cpp - see power.h
#include "power.h"
#include "config.h"
#include "hw.h"
#include "ir_tx.h"
#include "log.h"
#include "sound.h"
#include "ui.h"
#include "wifi_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

namespace power {
namespace {
uint32_t g_last = 0;              // millis() of the last activity
uint32_t g_wifi_on_at = 0;        // after a wake-up: when to switch Wi-Fi back on (0 = nothing pending)

void sleepNow() {
  logln("Idle: sleeping - any key wakes the remote");
  ui::setAsleep(true);            // draws "asleep" in the header, so the screen shows the state
  wifimgr::suspend();
  Serial.flush();
  sound::prepareSleep();          // amplifier off, I2S clocks stopped
  hw::prepareSleep();
  esp_light_sleep_start();        // returns when a key pin goes low
  hw::afterSleep();
  sound::afterSleep();
  ui::setAsleep(false);
  // Wi-Fi start-up blocks for a few hundred ms: let the key that woke us go out first.
  g_wifi_on_at = millis() + WIFI_RESUME_DELAY_MS;
  if (!g_wifi_on_at) g_wifi_on_at = 1;
  g_last = millis();
  logln("Woke up");
}
}  // namespace

void begin() { g_last = millis(); }

void activity() { g_last = millis(); }

void loop() {
  uint32_t now = millis();
  if (g_wifi_on_at && (int32_t)(now - g_wifi_on_at) >= 0 && !irtx::isSending()) {
    g_wifi_on_at = 0;
    wifimgr::resume();
  }
#if IDLE_SLEEP_S > 0
  if (hw::keys() || irtx::isSending()) { g_last = now; return; }
  if (!wifimgr::suspended() && WiFi.softAPgetStationNum() > 0) { g_last = now; return; }   // phone on the AP
  if (now - g_last >= (uint32_t)IDLE_SLEEP_S * 1000u) sleepNow();
#endif
}
}  // namespace power
