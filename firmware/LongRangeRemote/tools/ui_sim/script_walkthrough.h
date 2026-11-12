#include <assert.h>
#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int fails = 0;
int main() {
  ui::setNetworkInfo("192.168.4.1", "", "");
  ui::begin(); shot("nodevice");
  press('S'); shot("menu_first");                       // OK with no device -> menu
  press('S'); shot("letters");                          // Add a device: pick the brand
  seq("DDR"); press('S'); shot("brands");
  press('S'); shot("profiles");
  press('S'); shot("remote");                           // first profile (a TV) -> remote
  CHECK(settings::active() >= 0);
  int s0 = sim.sends; seq("RRRS"); CHECK(sim.sends == s0 + 1); shot("remote_sent");
  run(700); shot("remote_idle");
  seq("DDDDDD"); shot("remote_scrolled");
  seq("UUUUUU" "LLL"); press('S'); sim.keys = 1u << KEYBIT_UP; run(200); shot("volpad"); sim.keys = 0; run(100);
  press('B'); shot("remote_back_from_pad");
  seq("RS"); shot("navpad"); press('B', 900);
  seq("RS"); seq("UUR" "U"); shot("number"); press('B');
  // all keys: last tile
  press('L'); press('L'); press('L'); press('L'); 
  press('B'); shot("menu");
  seq("DDDD"); shot("menu_remove"); seq("D"); shot("menu_led"); press('S'); seq("D"); press('R'); shot("menu_sound");
  seq("D"); press('R'); shot("menu_auto"); CHECK(settings::scanAutoMs() == 1500); press('L'); CHECK(settings::scanAutoMs() == 1000);
  seq("D"); press('S'); shot("wifi_unset"); press('S'); shot("wifinets"); press('S'); seq("SRSRSRSRSRSRSRS"); shot("keyboard");
  seq("DDDR"); shot("keyboard_done"); press('S'); shot("wifi_set"); CHECK(settings::wifiSsid() == "Chateau Le Chat");
  sim.wifi_connected = true; ui::setNetworkInfo("192.168.4.1", "192.168.1.57", "Chateau Le Chat");
  seq("DD"); shot("wifi_forget"); press('B');
  seq("D"); press('S'); shot("status"); ui::noteWebRequest("192.168.1.42"); ui::loop(); shot("status_web"); press('B'); shot("menu_after_status");
  // code search
  seq("UUUUUUU"); press('S'); shot("scansrc"); press('S'); shot("scancat"); seq("D"); press('S'); shot("scankey"); press('S'); shot("scan");
  seq("RRR"); shot("scan4");
  // Auto: DOWN starts it (a code every 1 s), UP changes the speed, DOWN stops it, BACK then leaves
  { int s0 = sim.sends; press('D'); run(3050); int n1 = sim.sends - s0; shot("scan_auto");
    if (n1 != 3) { printf("FAIL auto at 1 s: %d codes in 3 s (want 3)\n", n1); fails++; }
    press('U'); int s1 = sim.sends; run(3000); int n2 = sim.sends - s1;
    if (n2 != 2) { printf("FAIL auto at 1.5 s: %d codes in 3 s (want 2)\n", n2); fails++; }
    press('D'); int s2 = sim.sends; run(3000); shot("scan_auto_stopped");
    if (sim.sends != s2) { printf("FAIL auto still sending after DOWN\n"); fails++; }
    press('U'); shot("scan_speed"); press('B'); press('B');   // UP again: 2 s
    if (settings::scanAutoMs() != 2000) { printf("FAIL speed %d (want 2000)\n", settings::scanAutoMs()); fails++; }
    press('D'); run(2500); press('B'); shot("scan_back_stops");  // BACK stops Auto, stays on the screen
    int s3 = sim.sends; run(4000); if (sim.sends != s3) { printf("FAIL BACK didn't stop Auto\n"); fails++; } }
  press('B'); shot("scankey_back"); press('B'); press('B'); press('B');
  // second device via browse, then menu shows two devices
  seq("UU"); // somewhere in menu
  shot("menu_x");
  press('B'); shot("remote_again");
  // all keys
  seq("UL"); shot("remote_last_tile");
  // states
  sim.pack_v = 4.5f; sim.pack_pct = 9; ui::requestRedraw(); shot("low_battery");
  sim.pack_v = 0.2f; ui::requestRedraw(); shot("no_cells");
  sim.pack_v = 5.0f; sim.pack_pct = 78; ui::requestRedraw();
  printf(fails ? "SOME CHECKS FAILED\n" : "checks ok\n");
  return 0;
}
