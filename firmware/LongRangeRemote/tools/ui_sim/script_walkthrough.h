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
  seq("DDD"); shot("menu_remove"); seq("D"); shot("menu_led"); press('S'); seq("D"); press('R'); shot("menu_sound");
  seq("D"); press('S'); shot("wifi_unset"); press('S'); shot("wifinets"); press('S'); seq("SRSRSRSRSRSRSRS"); shot("keyboard");
  seq("DDDR"); shot("keyboard_done"); press('S'); shot("wifi_set"); CHECK(settings::wifiSsid() == "Chateau Le Chat");
  sim.wifi_connected = true; ui::setNetworkInfo("192.168.4.1", "192.168.1.57", "Chateau Le Chat");
  seq("DD"); shot("wifi_forget"); press('B');
  seq("D"); press('S'); shot("status"); press('B'); shot("menu_after_status");
  // code search
  seq("UUUUU"); press('S'); shot("scansrc"); press('S'); shot("scancat"); seq("D"); press('S'); shot("scankey"); press('S'); shot("scan");
  seq("RRR"); shot("scan4"); press('B'); shot("scankey_back"); press('B'); press('B'); press('B');
  // second device via browse, then menu shows two devices
  seq("UU"); // somewhere in menu
  shot("menu_x");
  press('B'); shot("remote_again");
  // all keys
  seq("UL"); shot("remote_last_tile");
  // states
  sim.pack_v = 4.5f; sim.pack_pct = 9; sim.standby = true; ui::requestRedraw(); shot("low_standby");
  sim.pack_v = 0.2f; ui::requestRedraw(); shot("no_cells");
  sim.pack_v = 5.0f; sim.pack_pct = 78; sim.standby = false; ui::setAsleep(true); shot("asleep"); ui::setAsleep(false);
  printf(fails ? "SOME CHECKS FAILED\n" : "checks ok\n");
  return 0;
}
