#include "wifi_mgr.h"
#include <WiFi.h>
#include <assert.h>
#include <string.h>
uint32_t sim_ms = 0; SerialC Serial; SimWiFi WiFi; String sim_ssid("Home"), sim_pass("pw");
#include <stdarg.h>
void logf(const char *fmt, ...) { if (Serial.quiet) return; va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
void logln(const char *s) { if (!Serial.quiet) puts(s); }
namespace settings { String wifiSsid() { return sim_ssid; } String wifiPass() { return sim_pass; } }
void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 10) { sim_ms += 10; WiFi.tick(); wifimgr::loop(); } }
int fails = 0;
void check(bool c, const char *what) { printf("  [%s] %s\n", c ? "ok" : "FAIL", what); if (!c) fails++; }
int main() {
  wifimgr::begin();
  puts("0. first attempt after boot fails: the core retries by itself (+ STA_LEAVING echo)");
  WiFi.pass_ok = false; WiFi.attempt_ms = 7000;          // slow attempts (handshake timeouts)
  run(10500);                                            // scan, first attempt failed, core's retry running
  check(WiFi.reasons.size() >= 2 && WiFi.reasons[1] == WIFI_REASON_STA_LEAVING, "mock reproduces a failure then 36");
  check(strstr(wifimgr::statusText(), "password") != nullptr, "status shows the real reason, not STA_LEAVING");
  run(8000);
  check(WiFi.begin_rejected == 0 && WiFi.scan_refused == 0, "nothing started on top of the core's own retry");
  WiFi.attempt_ms = 3000; WiFi.pass_ok = true; WiFi.ap_present = false;
  puts("1. router off at boot, switched on after 2 minutes");
  run(101500);
  check(wifimgr::state() == wifimgr::FAILING, "reports 'network not found' while retrying");
  check(wifimgr::attempts() > 10, "kept retrying the whole time");
  WiFi.ap_present = true; uint32_t on = sim_ms; run(20000);
  check(wifimgr::state() == wifimgr::CONNECTED, "connects once the router is back");
  printf("     connected %lu ms after the router appeared\n", (unsigned long)(WiFi.connected_at - on));
  puts("2. router reboots while connected (gone 30 s)");
  WiFi.ap_present = false; run(30000); WiFi.ap_present = true; on = sim_ms; run(20000);
  check(wifimgr::state() == wifimgr::CONNECTED, "reconnects after the drop");
  puts("3. wrong password, fixed on the web page after 1 minute");
  WiFi.pass_ok = false; wifimgr::reconnect(); run(60000);
  check(wifimgr::state() == wifimgr::FAILING, "reports wrong password while retrying");
  WiFi.pass_ok = true; wifimgr::reconnect(); run(15000);
  check(wifimgr::state() == wifimgr::CONNECTED, "connects with the corrected password");
  puts("4. driver hangs (never reports a result)");
  WiFi.ap_present = false; run(8000); WiFi.hang = true; run(30000); WiFi.ap_present = true; run(150000);
  check(wifimgr::state() == wifimgr::CONNECTED, "watchdog recovers and connects");
  puts("5. slow router: each attempt takes 25 s");
  WiFi.attempt_ms = 25000; WiFi.ap_present = false; run(20000); WiFi.ap_present = true; run(90000);
  check(wifimgr::state() == wifimgr::CONNECTED, "a slow attempt is never interrupted");
  puts("6. a stray STA_LEAVING while connected (router kicks us) - reconnects");
  WiFi.attempt_ms = 3000; run(5000);
  WiFi.connected = false; WiFi.raw_disc(WIFI_REASON_STA_LEAVING); run(15000);
  check(wifimgr::state() == wifimgr::CONNECTED, "reconnects after an unexplained leave");
  puts("7. credentials changed on the web page while an attempt is running");
  WiFi.ap_present = false; WiFi.raw_disc(WIFI_REASON_BEACON_TIMEOUT); WiFi.connected = false; run(5600);
  WiFi.ap_present = true; wifimgr::reconnect(); run(15000);
  check(wifimgr::state() == wifimgr::CONNECTED, "aborts the attempt and connects with the new settings");
  auto connectedFirst = [] { for (int i = 0; i < 600 && wifimgr::state() != wifimgr::CONNECTED; i++) run(100); };
  auto drop = [] { WiFi.connected = false; WiFi.busy = false; WiFi.raw_disc(WIFI_REASON_BEACON_TIMEOUT); };
  puts("8. driver goes deaf (accepts begin(), then silence) - the board's log from 2 Oct");
  connectedFirst(); WiFi.deaf = true; drop();
  uint32_t t8 = sim_ms; run(200000);
  check(wifimgr::state() == wifimgr::CONNECTED && WiFi.sta_restarts > 0, "restarts the station and connects");
  printf("     connected %lu s after the driver went deaf\n", (unsigned long)((WiFi.connected_at - t8) / 1000));
  check(WiFi.connected_at - t8 < 150000, "recovers within 2.5 minutes");
  puts("9. router's security rejected (reason 210) until the minimum is dropped");
  connectedFirst(); WiFi.needs_any_security = true; drop(); run(30000);
  check(wifimgr::state() == wifimgr::CONNECTED, "retries with any security type and connects");
  puts("10. driver refuses begin() for 20 s");
  connectedFirst(); WiFi.refuse = true; drop(); run(20000);
  WiFi.refuse = false; uint32_t t10 = sim_ms; run(30000);
  check(wifimgr::state() == wifimgr::CONNECTED, "keeps retrying and connects");
  check(WiFi.connected_at - t10 < 12000, "no long watchdog wait after a refused begin()");
  puts("11. Wi-Fi menu asks for the network list while attempts to a missing router keep failing");
  connectedFirst(); WiFi.ap_present = false; drop(); run(9000);
  wifimgr::requestNetworks();
  wifimgr::Network nets[8];
  check(wifimgr::networks(nets, 8) == -1, "list not ready straight away");
  int refused11 = WiFi.scan_refused, waited = 0;
  while (wifimgr::networks(nets, 8) < 0 && waited < 60000) { run(100); waited += 100; }
  int n11 = wifimgr::networks(nets, 8);
  printf("     list ready after %.1f s: %d network(s)\n", waited / 1000.0, n11);
  check(n11 == 1 && !strcmp(nets[0].ssid, "Neighbour"), "list arrives (router off: only the neighbour)");
  check(WiFi.scan_refused == refused11, "the list scan never collides with an attempt");
  WiFi.ap_present = true; run(20000);
  check(wifimgr::state() == wifimgr::CONNECTED, "attempts carry on and connect afterwards");
  puts("12. no home network set: the list still works, and nothing tries to connect");
  sim_ssid = ""; wifimgr::reconnect(); run(3000);
  int begins12 = WiFi.begins;
  wifimgr::requestNetworks(); waited = 0;
  while (wifimgr::networks(nets, 8) < 0 && waited < 10000) { run(100); waited += 100; }
  int n12 = wifimgr::networks(nets, 8);
  check(n12 == 2 && !strcmp(nets[0].ssid, "Home") && nets[0].rssi > nets[1].rssi, "both networks, strongest first");
  printf("     list ready after %.1f s\n", waited / 1000.0);
  run(20000);
  check(WiFi.begins == begins12 && wifimgr::state() == wifimgr::NOT_CONFIGURED, "no connection attempts");
  puts("13. network chosen in the menu: connects");
  sim_ssid = "Home"; wifimgr::reconnect(); run(15000);
  check(wifimgr::state() == wifimgr::CONNECTED, "connects to the chosen network");
  puts("14. hidden network typed in the menu (never in a scan)");
  WiFi.hidden = true; drop(); uint32_t t14 = sim_ms; run(60000);
  check(wifimgr::state() == wifimgr::CONNECTED, "connects without seeing it in a scan");
  printf("     connected %lu s after the drop\n", (unsigned long)((WiFi.connected_at - t14) / 1000));
  WiFi.hidden = false;
  printf("begin() calls: %d (%d aimed at a scanned router), scans: %d, access point moved: %d time(s)\n",
         WiFi.begins, WiFi.begins_targeted, WiFi.scans, WiFi.ap_moves);
  check(WiFi.begins_targeted * 2 > WiFi.begins, "most attempts go straight to the scanned router");
  check(WiFi.ap_channel == 6 && WiFi.ap_moves == 1, "access point moved to the router's channel once");
  check(WiFi.scan_refused == 0, "never scans while the driver is connecting");
  printf("begin() calls: %d, rejected because the driver was busy: %d\n", WiFi.begins, WiFi.begin_rejected);
  check(WiFi.begin_rejected == 0, "begin() never collides with a running attempt");
  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails != 0;
}
