// Mock of the Arduino-ESP32 3.x STA behaviour that matters here (see libraries/WiFi/src/STA.cpp):
//  * begin() while an attempt is in progress fails ("sta is connecting, cannot set config")
//  * the core retries ONCE by itself after the first failure since boot (any reason except
//    ASSOC_LEAVE), even with autoReconnect off: disconnect() + connect(). The driver reports that
//    disconnect() as another disconnect event, reason 36 STA_LEAVING, a few ms after the real one
//    (seen in espressif/arduino-esp32 issue #12714: "201 - NO_AP_FOUND" then "36 - STA_LEAVING").
//  * disconnect() while connected -> ASSOC_LEAVE; while connecting or idle -> STA_LEAVING
//  * results arrive later as events, on another task
#pragma once
#include <Arduino.h>
#include <vector>
enum arduino_event_id_t { ARDUINO_EVENT_WIFI_STA_GOT_IP = 1, ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 2, ARDUINO_EVENT_WIFI_STA_CONNECTED = 3 };
struct arduino_event_info_t { struct { uint8_t reason; } wifi_sta_disconnected; };
enum wifi_err_reason_t { WIFI_REASON_AUTH_FAIL = 202, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15, WIFI_REASON_HANDSHAKE_TIMEOUT = 204,
  WIFI_REASON_802_1X_AUTH_FAILED = 23, WIFI_REASON_MIC_FAILURE = 14, WIFI_REASON_NO_AP_FOUND = 201,
  WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD = 212, WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY = 210,
  WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD = 211, WIFI_REASON_ASSOC_LEAVE = 8, WIFI_REASON_BEACON_TIMEOUT = 200, WIFI_REASON_STA_LEAVING = 36 };
enum { WIFI_OFF = 0, WIFI_AP_STA = 3, WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
  WIFI_AUTH_WAPI_PSK, WIFI_AUTH_OWE, WIFI_AUTH_WPA3_ENT_192, WIFI_AUTH_WPA3_ENTERPRISE = 17 };
enum wl_status_t { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
struct IP { std::string s; String toString() const { return String(s.c_str()); } };
struct SimWiFi {
  // ---- world model ----
  bool ap_present = true; bool pass_ok = true; bool hang = false; // hang: driver never reports
  bool hidden = false;               // router doesn't show up in scans (hidden SSID), but connecting by name works
  bool needs_any_security = false;   // router's security is rejected unless the minimum is OPEN (-> 210)
  bool deaf = false;                 // driver accepts begin() but never does anything, until the STA restarts
  bool refuse = false;               // begin() fails outright (WL_CONNECT_FAILED), no event
  int min_sec = WIFI_AUTH_WPA_PSK; int sta_restarts = 0;
  int attempt_ms = 3000;
  // ---- driver state ----
  bool busy = false, connected = false, first_connect = true; uint32_t done_at = 0; int pending_reason = 0;
  std::function<void(arduino_event_id_t, arduino_event_info_t)> cb;
  int begins = 0, begin_rejected = 0; uint32_t connected_at = 0;
  int idle_echo_at = -1;                 // pending STA_LEAVING from disconnect() on an idle driver
  std::vector<int> reasons;              // every disconnect reason reported, in order
  void raw_disc(int r) { reasons.push_back(r); arduino_event_info_t i; i.wifi_sta_disconnected.reason = r; cb(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, i); }
  void fire_disc(int r) { raw_disc(r);
    if (first_connect && r != WIFI_REASON_ASSOC_LEAVE) {      // core's retry-once: disconnect() + connect()
      first_connect = false; raw_disc(WIFI_REASON_STA_LEAVING); start(); } }
  void start() { busy = true; done_at = sim_ms + attempt_ms; }
  void tick() {
    if (busy && !hang && (int32_t)(sim_ms - done_at) >= 0) {
      busy = false;
      if (pending_reason) { int r = pending_reason; pending_reason = 0; fire_disc(r); return; }
      if (!ap_present) fire_disc(WIFI_REASON_NO_AP_FOUND);
      else if (needs_any_security && min_sec != WIFI_AUTH_OPEN) fire_disc(WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY);
      else if (!pass_ok) fire_disc(WIFI_REASON_AUTH_FAIL);
      else { connected = true; connected_at = sim_ms; arduino_event_info_t i{}; cb(ARDUINO_EVENT_WIFI_STA_CONNECTED, i); cb(ARDUINO_EVENT_WIFI_STA_GOT_IP, i); }
    }
    if (idle_echo_at >= 0 && (int32_t)(sim_ms - idle_echo_at) >= 0) { idle_echo_at = -1; fire_disc(WIFI_REASON_STA_LEAVING); }
    if (connected && !ap_present) { connected = false; fire_disc(WIFI_REASON_BEACON_TIMEOUT); }
  }
  // ---- API used by wifi_mgr ----
  // ---- scanning: the real driver refuses to scan while it is connecting ----
  int scan_state = -3; uint32_t scan_done_at = 0; int scans = 0, scan_refused = 0, ap_channel = 1, ap_moves = 0;
  int begins_targeted = 0; uint8_t bssid_[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  int scanNetworks(bool, bool, bool = false, uint32_t = 300) {
    if (busy) { scan_refused++; return WIFI_SCAN_FAILED; }
    scans++; scan_state = WIFI_SCAN_RUNNING; scan_done_at = sim_ms + 1500; return WIFI_SCAN_RUNNING; }
  int scanComplete() {
    if (scan_state == WIFI_SCAN_RUNNING && (int32_t)(sim_ms - scan_done_at) >= 0) scan_state = ap_present && !hidden ? 2 : 1;
    return scan_state; }
  void scanDelete() { scan_state = -3; }
  String SSID(int i) { return String(i == 1 && ap_present ? "Home" : "Neighbour"); }
  long channel(int) { return 6; } long RSSI(int i) { return i == 1 ? -40 : -70; }
  uint8_t *BSSID(int) { return bssid_; }
  long channel() { return ap_channel; }
  void softAP(const char *, const char *, int ch = 1) { if (ch != ap_channel) ap_moves++; ap_channel = ch; }
  wl_status_t begin(const char *, const char *, int32_t ch = 0, const uint8_t *bssid = nullptr) { begins++;
    if (ch && bssid) begins_targeted++;
    if (refuse) return WL_CONNECT_FAILED;
    if (busy) { begin_rejected++; return WL_CONNECT_FAILED; }
    if (connected) connected = false;
    if (deaf) return WL_DISCONNECTED;              // accepted, then nothing ever happens
    start(); return WL_DISCONNECTED; }
  bool enableSTA(bool on) { if (!on) { busy = false; connected = false; } else { deaf = false; sta_restarts++; } return true; }
  void setMinSecurity(int m) { min_sec = m; }
  wifi_auth_mode_t encryptionType(int) { return WIFI_AUTH_WPA2_PSK; }
  bool disconnect(bool, bool) {
    if (connected)  { busy = true; connected = false; pending_reason = WIFI_REASON_ASSOC_LEAVE; done_at = sim_ms + 50; }
    else if (busy)  { pending_reason = WIFI_REASON_STA_LEAVING; done_at = sim_ms + 50; hang = false; }
    // idle: no event at all (seen on the real board: the watchdog's disconnect() got no reply)
    return true; }
  void persistent(bool) {} void setAutoReconnect(bool) {} void onEvent(std::function<void(arduino_event_id_t, arduino_event_info_t)> f) { cb = f; }
  void mode(int) {} void setHostname(const char *) {} void setSleep(bool) {}
  IP softAPIP() { return {"192.168.4.1"}; } IP localIP() { return {"192.168.1.50"}; }

  struct { const char *disconnectReasonName(wifi_err_reason_t) { return "r"; } } STA;
};
extern SimWiFi WiFi;
