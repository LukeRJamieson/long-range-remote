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
bool g_standby = false;           // on the home network with the access point off (HOME_STANDBY_MIN)
bool g_slow = false;              // ... and idle: processor slowed, Wi-Fi power saving on
uint32_t g_cpu_mhz = 0;           // processor speed to go back to after standby

#define STANDBY_CPU_MHZ 80        // the lowest speed Wi-Fi works at

// Standby, in use by the web page: full speed, but the access point stays off (switching it back
// on in the middle of a page load would disturb the connection the page is using).
void standbyBusy() {
  if (!g_standby || !g_slow) return;
  g_slow = false;
  if (g_cpu_mhz > STANDBY_CPU_MHZ) setCpuFrequencyMhz(g_cpu_mhz);
  wifimgr::standbyPowerSave(false);
}

void enterStandby() {
  if (!g_standby) {
    g_standby = true;
    logln("Idle: standby - the web page still works on the home network");
    wifimgr::standby(true);
    ui::requestRedraw();          // the header shows "Standby"
  }
  if (g_slow) return;
  g_slow = true;
  wifimgr::standbyPowerSave(true);
  g_cpu_mhz = getCpuFrequencyMhz();
  if (g_cpu_mhz > STANDBY_CPU_MHZ) setCpuFrequencyMhz(STANDBY_CPU_MHZ);
}

// A key press (or going to sleep) ends standby: full speed, access point back on.
void leaveStandby() {
  if (!g_standby) return;
  standbyBusy();
  g_standby = false;
  wifimgr::standby(false);
  ui::requestRedraw();
  logln("Idle: out of standby");
}

void sleepNow() {
  leaveStandby();                 // full speed again for the wake-up; suspend() switches the radio off anyway
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

bool inStandby() { return g_standby; }

void activity() {
  g_last = millis();
  standbyBusy();                  // a page request: full speed before it is handled
}

void loop() {
  uint32_t now = millis();
  if (g_wifi_on_at && (int32_t)(now - g_wifi_on_at) >= 0 && !irtx::isSending()) {
    g_wifi_on_at = 0;
    wifimgr::resume();
  }
#if IDLE_SLEEP_S > 0
  if (hw::keys()) { g_last = now; leaveStandby(); return; }
  if (irtx::isSending()) { g_last = now; standbyBusy(); return; }   // sent from the web page: the access point stays off
  if (!g_standby && !wifimgr::suspended() && WiFi.softAPgetStationNum() > 0) { g_last = now; return; }   // phone on the AP
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  // Plugged into a computer (a USB host is talking to the ESP32's own USB port, not just a charger):
  // stay awake. Light sleep stops that port, which made uploads fail ("device attached is not
  // functioning", "No serial data received") and the Serial Monitor go silent.
  if (HWCDC::isPlugged()) { g_last = now; leaveStandby(); return; }
#endif
  const uint32_t idle = now - g_last;
  if (idle < (uint32_t)IDLE_SLEEP_S * 1000u) { standbyBusy(); return; }    // in use (a page request counts)
#if HOME_STANDBY_MIN != 0
  // Idle, but on the home network: stay reachable for the web page. Once in standby, a short loss
  // of the router doesn't end it (wifi_mgr reconnects by itself).
  if (g_standby || (!wifimgr::suspended() && !g_wifi_on_at && wifimgr::state() == wifimgr::CONNECTED)) {
#if HOME_STANDBY_MIN > 0
    if (idle < (uint32_t)IDLE_SLEEP_S * 1000u + (uint32_t)HOME_STANDBY_MIN * 60000u) { enterStandby(); return; }
#else
    enterStandby();
    return;
#endif
  }
#endif
  sleepNow();
#endif
}
}  // namespace power
