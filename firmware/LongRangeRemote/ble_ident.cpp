// ble_ident.cpp - see ble_ident.h. Message formats and their sources:
//   * AD structures, appearance, company IDs, service UUIDs: Bluetooth SIG assigned numbers (ble_db.h)
//   * Apple "continuity" messages (company 0x004C: type, length, data): Wireshark's btad_apple_continuity
//     dissector (epan/dissectors/packet-bluetooth.c). AirPods / Beats model numbers: Wireshark's list and
//     the newer models in Flipper Zero's ble_spam app (Next-Flip/Momentum-Apps, protocols/continuity.c).
//   * Microsoft (0x0006): [MS-CDP] 2.2.2.2.3 "Bluetooth: Advertising Beacon" (scenario 1, device type in
//     the low 5 bits of the next byte); Swift Pair (beacon ID 3, reserved RSSI byte 0x80).
//   * Google Find Hub network (Eddystone service 0xFEAA, frame type 0x40 / 0x41): Google's "Find Hub
//     Network Accessory Specification". Eddystone frame types 0x00/0x10/0x20/0x30: Eddystone protocol spec.
#include "ble_ident.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ble_db.h"

namespace bleid {

// ---------------------------------------------------------------------------------------------------
// Parsing
void addUuid(Adv &a, uint16_t u) {
  for (int i = 0; i < a.n_uuid16; i++) if (a.uuid16[i] == u) return;
  if (a.n_uuid16 < (int)(sizeof(a.uuid16) / sizeof(a.uuid16[0]))) a.uuid16[a.n_uuid16++] = u;
}

void parse(const uint8_t *p, int len, Adv &a) {
  memset(&a, 0, sizeof(a));
  int i = 0;
  while (i < len) {
    int l = p[i];
    if (l == 0) break;                       // end of significant data (padding)
    if (i + 1 + l > len) break;              // truncated
    uint8_t t = p[i + 1];
    const uint8_t *d = p + i + 2;
    int n = l - 1;
    switch (t) {
      case 0x01: if (n >= 1) { a.has_flags = true; a.flags = d[0]; } break;
      case 0x02: case 0x03:                  // incomplete / complete list of 16-bit service UUIDs
        for (int k = 0; k + 1 < n; k += 2) addUuid(a, d[k] | (d[k + 1] << 8));
        break;
      case 0x06: case 0x07: a.n_uuid128 += n / 16; break;
      case 0x08: case 0x09: {                // shortened / complete local name
        if (t == 0x08 && a.name[0]) break;   // a complete name wins
        int m = n < (int)sizeof(a.name) - 1 ? n : (int)sizeof(a.name) - 1;
        int o = 0;
        for (int k = 0; k < m; k++) {        // printable ASCII only (the screen font); UTF-8 -> '?'
          uint8_t c = d[k];
          if (c >= 0x20 && c < 0x7F) a.name[o++] = c;
          else if (c >= 0xC0) a.name[o++] = '?';   // lead byte of a multi-byte character
        }
        a.name[o] = 0;
        break;
      }
      case 0x0A: if (n >= 1) { a.has_tx = true; a.tx_power = (int8_t)d[0]; } break;
      case 0x16:                             // service data, 16-bit UUID
        if (n >= 2) {
          uint16_t u = d[0] | (d[1] << 8);
          addUuid(a, u);
          if (a.n_sdata < 3) {
            ServiceData &s = a.sdata[a.n_sdata++];
            s.uuid = u;
            s.len = n - 2 < (int)sizeof(s.data) ? n - 2 : sizeof(s.data);
            memcpy(s.data, d + 2, s.len);
          }
        }
        break;
      case 0x19: if (n >= 2) { a.has_app = true; a.appearance = d[0] | (d[1] << 8); } break;
      case 0xFF:                             // manufacturer specific data
        if (n >= 2 && !a.has_mfr) {
          a.has_mfr = true;
          a.company = d[0] | (d[1] << 8);
          a.mfr_len = n - 2 < (int)sizeof(a.mfr) ? n - 2 : sizeof(a.mfr);
          memcpy(a.mfr, d + 2, a.mfr_len);
        }
        break;
    }
    i += 1 + l;
  }
}

// ---------------------------------------------------------------------------------------------------
// Addresses
AddrKind addrKind(int addr_type, const uint8_t mac[6]) {
  if (addr_type == 0 || addr_type == 2) return ADDR_PUBLIC;
  switch (mac[0] >> 6) {
    case 3: return ADDR_STATIC;
    case 1: return ADDR_PRIVATE;
    default: return ADDR_NONRESOLVABLE;
  }
}

const char *addrKindText(AddrKind k) {
  switch (k) {
    case ADDR_PUBLIC: return "public (fixed)";
    case ADDR_STATIC: return "random, fixed until reset";
    case ADDR_PRIVATE: return "random, changes every few min";
    default: return "random, changes";
  }
}

// ---------------------------------------------------------------------------------------------------
// Lookups in ble_db.h (sorted tables: binary search)
template <typename T>
int find(const T *keys, int n, unsigned v) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (keys[m] == v) return m;
    if (keys[m] < v) lo = m + 1; else hi = m - 1;
  }
  return -1;
}

const char *companyName(uint16_t id) {
  int i = find(bledb::COMPANY_ID, bledb::N_COMPANY, id);
  return i < 0 ? nullptr : bledb::COMPANY_NAMES + bledb::COMPANY_OFS[i];
}

const char *uuid16Name(uint16_t u, bool *is_company) {
  int i = find(bledb::UUID16, bledb::N_UUID16, u);
  if (is_company) *is_company = i >= 0 && bledb::UUID16_KIND[i];
  return i < 0 ? nullptr : bledb::UUID16_NAMES + bledb::UUID16_OFS[i];
}

const char *appearanceName(uint16_t v) {
  int i = find(bledb::APPEARANCE, bledb::N_APPEARANCE, v);
  if (i < 0) i = find(bledb::APPEARANCE, bledb::N_APPEARANCE, v & ~0x3F);   // its category
  return i < 0 ? nullptr : bledb::APPEARANCE_NAMES + bledb::APPEARANCE_OFS[i];
}

void servicesText(const Adv &a, char *out, size_t n) {
  out[0] = 0;
  size_t o = 0;
  for (int i = 0; i < a.n_uuid16 && o + 1 < n; i++) {
    bool co;
    const char *nm = uuid16Name(a.uuid16[i], &co);
    char one[48];
    if (nm && co) snprintf(one, sizeof(one), "%s (%04X)", nm, a.uuid16[i]);
    else if (nm) snprintf(one, sizeof(one), "%s", nm);
    else snprintf(one, sizeof(one), "%04X", a.uuid16[i]);
    o += snprintf(out + o, n - o, "%s%s", o ? ", " : "", one);
  }
  if (a.n_uuid128 && o + 1 < n) snprintf(out + o, n - o, "%s%d custom", o ? ", " : "", a.n_uuid128);
}

// ---------------------------------------------------------------------------------------------------
// Identification
namespace {

struct Code { uint16_t v; const char *s; };

const char *lookup(const Code *t, uint16_t v) {
  for (; t->s; t++) if (t->v == v) return t->s;
  return nullptr;
}

// Apple proximity pairing (type 0x07): device model, big-endian after the 1-byte prefix
const Code APPLE_AUDIO[] = {
  {0x0220, "AirPods"}, {0x0F20, "AirPods 2nd Gen"}, {0x1320, "AirPods 3rd Gen"},
  {0x0E20, "AirPods Pro"}, {0x1420, "AirPods Pro 2nd Gen"}, {0x0A20, "AirPods Max"},
  {0x0320, "Powerbeats 3"}, {0x0B20, "Powerbeats Pro"}, {0x0520, "BeatsX"}, {0x1020, "Beats Flex"},
  {0x0620, "Beats Solo 3"}, {0x0C20, "Beats Solo Pro"}, {0x0920, "Beats Studio 3"},
  {0x1720, "Beats Studio Pro"}, {0x1120, "Beats Studio Buds"}, {0x1620, "Beats Studio Buds+"},
  {0x1220, "Beats Fit Pro"}, {0x0055, "AirTag"}, {0x0030, "AirTag (Hermes)"}, {0, nullptr}};

// Apple HomeKit (type 0x06): accessory category, big-endian
const Code HOMEKIT[] = {
  {0x0200, "HomeKit bridge"}, {0x0300, "HomeKit fan"}, {0x0400, "HomeKit garage door"},
  {0x0500, "HomeKit light"}, {0x0600, "HomeKit door lock"}, {0x0700, "HomeKit outlet"},
  {0x0800, "HomeKit switch"}, {0x0900, "HomeKit thermostat"}, {0x0A00, "HomeKit sensor"},
  {0x0B00, "HomeKit security system"}, {0x0C00, "HomeKit door"}, {0x0D00, "HomeKit window"},
  {0x0E00, "HomeKit blinds"}, {0x0F00, "HomeKit button"}, {0x1000, "HomeKit range extender"},
  {0x1100, "HomeKit camera"}, {0x1200, "HomeKit doorbell"}, {0x1300, "HomeKit air purifier"},
  {0x1400, "HomeKit heater"}, {0x1500, "HomeKit air conditioner"}, {0x1600, "HomeKit humidifier"},
  {0x1700, "HomeKit dehumidifier"}, {0x1C00, "HomeKit sprinklers"}, {0x1D00, "HomeKit tap"},
  {0x1E00, "HomeKit shower"}, {0, nullptr}};

// Apple Nearby Action (type 0x0F): action type
const Code NEARBY_ACTION[] = {
  {0x01, "Apple TV setup"}, {0x04, "Mobile backup"}, {0x05, "Watch setup"}, {0x06, "Apple TV pair"},
  {0x07, "Internet relay"}, {0x08, "Sharing Wi-Fi password"}, {0x09, "iOS setup"}, {0x0A, "Repair"},
  {0x0B, "Speaker setup"}, {0x0C, "Apple Pay"}, {0x0D, "Home audio setup"}, {0x0F, "Answered call"},
  {0x10, "Ended call"}, {0x13, "Remote AutoFill"}, {0x17, "Remote display"}, {0, nullptr}};

// Microsoft CDP beacon: device type (low 5 bits of the byte after the scenario)
struct MsDev { uint8_t v; const char *detail; const char *type; const char *brand; };
const MsDev MS_DEVICES[] = {
  {1, "Xbox One", "Game console", "Microsoft"},   {6, "iPhone (with Phone Link)", "Phone", "Apple"},
  {7, "iPad (with Phone Link)", "Tablet", "Apple"}, {8, "Android (with Phone Link)", "Phone", nullptr},
  {9, "Windows desktop", "Computer", nullptr},    {11, "Windows phone", "Phone", nullptr},
  {12, "Linux device", "Computer", nullptr},      {13, "Windows IoT", "Computer", nullptr},
  {14, "Surface Hub", "Computer", "Microsoft"},   {15, "Windows laptop", "Computer", nullptr},
  {16, "Windows tablet", "Tablet", nullptr},      {0, nullptr, nullptr, nullptr}};

// Bluetooth SIG services that say what a device does
const Code SERVICE_TYPES[] = {
  {0x180D, "Heart rate sensor"}, {0x1812, "Keyboard / mouse / pad"}, {0x1809, "Thermometer"},
  {0x1810, "Blood pressure monitor"}, {0x1808, "Glucose meter"}, {0x1816, "Cycling sensor"},
  {0x1818, "Cycling power meter"}, {0x1814, "Running sensor"}, {0x181A, "Environment sensor"},
  {0x181D, "Scale"}, {0x181B, "Body composition scale"}, {0x1822, "Pulse oximeter"},
  {0x1826, "Fitness machine"}, {0x1827, "Mesh device (unprovisioned)"}, {0x1828, "Mesh device"},
  {0x1844, "Audio device (LE Audio)"}, {0x184E, "Audio device (LE Audio)"}, {0x184F, "Audio device (LE Audio)"},
  {0x1850, "Audio device (LE Audio)"}, {0x1852, "Audio broadcast (Auracast)"}, {0x1853, "Audio device (LE Audio)"},
  {0x1854, "Hearing aid"}, {0xFDF0, "Hearing aid"}, {0xFD6F, "Phone (exposure notifications)"}, {0, nullptr}};

// Words in a device's own name. Matched case-insensitively, first match wins.
struct NameRule { const char *word; const char *type; const char *brand; };
const NameRule NAME_RULES[] = {
  {"airpods", "Headphones", "Apple"}, {"airtag", "Tracker", "Apple"}, {"iphone", "Phone", "Apple"},
  {"ipad", "Tablet", "Apple"}, {"macbook", "Computer", "Apple"}, {"imac", "Computer", "Apple"},
  {"apple watch", "Watch", "Apple"}, {"apple tv", "Media player", "Apple"}, {"homepod", "Speaker", "Apple"},
  {"beats", "Headphones", "Beats"}, {"[tv]", "TV", nullptr}, {"bravia", "TV", "Sony"},
  {"galaxy buds", "Headphones", "Samsung"}, {"galaxy watch", "Watch", "Samsung"}, {"galaxy tab", "Tablet", "Samsung"},
  {"galaxy", "Phone", "Samsung"}, {"smarttag", "Tracker", "Samsung"}, {"pixel buds", "Headphones", "Google"},
  {"pixel watch", "Watch", "Google"}, {"pixel", "Phone", "Google"}, {"wh-", "Headphones", "Sony"},
  {"wf-", "Headphones", "Sony"}, {"wi-", "Headphones", "Sony"}, {"srs-", "Speaker", "Sony"},
  {"bose", nullptr, "Bose"}, {"jbl", nullptr, "JBL"}, {"jabra", "Headphones", "Jabra"},
  {"sennheiser", "Headphones", "Sennheiser"}, {"momentum", "Headphones", "Sennheiser"},
  {"soundcore", nullptr, "Anker"}, {"skullcandy", "Headphones", "Skullcandy"}, {"marshall", nullptr, "Marshall"},
  {"sonos", "Speaker", "Sonos"}, {"buds", "Headphones", nullptr}, {"earbud", "Headphones", nullptr},
  {"headphone", "Headphones", nullptr}, {"headset", "Headphones", nullptr}, {"speaker", "Speaker", nullptr},
  {"soundbar", "Speaker", nullptr}, {"xbox", "Game controller", "Microsoft"}, {"dualsense", "Game controller", "Sony"},
  {"dualshock", "Game controller", "Sony"}, {"wireless controller", "Game controller", "Sony"},
  {"pro controller", "Game controller", "Nintendo"}, {"joy-con", "Game controller", "Nintendo"},
  {"controller", "Game controller", nullptr}, {"keyboard", "Keyboard", nullptr}, {"mouse", "Mouse", nullptr},
  {"mx master", "Mouse", "Logitech"}, {"mx keys", "Keyboard", "Logitech"}, {"logi", nullptr, "Logitech"},
  {"surface", nullptr, "Microsoft"}, {"desktop-", "Computer", nullptr}, {"laptop-", "Computer", nullptr},
  {"fitbit", "Fitness band", "Fitbit"}, {"garmin", nullptr, "Garmin"}, {"forerunner", "Watch", "Garmin"},
  {"fenix", "Watch", "Garmin"}, {"venu", "Watch", "Garmin"}, {"amazfit", "Watch", "Amazfit"},
  {"mi smart band", "Fitness band", "Xiaomi"}, {"redmi", nullptr, "Xiaomi"}, {"xiaomi", nullptr, "Xiaomi"},
  {"huawei", nullptr, "Huawei"}, {"oneplus", nullptr, "OnePlus"}, {"polar", nullptr, "Polar"},
  {"wahoo", nullptr, "Wahoo"}, {"withings", nullptr, "Withings"}, {"oura", "Ring", "Oura"},
  {"watch", "Watch", nullptr}, {"band", "Fitness band", nullptr}, {"tile", "Tracker", "Tile"},
  {"chipolo", "Tracker", "Chipolo"}, {"hue", "Light", "Philips Hue"}, {"govee", nullptr, "Govee"},
  {"bulb", "Light", nullptr}, {"lamp", "Light", nullptr}, {"tesla", "Car", "Tesla"},
  {"printer", "Printer", nullptr}, {"lg", nullptr, "LG"}, {"samsung", nullptr, "Samsung"},
  {"sony", nullptr, "Sony"}, {"philips", nullptr, "Philips"}, {"tv", "TV", nullptr},
  {nullptr, nullptr, nullptr}};

// Case-insensitive, at the start of a word ("Logi M240" yes, "Technologies" no); short words
// ("lg", "tv", "hue") must also end there.
bool hasWord(const char *name, const char *w) {
  size_t wl = strlen(w);
  for (const char *p = name; *p; p++) {
    if (p != name && isalnum((unsigned char)p[-1]) && isalnum((unsigned char)w[0])) continue;
    size_t k = 0;
    while (k < wl && p[k] && tolower((unsigned char)p[k]) == w[k]) k++;
    if (k < wl) continue;
    if (wl > 3 || !isalnum((unsigned char)p[wl])) return true;
  }
  return false;
}

void set(char *dst, size_t n, const char *s) {
  if (!s) return;
  snprintf(dst, n, "%s", s);
}

struct Builder {
  Ident &o;
  void brand(const char *b, const char *src) {
    if (!o.brand[0] && b && *b) { set(o.brand, sizeof(o.brand), b); o.brand_src = src; }
  }
  void type(const char *t, const char *src) {
    if (!o.type[0] && t && *t) { set(o.type, sizeof(o.type), t); o.type_src = src; }
  }
  void detail(const char *d) { if (!o.detail[0] && d) set(o.detail, sizeof(o.detail), d); }
};

// Apple continuity: one or more [type][length][data] messages after the company ID
void apple(const Adv &a, Builder &b) {
  b.brand("Apple", "maker data");
  int i = 0;
  while (i + 2 <= a.mfr_len) {
    uint8_t t = a.mfr[i], l = a.mfr[i + 1];
    const uint8_t *d = a.mfr + i + 2;
    int n = l;
    if (i + 2 + n > a.mfr_len) n = a.mfr_len - i - 2;   // cut short (our copy is limited)
    char s[48];
    switch (t) {
      case 0x02: b.type("Beacon", "Apple data"); b.detail("iBeacon"); break;
      case 0x03: b.type("Printer", "Apple data"); b.detail("AirPrint"); break;
      case 0x05: b.type("Phone or computer", "Apple data"); b.detail("AirDrop"); break;
      case 0x06:
        if (n >= 9) {
          const char *c = lookup(HOMEKIT, (d[7] << 8) | d[8]);
          b.type("Smart home", "Apple data");
          b.detail(c ? c : "HomeKit accessory");
        }
        break;
      case 0x07:
        if (n >= 3) {
          uint16_t m = (d[1] << 8) | d[2];
          const char *name = lookup(APPLE_AUDIO, m);
          if (m == 0x0055 || m == 0x0030) { b.type("Tracker", "Apple data"); b.o.tracker = true; }
          else b.type("Headphones", "Apple data");
          if (name) b.detail(name);
          else { snprintf(s, sizeof(s), "Apple audio, model %04X", m); b.detail(s); }
          if (name && !strncmp(name, "Beats", 5)) { b.o.brand[0] = 0; b.brand("Beats (Apple)", "maker data"); }
        }
        break;
      case 0x08: b.type("Phone or computer", "Apple data"); b.detail("Hey Siri"); break;
      case 0x09: b.type("Media player", "Apple data"); b.detail("AirPlay receiver (Apple TV, HomePod, Mac)"); break;
      case 0x0A: b.type("Phone or computer", "Apple data"); b.detail("AirPlay source"); break;
      case 0x0B: b.type("Watch", "Apple data"); b.detail("Apple Watch (Magic Switch)"); break;
      case 0x0C: b.type("Phone or computer", "Apple data"); b.detail("iPhone, iPad or Mac (Handoff)"); break;
      case 0x0D: b.type("Phone", "Apple data"); b.detail("Hotspot available"); break;
      case 0x0E: b.type("Phone or computer", "Apple data"); b.detail("Hotspot user"); break;
      case 0x0F:
        if (n >= 2) {
          const char *act = lookup(NEARBY_ACTION, d[1]);
          b.type("Phone or computer", "Apple data");
          if (act) { snprintf(s, sizeof(s), "Nearby action: %s", act); b.detail(s); }
        }
        break;
      case 0x10: b.type("Phone or computer", "Apple data"); b.detail("iPhone, iPad, Mac or Watch"); break;
      case 0x12:
        b.type("Tracker", "Apple data");
        b.o.tracker = true;
        b.detail(n >= 25 ? "Find My: away from its owner" : "Find My: near its owner");
        break;
    }
    i += 2 + l;
  }
}

void microsoft(const Adv &a, Builder &b) {
  b.brand("Microsoft", "maker data");
  if (a.mfr_len >= 2 && a.mfr[0] == 1) {                 // CDP beacon
    uint8_t dev = a.mfr[1] & 0x1F;
    for (const MsDev *m = MS_DEVICES; m->detail; m++)
      if (m->v == dev) {
        b.type(m->type, "Microsoft data");
        b.detail(m->detail);
        if (m->brand) { b.o.brand[0] = 0; b.brand(m->brand, "Microsoft data"); }
        return;
      }
    b.detail("Windows device (Connected Devices)");
  } else if (a.mfr_len >= 3 && a.mfr[0] == 3 && a.mfr[2] == 0x80) {
    b.type("Accessory", "Microsoft data");
    b.detail("Swift Pair: ready to pair with Windows");
    b.o.brand[0] = 0;                                    // the accessory's maker, not Microsoft
  }
}

void serviceData(const Adv &a, Builder &b) {
  char s[48];
  for (int i = 0; i < a.n_sdata; i++) {
    const ServiceData &sd = a.sdata[i];
    if (sd.uuid == 0xFEAA && sd.len >= 1) {              // Eddystone / Google Find Hub
      uint8_t f = sd.data[0];
      if (f == 0x40 || f == 0x41) {
        b.type("Tracker", "Google data");
        b.o.tracker = true;
        b.detail(f == 0x41 ? "Find Hub tag (separated, unwanted-tracking mode)" : "Find Hub tag (Google network)");
      } else if (f == 0x00 || f == 0x10 || f == 0x20 || f == 0x30) {
        b.type("Beacon", "Google data");
        b.detail(f == 0x00 ? "Eddystone UID beacon" : f == 0x10 ? "Eddystone URL beacon" :
                 f == 0x20 ? "Eddystone telemetry" : "Eddystone EID beacon");
      }
    } else if (sd.uuid == 0xFE2C) {                      // Google Fast Pair
      b.type("Accessory", "Google data");
      if (sd.len == 3) { snprintf(s, sizeof(s), "Fast Pair: pairing mode, model %02X%02X%02X", sd.data[0], sd.data[1], sd.data[2]); b.detail(s); }
      else b.detail("Fast Pair accessory");
    }
  }
}

}  // namespace

void identify(const Adv &a, AddrKind kind, const char *oui, Ident &out) {
  memset(&out, 0, sizeof(out));
  out.brand_src = out.type_src = "";
  Builder b{out};

  // 1. maker data: what Apple / Microsoft devices say about themselves; otherwise just the company
  if (a.has_mfr) {
    if (a.company == 0x004C) apple(a, b);
    else if (a.company == 0x0006) microsoft(a, b);
    else b.brand(companyName(a.company), "maker data");
  }
  // 2. service data (Google trackers, beacons, Fast Pair) and services assigned to companies
  serviceData(a, b);
  for (int i = 0; i < a.n_uuid16; i++) {
    uint16_t u = a.uuid16[i];
    bool co;
    const char *nm = uuid16Name(u, &co);
    if (u == 0xFEED || u == 0xFEEC) { b.type("Tracker", "services"); out.tracker = true; b.detail("Tile tracker"); }
    if (u == 0xFE33) { b.type("Tracker", "services"); out.tracker = true; b.detail("Chipolo tracker"); }
    if (co) b.brand(nm, "service");
  }
  // 3. appearance, then services that say what it does
  if (a.has_app && (a.appearance >> 6) != 0) {
    const char *nm = appearanceName(a.appearance);
    if ((a.appearance >> 6) == 8) out.tracker = true;     // "Tag"
    b.type(nm, "appearance");
  }
  for (int i = 0; i < a.n_uuid16; i++) b.type(lookup(SERVICE_TYPES, a.uuid16[i]), "services");
  // 4. its own name
  if (a.name[0])
    for (const NameRule *r = NAME_RULES; r->word; r++)
      if (hasWord(a.name, r->word)) {
        if (r->brand) b.brand(r->brand, "name");
        if (r->type) b.type(r->type, "name");
        if (r->brand && r->type) break;
      }
  // 5. the MAC prefix (public addresses only)
  if (kind == ADDR_PUBLIC && oui && *oui) b.brand(oui, "MAC prefix");
  if (!out.type[0]) { set(out.type, sizeof(out.type), "Unknown"); out.type_src = ""; }
}

}  // namespace bleid
