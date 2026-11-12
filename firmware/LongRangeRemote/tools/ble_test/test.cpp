// Host test of the Bluetooth scanner's advertisement parser / identification (ble_ident.cpp) and its
// device list (ble_scan.cpp). Advertisements are built byte by byte from the formats in ble_ident.cpp.
//   g++ -std=c++17 -I../.. test.cpp ../../ble_ident.cpp ../../ble_scan.cpp -o ble_test && ./ble_test
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "ble_ident.h"
#include "ble_scan.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef std::vector<uint8_t> Bytes;
// one AD structure
static Bytes ad(uint8_t type, Bytes data) { Bytes b{(uint8_t)(data.size() + 1), type}; b.insert(b.end(), data.begin(), data.end()); return b; }
static Bytes cat(std::initializer_list<Bytes> l) { Bytes o; for (auto &b : l) o.insert(o.end(), b.begin(), b.end()); return o; }
static Bytes str(const char *s) { return Bytes(s, s + strlen(s)); }
static const Bytes FLAGS = ad(0x01, {0x06});

struct Result { bleid::Adv a; bleid::Ident id; };
static Result run(const char *what, const Bytes &p, int addr_type = 1, const uint8_t *mac = nullptr, const char *oui = nullptr) {
  static const uint8_t priv[6] = {0x5A, 1, 2, 3, 4, 5};   // 01xxxxxx: resolvable private
  Result r;
  bleid::parse(p.data(), (int)p.size(), r.a);
  bleid::identify(r.a, bleid::addrKind(addr_type, mac ? mac : priv), oui, r.id);
  char svc[120]; bleid::servicesText(r.a, svc, sizeof(svc));
  printf("%-28s brand=%-22s (%s) type=%-24s (%s) detail=%s%s  svc=[%s]\n", what, r.id.brand, r.id.brand_src, r.id.type,
         r.id.type_src, r.id.detail, r.id.tracker ? " TRACKER" : "", svc);
  return r;
}

int main() {
  // AirPods Pro 2: Apple 0x004C, proximity pairing 0x07, length 25, prefix 0x01, model 14 20
  {
    Bytes m = {0x4C, 0x00, 0x07, 25, 0x01, 0x14, 0x20, 0x55};
    m.resize(4 + 2 + 25, 0);
    auto r = run("AirPods Pro 2", cat({FLAGS, ad(0xFF, m)}));
    CHECK(!strcmp(r.id.brand, "Apple")); CHECK(!strcmp(r.id.type, "Headphones"));
    CHECK(!strcmp(r.id.detail, "AirPods Pro 2nd Gen"));
  }
  // Beats: brand becomes Beats
  { Bytes m = {0x4C, 0x00, 0x07, 25, 0x01, 0x12, 0x20}; m.resize(31, 0);
    auto r = run("Beats Fit Pro", ad(0xFF, m)); CHECK(!strcmp(r.id.brand, "Beats (Apple)")); CHECK(!strcmp(r.id.detail, "Beats Fit Pro")); }
  // iBeacon: 0x02 0x15 + 16-byte UUID + major/minor + power
  { Bytes m = {0x4C, 0x00, 0x02, 0x15}; m.resize(4 + 21, 0x11);
    auto r = run("iBeacon", cat({FLAGS, ad(0xFF, m)})); CHECK(!strcmp(r.id.type, "Beacon")); CHECK(!strcmp(r.id.detail, "iBeacon")); }
  // AirTag separated from owner: Find My 0x12, length 25
  { Bytes m = {0x4C, 0x00, 0x12, 25}; m.resize(4 + 25, 0x22);
    auto r = run("AirTag (Find My)", ad(0xFF, m)); CHECK(r.id.tracker); CHECK(!strcmp(r.id.type, "Tracker"));
    CHECK(strstr(r.id.detail, "away")); }
  // iPhone: Nearby Info 0x10 + Handoff 0x0C
  { Bytes m = {0x4C, 0x00, 0x10, 0x05, 0x01, 0x18, 0x44, 0x55, 0x66, 0x0C, 0x0E}; m.resize(4 + 7 + 2 + 14, 0);
    auto r = run("iPhone", ad(0xFF, m)); CHECK(!strcmp(r.id.type, "Phone or computer")); CHECK(!strcmp(r.id.brand, "Apple")); }
  // HomeKit light: status, 6-byte id, category 0x0005 big-endian (05 00), ...
  { Bytes m = {0x4C, 0x00, 0x06, 13, 0x31, 1, 2, 3, 4, 5, 6, 0x05, 0x00, 0x10, 0x00, 0x02, 0x02};
    auto r = run("HomeKit light", ad(0xFF, m)); CHECK(!strcmp(r.id.detail, "HomeKit light")); CHECK(!strcmp(r.id.type, "Smart home")); }
  // Windows laptop: Microsoft 0x0006, scenario 1, version 001 + device type 15
  { Bytes m = {0x06, 0x00, 0x01, (uint8_t)(0x20 | 15), 0x20, 0x00}; m.resize(2 + 27, 0x33);
    auto r = run("Windows laptop", ad(0xFF, m)); CHECK(!strcmp(r.id.type, "Computer")); CHECK(!strcmp(r.id.detail, "Windows laptop")); }
  // Swift Pair keyboard: 06 00 03 00 80 + name
  { auto r = run("Swift Pair keyboard", cat({FLAGS, ad(0xFF, {0x06, 0x00, 0x03, 0x00, 0x80}), ad(0x09, str("MX Keys Mini"))}));
    CHECK(!strcmp(r.id.brand, "Logitech")); CHECK(!strcmp(r.id.brand_src, "name")); CHECK(!strcmp(r.id.type, "Accessory")); }
  // Google Find Hub tag: service data FEAA, frame 0x40, 20-byte EID
  { Bytes sd = {0xAA, 0xFE, 0x40}; sd.resize(3 + 20, 0x44);
    auto r = run("Find Hub tag", cat({FLAGS, ad(0x16, sd)})); CHECK(r.id.tracker); CHECK(r.id.brand[0] == 0); }   // Chipolo, Pebblebee, ... make them: no brand claimed
  // Eddystone URL
  { auto r = run("Eddystone URL", cat({FLAGS, ad(0x03, {0xAA, 0xFE}), ad(0x16, {0xAA, 0xFE, 0x10, 0xF4, 0x03, 'a', 'b', 'c'})}));
    CHECK(!strcmp(r.id.type, "Beacon")); }
  // Tile: service FEED
  { auto r = run("Tile", cat({FLAGS, ad(0x03, {0xED, 0xFE}), ad(0x16, {0xED, 0xFE, 1, 2, 3})}));
    CHECK(r.id.tracker); CHECK(!strcmp(r.id.brand, "Tile")); CHECK(!strcmp(r.id.brand_src, "service")); }
  // Heart rate strap with a name and appearance 0x0341 (Heart Rate Sensor: Heart Rate Belt)
  { auto r = run("HR strap", cat({FLAGS, ad(0x03, {0x0D, 0x18, 0x0F, 0x18}), ad(0x19, {0x41, 0x03}), ad(0x09, str("Polar H10 ABC"))}));
    CHECK(!strcmp(r.id.brand, "Polar")); CHECK(!strcmp(r.id.type_src, "appearance")); }
  // Samsung TV (by name) with Samsung maker data
  { auto r = run("Samsung TV", cat({FLAGS, ad(0xFF, {0x75, 0x00, 0x42, 0x04}), ad(0x09, str("[TV] Samsung Q80 Series"))}));
    CHECK(!strcmp(r.id.brand, "Samsung Electronics")); CHECK(!strcmp(r.id.type, "TV")); }
  // Public address, nothing else: brand from the MAC prefix
  { static const uint8_t mac[6] = {0xF0, 0xD1, 0xA9, 1, 2, 3};
    auto r = run("public, OUI only", FLAGS, 0, mac, "Apple");
    CHECK(!strcmp(r.id.brand, "Apple")); CHECK(!strcmp(r.id.brand_src, "MAC prefix")); CHECK(!strcmp(r.id.type, "Unknown")); }
  // Name rules don't fire inside words
  { auto r = run("no false match", ad(0x09, str("Acme Technologies Hub")));
    CHECK(r.id.brand[0] == 0); }
  // Truncated / garbage data must not crash or read past the end
  { Bytes g = {0x1F, 0xFF, 0x4C, 0x00, 0x07, 0xFF}; run("truncated", g);
    Bytes z(31, 0); run("all zero", z);
    Bytes big = {0x02, 0x01, 0x06, 0x1A, 0xFF, 0x4C, 0x00, 0x12, 0x40}; big.resize(31, 0xAB); run("bad Apple length", big); }
  // Address kinds
  { uint8_t a[6] = {0xC4, 0, 0, 0, 0, 1}, b[6] = {0x4A, 0, 0, 0, 0, 1}, c[6] = {0x0A, 0, 0, 0, 0, 1};
    CHECK(bleid::addrKind(1, a) == bleid::ADDR_STATIC); CHECK(bleid::addrKind(1, b) == bleid::ADDR_PRIVATE);
    CHECK(bleid::addrKind(1, c) == bleid::ADDR_NONRESOLVABLE); CHECK(bleid::addrKind(0, c) == bleid::ADDR_PUBLIC); }
  // Lookups
  CHECK(!strcmp(bleid::companyName(0x004C), "Apple"));
  CHECK(bleid::appearanceName(0x00C2) && !strcmp(bleid::appearanceName(0x00C2), "Smartwatch"));

  // ---- device list ----
  blescan::clear();
  uint8_t m1[6] = {0x5A, 1, 1, 1, 1, 1}, m2[6] = {0xC0, 2, 2, 2, 2, 2};
  Bytes p1 = cat({FLAGS, ad(0x09, str("Pixel 8"))}), p2 = cat({FLAGS, ad(0x19, {0xC2, 0x00})});
  blescan::feed(m1, 1, -70, p1.data(), (int)p1.size(), 1000);
  blescan::feed(m2, 1, -50, p2.data(), (int)p2.size(), 1100);
  blescan::feed(m1, 1, -60, FLAGS.data(), (int)FLAGS.size(), 2000);   // no name this time: keeps "Pixel 8"
  CHECK(blescan::count() == 2);
  int idx[8]; int n = blescan::sorted(idx, 8, blescan::SORT_SIGNAL, 2500);
  CHECK(n == 2 && !strcmp(blescan::device(idx[0]).id.type, "Smartwatch"));
  const blescan::Device &d1 = blescan::device(idx[1]);
  CHECK(!strcmp(d1.adv.name, "Pixel 8")); CHECK(d1.count == 2); CHECK(d1.rssi_max == -60);
  char lab[48]; blescan::label(d1, lab, sizeof(lab)); CHECK(!strcmp(lab, "Pixel 8"));
  n = blescan::sorted(idx, 8, blescan::SORT_NAME, 2500); CHECK(!strcmp(blescan::device(idx[0]).adv.name, "Pixel 8"));
  // the list fills up: the longest-unseen device makes room
  for (int i = 0; i < blescan::MAX_DEVICES + 5; i++) { uint8_t m[6] = {0xC1, 0, 0, 0, (uint8_t)(i >> 8), (uint8_t)i}; blescan::feed(m, 1, -80, FLAGS.data(), 3, 3000 + i); }
  CHECK(blescan::count() == blescan::MAX_DEVICES);
  blescan::clear(); CHECK(blescan::count() == 0);

  printf(fails ? "SOME CHECKS FAILED (%d)\n" : "checks ok\n", fails);
  return fails ? 1 : 0;
}
