// Bluetooth scanner screens: a list of typical devices, sorting, the details screen.
#include <vector>
#include <string>
#include <string.h>
#include "ble_scan.h"
#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int fails = 0;

struct Fake { uint8_t mac[6]; int at; int rssi; std::vector<uint8_t> p; };
static std::vector<uint8_t> AD(uint8_t t, std::vector<uint8_t> d) { std::vector<uint8_t> b{(uint8_t)(d.size() + 1), t}; b.insert(b.end(), d.begin(), d.end()); return b; }
static std::vector<uint8_t> NAME(const char *s) { return AD(0x09, std::vector<uint8_t>(s, s + strlen(s))); }
static std::vector<uint8_t> J(std::initializer_list<std::vector<uint8_t>> l) { std::vector<uint8_t> o; for (auto &b : l) o.insert(o.end(), b.begin(), b.end()); return o; }

static void hearAll(std::vector<Fake> &f) {
  for (auto &d : f) blescan::feed(d.mac, d.at, d.rssi, d.p.data(), (int)d.p.size(), sim_ms);
}

static bool fakeOui(const uint8_t m[6], char *o, size_t n) {   // a slice of oui.bin
  if (m[0] == 0xF0 && m[1] == 0xD1 && m[2] == 0xA9) { snprintf(o, n, "Apple"); return true; }
  return false;
}

int main() {
  ui::setNetworkInfo("192.168.4.1", "192.168.1.57", "Home");
  ui::begin();
  blescan::setOuiLookup(fakeOui);
  std::vector<uint8_t> airpods = {0x4C, 0x00, 0x07, 25, 0x01, 0x14, 0x20, 0x55}; airpods.resize(31, 0);
  std::vector<uint8_t> findmy = {0x4C, 0x00, 0x12, 25}; findmy.resize(29, 0x22);
  std::vector<uint8_t> iphone = {0x4C, 0x00, 0x10, 0x05, 0x01, 0x18, 0x44, 0x55, 0x66};
  std::vector<uint8_t> laptop = {0x06, 0x00, 0x01, 0x2F, 0x20, 0x00}; laptop.resize(29, 0x33);
  std::vector<Fake> f = {
    {{0x5A, 0x11, 0x22, 0x33, 0x44, 0x55}, 1, -48, J({AD(0x01, {0x1A}), AD(0xFF, airpods)})},
    {{0x7B, 0x01, 0x02, 0x03, 0x04, 0x05}, 1, -63, J({AD(0x01, {0x1A}), AD(0xFF, iphone)})},
    {{0xF0, 0xD1, 0xA9, 0x10, 0x20, 0x30}, 0, -71, J({NAME("[TV] Samsung Q80 Series"), AD(0xFF, {0x75, 0x00, 0x42, 0x04})})},
    {{0xD2, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE}, 1, -82, J({AD(0xFF, findmy)})},
    {{0x6C, 0x10, 0x20, 0x30, 0x40, 0x50}, 1, -58, J({AD(0x01, {0x06}), AD(0xFF, laptop)})},
    {{0xC4, 0x7C, 0x8D, 0x01, 0x02, 0x03}, 1, -66, J({AD(0x01, {0x06}), AD(0x03, {0x0D, 0x18}), AD(0x19, {0x41, 0x03}), NAME("Polar H10 7A3B")})},
    {{0xE1, 0x00, 0x00, 0x00, 0x12, 0x34}, 1, -90, J({AD(0x01, {0x06}), AD(0x03, {0xED, 0xFE}), AD(0x16, {0xED, 0xFE, 1, 2})})},
    {{0x48, 0x00, 0x00, 0x00, 0x00, 0x01}, 1, -77, J({AD(0x01, {0x1A})})},
  };

  // to the menu, up twice from the top (it wraps): Status, then Bluetooth scanner
  press('B'); seq("UU"); shot("ble_menu");
  press('S'); shot("ble_empty");
  CHECK(blescan::count() == 0);
  for (int k = 0; k < 4; k++) { hearAll(f); run(400); }
  run(2200); shot("ble_list");
  CHECK(blescan::count() == (int)f.size());
  press('S', 900); shot("ble_sort_name");                 // hold OK: sort by name
  press('S', 900); press('S', 900);                       // back to signal
  press('S'); shot("ble_detail_airpods");
  press('D'); press('D'); shot("ble_detail_scrolled");
  // signal order: AirPods -48, laptop -58, iPhone -63, Polar -66, TV -71, unknown -77, Find My -82, Tile -90
  press('B'); seq("DDDD"); press('S'); shot("ble_detail_tv");
  CHECK(blescan::device(blescan::find(f[2].mac)).id.type == std::string("TV"));
  press('B'); seq("DD"); press('S'); shot("ble_detail_findmy");
  CHECK(blescan::device(blescan::find(f[3].mac)).id.tracker);
  press('B'); press('B'); shot("ble_back_to_menu");
  printf(fails ? "SOME CHECKS FAILED\n" : "checks ok\n");
  return fails ? 1 : 0;
}
