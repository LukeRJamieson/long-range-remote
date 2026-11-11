// wifi_mgr.cpp - see wifi_mgr.h
#include "wifi_mgr.h"
#include "config.h"
#include "log.h"
#include "settings.h"

#include <WiFi.h>
#include "esp_wifi.h"
#include <string.h>
#include <utility>

namespace wifimgr {
namespace {

// --- written by the Wi-Fi event task, consumed in loop() ---
// A small queue, not single flags: the driver often reports two disconnects a few ms apart (the real
// failure, then the echo of the core's own retry - see below), and the first one carries the reason
// worth showing. Single producer (event task), single consumer (loop).
enum EvType : uint8_t { EV_GOT_IP, EV_DISCONNECTED, EV_ASSOCIATED };
struct Ev { uint8_t type, reason; };
const uint8_t EVQ = 16;
Ev evq[EVQ];
volatile uint8_t evq_head = 0, evq_tail = 0;
volatile uint32_t ev_last_ms = 0;       // millis() of the most recent Wi-Fi event

// --- loop() state ---
String g_ssid;
bool g_connected = false;
bool g_attempt_active = false;
uint32_t g_attempt_started = 0;
uint32_t g_next_try = 0;
uint32_t g_attempts = 0;
uint8_t g_last_reason = 0;
uint8_t g_notfound_streak = 0;
bool g_scan_running = false, g_scanned = false;
int g_visible = -1;                     // -1 unknown, 0 not seen in scan, 1 seen
uint32_t g_changes = 0;
bool g_watchdog_fired = false;
bool g_disc_requested = false;          // we called WiFi.disconnect(): its event ends the attempt
// Arduino-ESP32 3.x (libraries/WiFi/src/STA.cpp, `static bool first_connect`): after the FIRST
// disconnect since boot (any reason except ASSOC_LEAVE) the core retries once by itself, even with
// auto-reconnect off. It calls disconnect() - which the driver reports as another disconnect,
// reason 36 STA_LEAVING - and then connect(). We mirror that rule so we neither start a second
// attempt on top of the core's one nor mistake the STA_LEAVING echo for the attempt's result.
bool g_core_retry_used = false;

bool g_any_security = false;
bool g_scan_for_attempt = false;        // the running scan is the first step of a connection attempt
uint32_t g_scan_started = 0;
bool g_suspended = false;               // radio off while the remote sleeps
bool g_standby = false;                 // access point off + modem power saving (see standby())
bool g_print_scan = true;               // print the next scan result (first one, and after changes)
uint32_t g_targeted_fails = 0;          // failed attempts aimed at a scanned router (channel + BSSID)
uint8_t g_authexp_streak = 0;           // attempts in a row that ended in AUTH_EXPIRE (router never answered)
uint8_t g_mac_base[6] = {0};            // the station's factory MAC address
bool g_have_mac = false;
// Network list for the Wi-Fi menu (requestNetworks / networks)
const int MAX_NETS = 24;
Network g_nets[MAX_NETS];
int g_n_nets = 0;
bool g_list_wanted = false;             // a scan for the list is wanted / running
uint32_t g_list_retry_at = 0;
uint8_t g_silent_streak = 0;            // watchdog resets in a row with no driver event at all            // after a "no compatible security" failure: accept any
const uint32_t ATTEMPT_WATCHDOG_MS = 45000;   // a normal attempt ends (either way) in < 20 s
const uint32_t DISCONNECT_WAIT_MS = 5000;     // after the watchdog's disconnect(): idle driver = no event
const uint32_t QUIET_MS = 1000;         // start an attempt only after 1 s without driver events

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  Ev e;
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) e = {EV_GOT_IP, 0};
  else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) e = {EV_DISCONNECTED, info.wifi_sta_disconnected.reason};
  else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) e = {EV_ASSOCIATED, 0};
  else return;
  ev_last_ms = millis();
  uint8_t next = (evq_head + 1) % EVQ;
  if (next == evq_tail) return;          // full (cannot happen at Wi-Fi event rates): drop
  evq[evq_head] = e;
  evq_head = next;
}

bool popEvent(Ev &e) {
  if (evq_tail == evq_head) return false;
  e = evq[evq_tail];
  evq_tail = (evq_tail + 1) % EVQ;
  return true;
}

// Disconnect reasons the ESP32 generates itself when it leaves (we, or the core, called disconnect()).
bool isSelfLeave(uint8_t r) { return r == WIFI_REASON_ASSOC_LEAVE || r == WIFI_REASON_STA_LEAVING; }

const char *reasonName(uint8_t r) {
  const char *n = WiFi.STA.disconnectReasonName((wifi_err_reason_t)r);
  if (n && *n) return n;
  switch (r) {                              // names the core (3.3.x) leaves blank
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY - the network was found but its security type was rejected";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD - the network's security is below the minimum allowed";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD - the signal is too weak";
    default:  return "?";
  }
}

const char *authName(int a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";              case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";            case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";  case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";          case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_OWE: return "OWE";                case WIFI_AUTH_WPA3_ENTERPRISE: return "WPA3-Enterprise";
    default: return "other";
  }
}

bool isAuthProblem(uint8_t r) {
  return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_802_1X_AUTH_FAILED ||
         r == WIFI_REASON_MIC_FAILURE;
}
bool isNotFound(uint8_t r) { return r == WIFI_REASON_NO_AP_FOUND || r == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD; }
bool isSecurityMismatch(uint8_t r) {
  return r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY || r == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD;
}

void connectTo(int32_t channel, const uint8_t *bssid);

// Milliseconds from t to now, signed. loop() reads millis() once at the top, but the timestamps it
// compares against can be set later in the same pass (connectTo() stamps the attempt after the
// driver calls, which take a few ms) or by the Wi-Fi event task. A plain unsigned now - t then
// wraps to ~49 days: the watchdog fired at once and killed every attempt (seen on the board,
// 8 Nov: "no response from the driver for 45 s" in the same millisecond as the attempt).
inline int32_t since(uint32_t now, uint32_t t) { return (int32_t)(now - t); }

// Switch the station between its factory MAC address and a fixed second one (the same address with
// the "locally administered" bit set). Why: the remote is switched off by a hardware latch, so it
// never tells the router it is leaving. With protected management frames (WPA3 / WPA2-WPA3 mixed
// networks) the router then keeps the old session alive for minutes and ignores a new
// authentication from the same address - AUTH_EXPIRE until its timer runs out. From the other
// address the router answers straight away. Only called between attempts (station idle).
void swapMac() {
  if (!g_have_mac) return;
  uint8_t cur[6], next[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, cur) != ESP_OK) return;
  memcpy(next, g_mac_base, 6);
  if (memcmp(cur, g_mac_base, 6) == 0) next[0] ^= 0x02;   // factory -> second address
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, next);
  if (err == ESP_OK)
    logf("Wi-Fi: the router may still hold the remote's last session - trying as %02X:%02X:%02X:%02X:%02X:%02X%s\n",
         next[0], next[1], next[2], next[3], next[4], next[5],
         memcmp(next, g_mac_base, 6) == 0 ? " (factory address)" : "");
  else
    logf("Wi-Fi: couldn't change the MAC address (error %d)\n", (int)err);
}

// Step 1 of an attempt: scan, so we know which channel the router is on. Connecting blind makes
// the radio hop over all 13 channels while it also has to keep the LR-Remote access point up -
// with a phone connected to that, attempts could hang for a minute without any result.
void startAttempt() {
  g_ssid = settings::wifiSsid();
  if (g_ssid.length() == 0) return;
  g_attempts++;
  g_watchdog_fired = false;
  int16_t r = WiFi.scanNetworks(true /*async*/, false /*hidden*/, false /*active*/, 150 /*ms per channel*/);
  if (r == WIFI_SCAN_RUNNING) {
    g_scan_running = true;
    g_scan_for_attempt = true;
    g_scan_started = millis();
  } else {
    connectTo(0, nullptr);                // scan refused: connect without it
  }
}

// Step 2: connect. channel/bssid from the scan (0/nullptr = let the driver search).
void connectTo(int32_t channel, const uint8_t *bssid) {
  const String pw = settings::wifiPass();
  if (channel > 0 && WiFi.channel() != channel && !g_standby) {
    // Put our access point on the router's channel now, rather than having the driver switch it
    // mid-connection. A phone on LR-Remote drops off for a moment and rejoins.
    logf("Wi-Fi: moving the LR-Remote access point to channel %ld (the router's channel)\n", (long)channel);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, channel);
  }
  logf("Wi-Fi: connecting to \"%s\" with password \"%s\" (%u chars) (attempt %lu%s%s)\n", g_ssid.c_str(),
       pw.c_str(), (unsigned)pw.length(), (unsigned long)g_attempts,
       channel > 0 ? "" : ", searching all channels",
       pw.length() ? "" : ", no password set - open network only");
  if (bssid)
    logf("Wi-Fi: router %02X:%02X:%02X:%02X:%02X:%02X on channel %ld\n", bssid[0], bssid[1], bssid[2], bssid[3],
         bssid[4], bssid[5], (long)channel);
  // The minimum security is applied by begin(). WPA (not WPA2) keeps old routers working; after a
  // "security not compatible" failure we drop the minimum so whatever the router offers is tried.
  WiFi.setMinSecurity(g_any_security ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK);
  // begin() without connecting, so the station config can be completed before the attempt starts.
  wl_status_t st = WiFi.begin(g_ssid.c_str(), pw.c_str(), channel, bssid, false /*tryConnect*/);
  if (st != WL_CONNECT_FAILED) {
    // The core zeroes the whole station config, which leaves sae_pwe_h2e "unspecified" - in practice
    // WPA3's old hunt-and-peck SAE only. Routers in WPA2/WPA3 mixed mode (most current Wi-Fi 6 ones)
    // often answer only hash-to-element SAE: the ESP32's authentication frame then gets no reply
    // and the attempt ends in reason 2, AUTH_EXPIRE. Offer both methods; WPA2-only routers are
    // unaffected.
    wifi_config_t c;
    if (esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK) {
      c.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
      esp_wifi_set_config(WIFI_IF_STA, &c);
    }
    if (esp_wifi_connect() != ESP_OK) st = WL_CONNECT_FAILED;
  }
  if (st == WL_CONNECT_FAILED) {
    // The driver refused to start (no event will follow): don't wait for the watchdog.
    logf("Wi-Fi: the Wi-Fi driver refused to start the attempt - retrying in %u s\n",
         (unsigned)(WIFI_STA_RETRY_MS / 1000));
    g_attempt_active = false;
    g_next_try = millis() + WIFI_STA_RETRY_MS;
    return;
  }
  g_attempt_active = true;
  g_attempt_started = millis();
}

// Keep the scan result for the Wi-Fi menu: one entry per name (its strongest access point),
// strongest first. Hidden networks (no name) are left out.
void keepNetworkList(int n) {
  g_n_nets = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0 || s.length() > 32) continue;
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    int k = 0;
    while (k < g_n_nets && strcmp(g_nets[k].ssid, s.c_str()) != 0) k++;
    if (k < g_n_nets) {                       // same name again (mesh / second router)
      if (rssi > g_nets[k].rssi) { g_nets[k].rssi = rssi; g_nets[k].open = open; }
      continue;
    }
    if (g_n_nets == MAX_NETS) {               // full: replace the weakest if this one is stronger
      int w = 0;
      for (int j = 1; j < g_n_nets; j++) if (g_nets[j].rssi < g_nets[w].rssi) w = j;
      if (g_nets[w].rssi >= rssi) continue;
      k = w;
    } else {
      k = g_n_nets++;
    }
    strncpy(g_nets[k].ssid, s.c_str(), sizeof(g_nets[k].ssid) - 1);
    g_nets[k].ssid[sizeof(g_nets[k].ssid) - 1] = 0;
    g_nets[k].rssi = rssi;
    g_nets[k].open = open;
  }
  for (int a = 1; a < g_n_nets; a++)          // strongest first (insertion sort, at most 24)
    for (int b = a; b > 0 && g_nets[b].rssi > g_nets[b - 1].rssi; b--) std::swap(g_nets[b], g_nets[b - 1]);
}

void printScan(int n) {
  logf("Wi-Fi: %d networks visible (2.4 GHz only):\n", n);
  g_visible = 0;
  for (int i = 0; i < n; i++) {
    bool match = WiFi.SSID(i) == g_ssid;
    if (match) g_visible = 1;
    logf("  %s %-32s ch %2ld  %4ld dBm  %s\n", match ? "->" : "  ", WiFi.SSID(i).c_str(),
         (long)WiFi.channel(i), (long)WiFi.RSSI(i), authName(WiFi.encryptionType(i)));
    if (match && WiFi.RSSI(i) < -80)
      logln("Wi-Fi: that signal is very weak - move the remote closer to the router to test");
    if (match && (WiFi.encryptionType(i) == WIFI_AUTH_WPA2_ENTERPRISE || WiFi.encryptionType(i) == WIFI_AUTH_WPA3_ENTERPRISE))
      logln("Wi-Fi: that is an enterprise (802.1X) network - not supported");
  }
  if (!g_visible)
    logf("Wi-Fi: \"%s\" is NOT in the list. Check the name (case-sensitive) and that it is a "
                  "2.4 GHz network - the ESP32 cannot see 5 GHz.\n", g_ssid.c_str());
}

}  // namespace

void begin() {
  g_ssid = settings::wifiSsid();
  WiFi.persistent(false);              // credentials live in our own settings, not the driver's NVS
  WiFi.setAutoReconnect(false);        // we own the retries (see header)
  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_country_code(WIFI_COUNTRY, true);   // e.g. AU: channels 1-13
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);          // also accept older WPA-only routers
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  WiFi.setSleep(false);                // snappier web remote
  g_have_mac = esp_wifi_get_mac(WIFI_IF_STA, g_mac_base) == ESP_OK;
  logf("Wi-Fi: access point \"%s\" up at http://%s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
  if (g_ssid.length() == 0)
    logln("Wi-Fi: no home network set (Menu > Wi-Fi network, or wifi.txt on the SD card) - access point only");
  g_next_try = millis();
}

void loop() {
  if (g_suspended) return;
  uint32_t now = millis();

  Ev e;
  while (popEvent(e)) {
    g_silent_streak = 0;
    if (e.type == EV_GOT_IP) {
      g_targeted_fails = 0;
      g_authexp_streak = 0;
      g_connected = true;
      g_attempt_active = false;
      g_disc_requested = false;
      g_attempts = 0;
      g_notfound_streak = 0;
      g_last_reason = 0;
      g_changes++;
      logf("Wi-Fi: connected to \"%s\" - http://%s  (http://%s.local)\n", g_ssid.c_str(),
           WiFi.localIP().toString().c_str(), MDNS_HOSTNAME);
      continue;
    }
    if (e.type == EV_ASSOCIATED) {
      // Joined the access point (DHCP still to come). If we didn't think an attempt was running,
      // the core started one: wait for its result instead of starting another.
      if (!g_attempt_active && !g_connected) { g_attempt_active = true; g_attempt_started = now; }
      continue;
    }

    // ---- disconnected ----
    const uint8_t r = e.reason;
    const bool was_connected = g_connected;
    g_connected = false;
    g_changes++;

    if (!g_core_retry_used && r != WIFI_REASON_ASSOC_LEAVE) {
      // The core is now retrying by itself (see g_core_retry_used): the attempt carries on.
      g_core_retry_used = true;
      if (!isSelfLeave(r)) {
        g_last_reason = r;
        g_notfound_streak = isNotFound(r) ? 1 : 0;
        if (isSecurityMismatch(r)) { g_any_security = true; g_print_scan = true; }
      }
      g_disc_requested = false;
      g_attempt_active = true;
      g_watchdog_fired = false;
      g_attempt_started = now;
      logf("Wi-Fi: %s - reason %u (%s); the Wi-Fi core retries once by itself\n",
           was_connected ? "connection lost" : "attempt failed", r, reasonName(r));
      continue;
    }

    if (isSelfLeave(r)) {
      if (g_disc_requested) {                      // the disconnect we asked for: start over soon
        g_disc_requested = false;
        g_attempt_active = false;
        g_next_try = now + 500;
      } else if (was_connected) {                  // dropped by the router
        g_attempt_active = false;
        g_next_try = now + 500;
        logf("Wi-Fi: connection lost - reason %u (%s)\n", r, reasonName(r));
      }
      // Otherwise it's the echo of a disconnect() the core made while restarting an attempt
      // (reason 36 STA_LEAVING right after a real failure). Not a result: keep waiting. The
      // reason shown stays the real one from the event before it.
      continue;
    }

    // A real failure or drop: this attempt is over.
    if (!was_connected) g_targeted_fails++;
    g_last_reason = r;
    g_attempt_active = false;
    g_disc_requested = false;
    g_notfound_streak = isNotFound(r) ? g_notfound_streak + 1 : 0;
    if (isSecurityMismatch(r)) {
      if (!g_any_security) logln("Wi-Fi: retrying with the minimum security requirement removed");
      g_any_security = true;
      g_print_scan = true;                    // the scan shows the router's security type
    }
    logf("Wi-Fi: %s - reason %u (%s)\n", was_connected ? "connection lost" : "attempt failed", r, reasonName(r));
    uint32_t wait = was_connected ? 500 : WIFI_STA_RETRY_MS;           // reconnect quickly after a drop
    if (r == WIFI_REASON_AUTH_EXPIRE && !was_connected) {
      // The router didn't answer our authentication request. Hammering it makes it worse: many
      // routers ignore a station for a while after a burst of failed (WPA3) authentications. Back
      // off 5 / 10 / 15 / 20 s, and from the second one on let the driver choose the access point
      // itself (another node of a mesh, or the same one on a fresh channel lookup).
      if (g_authexp_streak < 255) g_authexp_streak++;
      wait = WIFI_STA_RETRY_MS * (g_authexp_streak < 4 ? g_authexp_streak : 4);
      logf("Wi-Fi: the router didn't answer (%u in a row) - waiting %lu s before trying again\n",
           (unsigned)g_authexp_streak, (unsigned long)(wait / 1000));
      if (g_authexp_streak % 2 == 0) swapMac();   // 2nd, 4th, ...: try the other address
    } else if (!was_connected) {
      g_authexp_streak = 0;
    }
    g_next_try = now + wait;
  }

  // Scan finished (or stuck): pick the strongest access point with our network name.
  if (g_scan_running) {
    int n = WiFi.scanComplete();
    bool stuck = n == WIFI_SCAN_RUNNING && since(now, g_scan_started) > 15000;
    if (n >= 0 || n == WIFI_SCAN_FAILED || stuck) {
      int best = -1;
      if (g_ssid.length())
        for (int i = 0; i < n; i++)
          if (WiFi.SSID(i) == g_ssid && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
      int prev_visible = g_visible;
      if (n >= 0 && g_ssid.length()) {
        g_visible = best >= 0 ? 1 : 0;
        if (g_print_scan || g_visible != prev_visible) { printScan(n); g_print_scan = false; }
      }
      if (n >= 0) { keepNetworkList(n); g_list_wanted = false; }
      else if (g_list_wanted) g_list_retry_at = now + 2000;     // failed: the list scan tries again
      g_scanned = true;
      int32_t ch = best >= 0 ? WiFi.channel(best) : 0;
      uint8_t bssid[6];
      if (best >= 0) memcpy(bssid, WiFi.BSSID(best), 6);
      WiFi.scanDelete();
      g_scan_running = false;
      if (g_scan_for_attempt) {
        g_scan_for_attempt = false;
        if (best >= 0 && g_targeted_fails % 3 != 2 && g_authexp_streak < 2) {
          connectTo(ch, bssid);             // the normal case: straight to the router we saw
        } else if (n < 0 || best >= 0) {
          connectTo(0, nullptr);            // scan failed, or every 3rd retry: let the driver search
        } else {
          g_last_reason = WIFI_REASON_NO_AP_FOUND;
          g_notfound_streak++;
          g_changes++;
          if (g_notfound_streak % 3 == 0) {
            // A hidden network is never in the scan. Every third time, connect without it: the
            // driver then asks for the network by name, which a hidden router answers.
            logf("Wi-Fi: \"%s\" not found in the scan (attempt %lu) - trying it anyway, in case it is hidden\n",
                 g_ssid.c_str(), (unsigned long)g_attempts);
            connectTo(0, nullptr);
          } else {
            logf("Wi-Fi: \"%s\" not found in the scan (attempt %lu) - scanning again in %u s\n", g_ssid.c_str(),
                 (unsigned long)g_attempts, (unsigned)(WIFI_STA_RETRY_MS / 1000));
            g_next_try = now + WIFI_STA_RETRY_MS;
          }
        }
      }
    }
  }

  // A scan for the Wi-Fi menu's network list, when nothing else is using the radio. An attempt
  // that is due goes first (its scan fills the list as well).
  bool attempt_due = !g_connected && g_ssid.length() && (int32_t)(now - g_next_try) >= 0;
  if (g_list_wanted && !g_scan_running && !g_attempt_active && !attempt_due &&
      (int32_t)(now - g_list_retry_at) >= 0 && since(now, ev_last_ms) >= (int32_t)QUIET_MS) {
    int16_t r = WiFi.scanNetworks(true /*async*/, false /*hidden*/, false /*active*/, 120 /*ms per channel*/);
    if (r == WIFI_SCAN_RUNNING) {
      g_scan_running = true;
      g_scan_for_attempt = false;
      g_scan_started = now;
    } else {
      g_list_retry_at = now + 2000;
    }
  }

  // Never give up: start the next attempt when the previous one has finished.
  // Wait for the driver to go quiet first, so a trailing event can't land on the new attempt.
  if (!g_connected && !g_attempt_active && !g_scan_running && g_ssid.length() &&
      (int32_t)(now - g_next_try) >= 0 && since(now, ev_last_ms) >= (int32_t)QUIET_MS) {
    startAttempt();
  }

  // Watchdog: an attempt the driver never reported back on. First ask the driver to stop (its
  // disconnect event then takes the normal retry path). An idle driver sends no event for that, so
  // after a few seconds start over regardless. If the driver keeps going silent, restart the
  // station interface (the access point stays up).
  if (g_attempt_active && since(now, g_attempt_started) > (int32_t)(g_watchdog_fired ? DISCONNECT_WAIT_MS : ATTEMPT_WATCHDOG_MS)) {
    if (!g_watchdog_fired) {
      logf("Wi-Fi: no response from the driver for %u s - resetting the attempt\n",
           (unsigned)(ATTEMPT_WATCHDOG_MS / 1000));
      g_disc_requested = true;
      WiFi.disconnect(false, false);
      g_watchdog_fired = true;
      g_attempt_started = now;
    } else {
      g_watchdog_fired = false;
      g_disc_requested = false;
      g_attempt_active = false;
      if (++g_silent_streak >= 2) {
        g_silent_streak = 0;
        logln("Wi-Fi: the driver keeps going silent - restarting the Wi-Fi station");
        WiFi.enableSTA(false);
        delay(200);
        WiFi.setHostname(MDNS_HOSTNAME);
        WiFi.enableSTA(true);
      }
      g_print_scan = true;                    // show what the radio can see
      g_next_try = now + 1000;
    }
  }
}

void suspend() {
  if (g_suspended) return;
  g_suspended = true;
  g_standby = false;                   // resume() brings the access point back
  WiFi.mode(WIFI_OFF);
  g_connected = false;
  g_attempt_active = false;
  g_disc_requested = false;
  g_scan_running = false;
  g_scan_for_attempt = false;
  g_changes++;
  logln("Wi-Fi: off (sleeping)");
}

void resume() {
  if (!g_suspended) return;
  Ev e;
  while (popEvent(e)) {}               // events from switching off are not results of anything
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_country_code(WIFI_COUNTRY, true);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  WiFi.setSleep(false);
  g_suspended = false;
  g_attempts = 0;
  g_scanned = false;
  g_visible = -1;
  g_next_try = millis() + QUIET_MS;
  g_changes++;
  logf("Wi-Fi: back on, access point \"%s\" at http://%s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

bool suspended() { return g_suspended; }

void standby(bool on) {
  if (g_suspended || on == g_standby) return;
  g_standby = on;
  if (on) {
    WiFi.enableAP(false);              // station only: lets the radio sleep between the router's beacons
    WiFi.setSleep(true);
    logln("Wi-Fi: standby - still on the home network, access point off");
  } else {
    WiFi.setSleep(false);
    int32_t ch = g_connected ? WiFi.channel() : 0;
    if (ch > 0) WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, ch);   // on the router's channel
    else WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    logf("Wi-Fi: out of standby, access point \"%s\" back on\n", WIFI_AP_SSID);
  }
  g_changes++;
}

void standbyPowerSave(bool on) {
  if (g_suspended || !g_standby) return;
  WiFi.setSleep(on);
}

void reconnect() {
  g_ssid = settings::wifiSsid();
  g_attempts = 0;
  g_authexp_streak = 0;
  g_notfound_streak = 0;
  g_last_reason = 0;
  g_scanned = false;
  g_visible = -1;
  g_changes++;
  if (g_connected || g_attempt_active) {
    g_disc_requested = true;
    WiFi.disconnect(false, false);    // the disconnect event schedules the new attempt (0.5 s)
  } else {
    g_next_try = millis();
  }
}

State state() {
  if (g_ssid.length() == 0) return NOT_CONFIGURED;
  if (g_connected) return CONNECTED;
  if (g_attempts >= 2 && (isAuthProblem(g_last_reason) || isNotFound(g_last_reason) || isSecurityMismatch(g_last_reason)))
    return FAILING;
  return CONNECTING;
}

const char *statusText() {
  if (g_suspended) return "off while asleep";
  if (g_ssid.length() == 0) return "not configured";
  if (g_connected) return "connected";
  if (g_visible == 0) return "network not visible - 2.4 GHz? name spelled exactly right? - retrying";
  if (isAuthProblem(g_last_reason)) return "rejected - wrong password? - retrying";
  if (isSecurityMismatch(g_last_reason) && settings::wifiPass().length() == 0)
    return "no password set - enter it in Menu > Wi-Fi network";
  if (isSecurityMismatch(g_last_reason)) return "unsupported security (WPA3-only or enterprise?) - retrying";
  if (isNotFound(g_last_reason)) return "network not found - retrying";
  if (g_last_reason) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "connecting... (last failure: %s)", reasonName(g_last_reason));
    return buf;
  }
  return "connecting...";
}

uint32_t attempts() { return g_attempts; }
String ssid() { return g_ssid; }
String ip() { return g_connected ? WiFi.localIP().toString() : String(""); }
String apIp() { return WiFi.softAPIP().toString(); }
uint32_t changeCounter() { return g_changes; }

void requestNetworks() {
  g_list_wanted = true;
  g_list_retry_at = 0;
}

int networks(Network *out, int max) {
  if (g_list_wanted) return -1;
  int n = g_n_nets < max ? g_n_nets : max;
  for (int i = 0; i < n; i++) out[i] = g_nets[i];
  return n;
}

}  // namespace wifimgr
