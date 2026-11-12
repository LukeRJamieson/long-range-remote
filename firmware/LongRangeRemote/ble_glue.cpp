// ble_glue.cpp - the radio side of the Bluetooth scanner (see ble_scan.h), on the esp32 core's BLE
// library (3.x). Adverts arrive on the Bluetooth task; they are copied into a small queue and taken
// into the list from the Arduino loop, so the list is only ever touched from one task.
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "ble_scan.h"
#include "log.h"
#include "sd_store.h"

namespace blescan {
namespace {

// ---- adverts from the Bluetooth task ----
struct Heard { uint8_t mac[6]; uint8_t addr_type; int8_t rssi; uint8_t len; uint8_t data[62]; };
const int QN = 48;
Heard g_q[QN];
volatile int g_qhead = 0, g_qtail = 0;
volatile uint32_t g_dropped = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c |= 0x20;
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

class Callbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    Heard h;
    String a = d.getAddress().toString();          // "aa:bb:cc:dd:ee:ff"
    if (a.length() < 17) return;
    for (int i = 0; i < 6; i++) {
      int hi = hexval(a[i * 3]), lo = hexval(a[i * 3 + 1]);
      if (hi < 0 || lo < 0) return;
      h.mac[i] = (hi << 4) | lo;
    }
    h.addr_type = (uint8_t)d.getAddressType();
    h.rssi = (int8_t)d.getRSSI();
    size_t n = d.getPayloadLength();
    if (n > sizeof(h.data)) n = sizeof(h.data);
    h.len = n;
    if (n) memcpy(h.data, d.getPayload(), n);
    portENTER_CRITICAL(&g_mux);
    int next = (g_qhead + 1) % QN;
    if (next != g_qtail) { g_q[g_qhead] = h; g_qhead = next; }
    else g_dropped++;
    portEXIT_CRITICAL(&g_mux);
  }
};
Callbacks g_cb;

// ---- radio state ----
BLEScan *g_scan = nullptr;
bool g_started = false;          // BLEDevice::init done
bool g_failed = false;
bool g_running = false;          // a scan is going
volatile bool g_chunk_done = true;
uint32_t g_want_until = 0;
const uint32_t CHUNK_S = 10;     // scans run 10 s at a time and are restarted while wanted

void chunkDone(BLEScanResults) { g_chunk_done = true; }

// ---- oui.bin (see tools/ble_db/make_ble_db.py): "OUI1", count, names length, entries, names ----
const uint8_t *g_oui = nullptr;
size_t g_oui_len = 0;
uint32_t g_oui_n = 0;
bool g_oui_tried = false;

uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint32_t rd24(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16); }

void loadOui() {
  g_oui_tried = true;
  size_t len = 0;
  uint8_t *buf = sdstore::loadToPsram("/oui.bin", &len, 4 * 1024 * 1024);
  if (!buf) { logln("Bluetooth: no oui.bin on the SD card - no MAC prefix vendors"); return; }
  uint32_t n = len >= 12 ? rd32(buf + 4) : 0, names = len >= 12 ? rd32(buf + 8) : 0;
  if (len < 12 || memcmp(buf, "OUI1", 4) || 12 + (size_t)n * 6 + names != len) {
    logln("Bluetooth: oui.bin is not valid - ignored");
    free(buf);
    return;
  }
  g_oui = buf; g_oui_len = len; g_oui_n = n;
  logf("Bluetooth: %u MAC prefixes loaded from oui.bin\n", (unsigned)n);
}

bool ouiLookup(const uint8_t mac[6], char *out, size_t on) {
  if (!g_oui) return false;
  uint32_t want = (mac[0] << 16) | (mac[1] << 8) | mac[2];
  const uint8_t *e = g_oui + 12, *names = e + g_oui_n * 6;
  size_t names_len = g_oui_len - 12 - g_oui_n * 6;
  int lo = 0, hi = (int)g_oui_n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    uint32_t p = rd24(e + m * 6);
    if (p == want) {
      uint32_t off = rd24(e + m * 6 + 3);
      if (off >= names_len) return false;
      snprintf(out, on, "%.*s", (int)strnlen((const char *)names + off, names_len - off), names + off);
      return true;
    }
    if (p < want) lo = m + 1; else hi = m - 1;
  }
  return false;
}

void startRadio() {
  if (!g_oui_tried) { loadOui(); setOuiLookup(ouiLookup); }
  logf("Bluetooth: starting (free heap %u)\n", (unsigned)ESP.getFreeHeap());
  BLEDevice::init("");
  g_scan = BLEDevice::getScan();
  if (!g_scan) { g_failed = true; logln("Bluetooth: failed to start"); return; }
  g_scan->setAdvertisedDeviceCallbacks(&g_cb, true /* every advert: the signal strength keeps updating */);
  g_scan->setActiveScan(true);        // ask for the scan response too: that is where most names are
  g_scan->setInterval(100);
  g_scan->setWindow(60);              // listen 60 ms of every 100: the rest is left to Wi-Fi
  g_started = true;
  logf("Bluetooth: ready (free heap %u)\n", (unsigned)ESP.getFreeHeap());
}
}  // namespace

void want(uint32_t ms) {
  uint32_t until = millis() + ms;
  if ((int32_t)(until - g_want_until) > 0) g_want_until = until;
}

void loop() {
  // take in what was heard
  for (int k = 0; k < QN; k++) {
    Heard h;
    portENTER_CRITICAL(&g_mux);
    bool any = g_qtail != g_qhead;
    if (any) { h = g_q[g_qtail]; g_qtail = (g_qtail + 1) % QN; }
    portEXIT_CRITICAL(&g_mux);
    if (!any) break;
    feed(h.mac, h.addr_type, h.rssi, h.data, h.len, millis());
  }
  if (g_failed) return;
  bool wanted = (int32_t)(g_want_until - millis()) > 0;
  if (wanted) {
    if (!g_started) startRadio();
    if (g_started && g_chunk_done) {
      g_chunk_done = false;
      if (g_scan->start(CHUNK_S, chunkDone, false)) {
        if (!g_running) logln("Bluetooth: scanning");
        g_running = true;
      } else {
        g_chunk_done = true;          // try again next time round
      }
    }
  } else if (g_running) {
    g_scan->stop();
    g_running = false;
    g_chunk_done = true;
    logf("Bluetooth: scan stopped (%d devices in the list)\n", count());
  }
}

bool scanning() { return g_running; }
bool ouiLoaded() { return g_oui != nullptr; }

void reloadOui() {
  if (g_oui) { free((void *)g_oui); g_oui = nullptr; g_oui_n = 0; g_oui_len = 0; }
  loadOui();
  setOuiLookup(ouiLookup);
  refreshOui();
}

const char *status() {
  if (g_failed) return "Bluetooth failed to start";
  if (g_running) return "scanning";
  return g_started ? "paused" : "off";
}

}  // namespace blescan
