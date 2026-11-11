#include <Arduino.h>
#include <vector>
#include "config.h"
#include "hw.h"
#include "ir_db.h"
#include "ir_keys.h"
#include "ir_tx.h"
#include "sd_store.h"
#include "settings.h"
#include "sound.h"
#include "wifi_file.h"
#include "wifi_mgr.h"
#include "power.h"
#include "log.h"
#include "sim.h"
uint32_t sim_ms = 1000;
Sim sim;
void logf(const char *, ...) {} void logln(const char *) {} void logBegin() {}
const char *irProtoName(uint8_t p) { static const char *n[] = {"NEC", "Sony12", "RC5", "Kaseikyo", "Pioneer", "raw"}; return n[p % 6]; }
namespace hw {
uint8_t keys() { return sim.keys; } uint8_t stuckKeys() { return 0; }
float packVolts() { return sim.pack_v; } int packPercent() { return sim.pack_v < 1 ? -1 : sim.pack_pct; } bool packOn() { return sim.pack_v > 1; }
float screenVolts() { return 3.9f; } int screenPercent() { return sim.scr_pct; }
}
// ---- fake database: brands x 2 profiles, each profile has a fixed key set
static const char *BR[] = {"Aiwa", "Akai", "Bose", "Denon", "Hisense", "JVC", "Kogan", "LG", "Marantz", "NAD", "Onkyo", "Panasonic", "Philips", "Pioneer", "Rotel", "Samsung", "Sharp", "Sherwood", "Sony", "TCL", "Technics", "Toshiba", "Vizio", "Yamaha", "3M"};
static const int NB = sizeof(BR) / sizeof(BR[0]);
static std::vector<uint8_t> keysOf(int p) {
  std::vector<uint8_t> v;
  if (p % 2 == 0) for (int k = 1; k < KEY__COUNT; k++) { if (k % 3 != 2 || k < 30 || k >= KEY_D0) v.push_back(k); }
  else for (uint8_t k : {KEY_POWER, KEY_VOL_UP, KEY_VOL_DOWN, KEY_MUTE, KEY_IN_CD, KEY_IN_TUNER, KEY_IN_PHONO, KEY_IN_AUX}) v.push_back(k);
  return v;
}
namespace irdb {
const char *sourceName() { return "SD card (irdb.bin)"; }
int numUserProfiles() { return 2; }
int numBrands() { return sim.empty_db ? 0 : NB; } int numProfiles() { return sim.empty_db ? 0 : NB * 2; } int numCodes() { return 233188; }
const char *brandName(int b) { return BR[b]; }
int brandFirstProfile(int b) { return b * 2; } int brandProfileCount(int) { return 2; }
const char *profileName(int p) { static char b[40]; snprintf(b, sizeof(b), p % 2 ? "RX-V%d receiver" : "TV model %d series", 400 + p * 7); return b; }
int profileBrand(int p) { return p / 2; } int profileCategory(int p) { return p % 2 ? 1 : 0; }
int profileFirstCode(int p) { return p * 100; } int profileCodeCount(int p) { return keysOf(p).size(); }
int profileFindKey(int p, uint8_t key) { auto v = keysOf(p); for (size_t i = 0; i < v.size(); i++) if (v[i] == key) return p * 100 + i; return -1; }
const IrCode &code(int c) { static IrCode x; x = IrCode{0, 4, 0, (uint16_t)(c % 100), (uint8_t)(c / 100 % 5), 0}; return x; }
const char *codeName(int c) { return IR_KEY_LABELS[keysOf(c / 100)[c % 100]]; }
int buildScan(const ScanFilter &, uint8_t key, ScanCand *out, int max_out) { int n = 0; for (int p = 0; p < NB * 2 && n < max_out; p++) { int c = profileFindKey(p, key); if (c >= 0) out[n++] = ScanCand{c, p, (uint16_t)(p % 4 == 0 ? 12 : 1)}; } return n; }
int countProfiles(const ScanFilter &f) { return f.category < 0 ? NB * 2 : f.category < 2 ? NB : 0; }
int numGroups() { return 4; }
const char *groupName(int g) { static const char *n[] = {"Most popular", "Japanese", "Korean", "European"}; return n[g]; }
int groupBrandCount(int g) { return 8 + g * 3; }
}
namespace irtx {
static LastSent L;
void fire(int c) { if (c < 0) return; snprintf(L.label, sizeof(L.label), "%s - %s", BR[c / 200], irdb::codeName(c)); L.proto = c / 100 % 5; L.d = 4; L.s = 0; L.f = c % 100; L.frames = 1; L.seq++; L.at_ms = sim_ms; sim.sends++; }
void tap(int c) { fire(c); } void holdStart(int c) { fire(c); sim.holding = true; } void holdKeepAlive() { if (sim.holding) L.frames++; } void holdStop() { sim.holding = false; }
LastSent last() { return L; } bool isSending() { return sim.holding; }
}
namespace sdstore { const char *status() { return "card ok, 14.8 GB free, irdb.bin 5.6 MB, 2 .ir files"; } }
namespace settings {
static std::vector<int> favs; static int act = -1, ledp = 0, vol = 6; static bool muted = false; static String ss, pw;
int favCount() { return favs.size(); } int fav(int i) { return favs[i]; }
void removeFav(int p) { favs.erase(std::remove(favs.begin(), favs.end(), p), favs.end()); act = favs.empty() ? -1 : favs[0]; }
int active() { return act; }
void setActive(int p) { act = p; if (std::find(favs.begin(), favs.end(), p) == favs.end()) favs.push_back(p); }
int ledPower() { return ledp; } void setLedPower(int l) { ledp = l; }
int soundVolume() { return vol; } void setSoundVolume(int l) { vol = l; } bool soundMuted() { return muted; } void setSoundMuted(bool m) { muted = m; }
String wifiSsid() { return ss; } String wifiPass() { return pw; } const char *wifiSource() { return "wifi.txt on the SD card"; }
bool setWifi(const String &s, const String &p, char *msg, size_t n) { ss = s; pw = p; snprintf(msg, n, "Saved to wifi.txt"); return true; }
}
namespace sound { void play(Cue) { sim.clicks++; } bool ready() { return true; } const char *status() { return "ok"; } }
namespace wififile { const char *check(const std::string &, const std::string &p) { return p.size() && p.size() < 8 ? "the password must be 8 to 63 characters" : nullptr; } }
namespace wifimgr {
State state() { return sim.wifi_connected ? CONNECTED : settings::ss.empty() ? NOT_CONFIGURED : CONNECTING; }
const char *statusText() { return sim.wifi_connected ? "connected" : "connecting... (last failure: AUTH_EXPIRE)"; }
void reconnect() {} void requestNetworks() {}
int networks(Network *out, int max) { static const Network N[] = {{"Chateau Le Chat", -48, false}, {"NETGEAR-2G", -66, false}, {"Telstra1F3A", -74, false}, {"FreeWifi", -83, true}}; int n = std::min(max, 4); for (int i = 0; i < n; i++) out[i] = N[i]; return sim.scanning ? -1 : n; }
}
namespace power { bool inStandby() { return sim.standby; } void activity() {} }
