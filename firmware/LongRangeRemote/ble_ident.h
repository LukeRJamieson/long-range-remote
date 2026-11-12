// ble_ident.h - read a Bluetooth LE advertisement and work out what sent it.
//
// A BLE device says who it is only indirectly. What the scanner can use, best first:
//   * maker data      "manufacturer specific data" starts with the maker's company ID (Apple, Microsoft,
//                     Samsung, ...). Apple and Microsoft put a message type after it that says more:
//                     AirPods model, Find My tag, iBeacon, Windows PC, Xbox, ...
//   * services        16-bit service UUIDs: Bluetooth SIG ones say what it does (Heart Rate, keyboard /
//                     mouse, LE Audio, ...); the ones the SIG gives to companies name the company (Tile,
//                     Google, Sonos, ...). Google's Find Hub trackers and Eddystone beacons are told
//                     apart by their service data.
//   * appearance      an optional value the device chooses from a SIG list: Phone, Watch, Keyboard, ...
//   * name            whatever the device calls itself ("Pixel 8", "[TV] Samsung Q80", "WH-1000XM5")
//   * MAC prefix      only for a public address: the first three bytes are the maker's IEEE prefix
//                     (oui.bin on the SD card). Most phones, earbuds and watches use a random address
//                     that changes every few minutes instead, so the same phone can show up twice.
// Every brand and type the scanner shows says which of these it came from.
//
// No Arduino dependencies: tools/ble_test runs it on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace bleid {

// ---- a parsed advertisement (advertising data + scan response) ----
struct ServiceData { uint16_t uuid; uint8_t len; uint8_t data[26]; };
struct Adv {
  char name[30];           // complete or shortened local name, "" if none
  bool has_flags; uint8_t flags;
  bool has_tx; int8_t tx_power;          // dBm the device says it sends at
  bool has_app; uint16_t appearance;
  int n_uuid16; uint16_t uuid16[10];     // service UUIDs listed (16-bit), and those with service data
  int n_uuid128;                         // 128-bit service UUIDs (vendor specific, not looked up)
  bool has_mfr; uint16_t company; uint8_t mfr_len; uint8_t mfr[27];   // data after the company ID
  int n_sdata; ServiceData sdata[3];
};
// Parse AD structures. Unknown types are skipped; a truncated structure ends the parse.
void parse(const uint8_t *p, int len, Adv &a);

// ---- addresses ----
enum AddrKind : uint8_t {
  ADDR_PUBLIC,             // fixed, from the maker's IEEE range: the prefix names the maker
  ADDR_STATIC,             // random, but fixed until the device is reset
  ADDR_PRIVATE,            // random, changes every few minutes (phones, earbuds, watches)
  ADDR_NONRESOLVABLE,      // random, changes (beacons and the like)
};
// addr_type: 0 public, 1 random (as the controller reports it; 2 / 3 = resolved, treated the same).
// mac[0] is the first byte as written ("AA" in AA:BB:...); the top two bits give the random kind.
AddrKind addrKind(int addr_type, const uint8_t mac[6]);
const char *addrKindText(AddrKind k);     // "public", "random static", "random (changes)", ...

// ---- what it is ----
struct Ident {
  char brand[32];      const char *brand_src;   // "maker data", "service", "name", "MAC prefix"; "" = none
  char type[32];       const char *type_src;    // "Apple data", "appearance", "services", "name", ...
  char detail[48];     // the most specific thing known: "AirPods Pro 2nd Gen", "iBeacon", ...
  bool tracker;        // AirTag / Find My, Tile, Chipolo, Google Find Hub tag
};
// oui_vendor: the MAC prefix's maker for a public address (nullptr or "" if not known)
void identify(const Adv &a, AddrKind kind, const char *oui_vendor, Ident &out);

// ---- lookups (also used for the details screen) ----
const char *companyName(uint16_t id);                 // nullptr if not in the list
const char *uuid16Name(uint16_t uuid, bool *is_company);
const char *appearanceName(uint16_t value);           // exact value, else its category; nullptr if none
// "Heart Rate, Battery, Tile" - the 16-bit services, names where known
void servicesText(const Adv &a, char *out, size_t n);

}  // namespace bleid
