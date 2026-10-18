// settings.cpp - NVS storage via the Arduino Preferences library.
#include "settings.h"
#include "ir_db.h"
#include <Preferences.h>
#include "sound.h"
#include "nvs.h"
#include "log.h"
#include "ir_tx.h"
#include "sd_store.h"
#include "wifi_file.h"

namespace settings {
namespace {
Preferences prefs;
int g_favs[MAX_FAVOURITES];
int g_nfav = 0;
int g_active = -1;

// Home Wi-Fi (see loadWifi)
String g_wifi_ssid, g_wifi_pass;
bool g_wifi_saved = false;
char g_wifi_src[120] = "";

String keyOf(int p) {
  return String(irdb::brandName(irdb::profileBrand(p))) + "|" + irdb::profileName(p);
}

int resolve(const String &k) {
  int bar = k.indexOf('|');
  if (bar < 0) return -1;
  return irdb::findProfile(k.substring(0, bar).c_str(), k.substring(bar + 1).c_str());
}

void save() {
  String s;
  for (int i = 0; i < g_nfav; i++) {
    if (i) s += '\n';
    s += keyOf(g_favs[i]);
  }
  prefs.putString("favs", s);
  prefs.putString("active", g_active >= 0 ? keyOf(g_active) : String(""));
}

// Write the current network to /wifi.txt. false + err if there is no card or the write failed.
bool saveWifi(char *err, size_t en) {
  std::string text = wififile::format(g_wifi_ssid.c_str(), g_wifi_pass.c_str());
  g_wifi_saved = sdstore::replaceFile(SD_WIFI_FILE, text, err, en);
  return g_wifi_saved;
}

void loadWifi() {
  std::string text;
  if (sdstore::readFile(SD_WIFI_FILE, text, 4096)) {
    wififile::Creds c = wififile::parse(text);
    g_wifi_ssid = c.ssid.c_str();
    g_wifi_pass = c.pass.c_str();
    g_wifi_saved = true;
    snprintf(g_wifi_src, sizeof(g_wifi_src), "SD card, wifi.txt");
    if (!c.has_ssid) logln("Settings: wifi.txt has no ssid= line - no home network");
    if (const char *why = wififile::check(c.ssid, c.pass))
      logf("Settings: wifi.txt: %s - the connection will probably fail\n", why);
    logf("Settings: home Wi-Fi from wifi.txt: \"%s\"\n", g_wifi_ssid.c_str());
    return;
  }
  // No file (or no card): the defaults from config.h, written to the card so they can be edited there.
  g_wifi_ssid = HOME_WIFI_SSID;
  g_wifi_pass = HOME_WIFI_PASSWORD;
  if (!sdstore::mounted()) {
    g_wifi_saved = false;
    snprintf(g_wifi_src, sizeof(g_wifi_src), "config.h (no SD card)");
    logln("Settings: no SD card - home Wi-Fi from config.h");
    return;
  }
  if (sdstore::fileExists(SD_WIFI_FILE)) {      // there, but unreadable or huge: never overwrite it
    g_wifi_saved = false;
    snprintf(g_wifi_src, sizeof(g_wifi_src), "config.h (wifi.txt unreadable)");
    logln("Settings: wifi.txt is on the card but couldn't be read (over 4 KB?) - home Wi-Fi from config.h");
    return;
  }
  char err[64];
  if (saveWifi(err, sizeof(err))) {
    snprintf(g_wifi_src, sizeof(g_wifi_src), "SD card, wifi.txt");
    logf("Settings: no wifi.txt on the card - created it from config.h (\"%s\")\n", g_wifi_ssid.c_str());
  } else {
    snprintf(g_wifi_src, sizeof(g_wifi_src), "config.h (couldn't write wifi.txt: %s)", err);
    logf("Settings: no wifi.txt, and creating it failed (%s) - home Wi-Fi from config.h\n", err);
  }
}
}  // namespace

void begin() {
  if (!prefs.begin("lrremote", false)) logln("Settings: could not open the settings storage (NVS)!");
  nvs_stats_t st;
  if (nvs_get_stats(NULL, &st) == ESP_OK)
    logf("Settings: storage %u of %u entries used, %u free\n", (unsigned)st.used_entries,
         (unsigned)st.total_entries, (unsigned)st.free_entries);
  // Older builds stored Wi-Fi credentials from the web page; they are no longer used - remove them.
  if (prefs.isKey("ssid") || prefs.isKey("pass")) {
    prefs.remove("ssid");
    prefs.remove("pass");
    logln("Settings: removed a Wi-Fi network saved in flash by an older version (wifi.txt is used now)");
  }
  loadWifi();                                   // needs the card: sdstore::begin() runs first
  String s = prefs.getString("favs", "");
  g_nfav = 0;
  int start = 0;
  while (start < (int)s.length() && g_nfav < MAX_FAVOURITES) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    int p = resolve(s.substring(start, nl));
    if (p >= 0 && !isFav(p)) g_favs[g_nfav++] = p;
    start = nl + 1;
  }
  g_active = resolve(prefs.getString("active", ""));
  irtx::setPower(prefs.getUChar("ledpwr", 0));
  sound::setVolume(prefs.getUChar("sndvol", SOUND_VOLUME_DEFAULT));
  sound::setMuted(prefs.getBool("sndmute", false));
}

int ledPower() { return irtx::power(); }

void setLedPower(int level) {
  irtx::setPower(level);
  prefs.putUChar("ledpwr", (uint8_t)irtx::power());
}

int soundVolume() { return sound::volume(); }

void setSoundVolume(int level) {
  sound::setVolume(level);
  prefs.putUChar("sndvol", (uint8_t)sound::volume());
}

bool soundMuted() { return sound::muted(); }

void setSoundMuted(bool m) {
  sound::setMuted(m);
  prefs.putBool("sndmute", m);
}

int favCount() { return g_nfav; }
int fav(int i) { return (i >= 0 && i < g_nfav) ? g_favs[i] : -1; }

bool isFav(int p) {
  for (int i = 0; i < g_nfav; i++) if (g_favs[i] == p) return true;
  return false;
}

bool addFav(int p) {
  if (p < 0 || p >= irdb::numProfiles()) return false;
  if (isFav(p)) return true;
  if (g_nfav >= MAX_FAVOURITES) return false;
  g_favs[g_nfav++] = p;
  save();
  return true;
}

void removeFav(int p) {
  int j = 0;
  for (int i = 0; i < g_nfav; i++) if (g_favs[i] != p) g_favs[j++] = g_favs[i];
  g_nfav = j;
  if (g_active == p) g_active = g_nfav ? g_favs[0] : -1;
  save();
}

void clearFavs() {
  g_nfav = 0;
  g_active = -1;
  save();
}

int active() { return g_active; }

void setActive(int p) {
  if (p < 0 || p >= irdb::numProfiles()) return;
  if (!isFav(p)) {
    if (g_nfav >= MAX_FAVOURITES) {           // drop the oldest favourite to make room
      memmove(g_favs, g_favs + 1, (MAX_FAVOURITES - 1) * sizeof(int));
      g_nfav--;
    }
    g_favs[g_nfav++] = p;
  }
  g_active = p;
  save();
}

String wifiSsid() { return g_wifi_ssid; }
String wifiPass() { return g_wifi_pass; }
const char *wifiSource() { return g_wifi_src; }
bool wifiSaved() { return g_wifi_saved; }

bool setWifi(const String &ssid, const String &pass, char *msg, size_t mn) {
  if (const char *why = wififile::check(ssid.c_str(), pass.c_str())) {
    snprintf(msg, mn, "Not changed: %s", why);
    return false;
  }
  g_wifi_ssid = ssid;
  g_wifi_pass = pass;
  char err[64];
  if (saveWifi(err, sizeof(err))) {
    snprintf(g_wifi_src, sizeof(g_wifi_src), "SD card, wifi.txt");
    snprintf(msg, mn, "Saved to wifi.txt on the SD card");
    logf("Settings: home Wi-Fi changed to \"%s\" - saved to wifi.txt\n", ssid.c_str());
  } else {
    snprintf(g_wifi_src, sizeof(g_wifi_src), "not saved (%s) - until restart", err);
    snprintf(msg, mn, "NOT saved (%s): used until the remote restarts", err);
    logf("Settings: home Wi-Fi changed to \"%s\" but NOT saved: %s\n", ssid.c_str(), err);
  }
  return true;
}

bool rewriteWifiFile(char *err, size_t en) {
  if (saveWifi(err, en)) {
    snprintf(g_wifi_src, sizeof(g_wifi_src), "SD card, wifi.txt");
    return true;
  }
  snprintf(g_wifi_src, sizeof(g_wifi_src), "not saved (%s) - until restart", err);
  return false;
}

}  // namespace settings
