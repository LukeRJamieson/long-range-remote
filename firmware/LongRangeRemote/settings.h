// settings.h - persistent settings: favourite devices, active device, LED power, sound (NVS), and the home
// Wi-Fi network (/wifi.txt on the SD card).
// Favourites are stored by "Brand|Profile" name so they survive database regeneration.
#pragma once
#include <Arduino.h>
#include "config.h"

namespace settings {

void begin();

// Favourites (profile indices into irdb). The active device is always a favourite.
int  favCount();
int  fav(int i);
bool isFav(int profile);
bool addFav(int profile);          // false if the list is full
void removeFav(int profile);       // if it was the active device, the next one becomes active
void clearFavs();                  // remove every saved device (no active device afterwards)

int  active();                     // active profile index, -1 if none
void setActive(int profile);       // also adds it to favourites

int  ledPower();                   // LED power level, 0 = full (saved; applied to irtx at start-up)
void setLedPower(int level);       // clamps, saves and applies it

int  soundVolume();                // key-sound volume 0..SOUND_VOLUME_MAX (saved; applied to sound)
void setSoundVolume(int level);    // clamps, saves and applies it
bool soundMuted();
void setSoundMuted(bool m);        // saves and applies it; the volume is kept for unmuting

// Home Wi-Fi network. Read from /wifi.txt on the SD card at start-up (format: wifi_file.h). If the
// card has no wifi.txt, the defaults in config.h (HOME_WIFI_SSID / HOME_WIFI_PASSWORD) are used and
// written to a new wifi.txt, so the card always shows what the remote uses.
String wifiSsid();
String wifiPass();
const char *wifiSource();          // where the network is kept, one line for the screens
bool wifiSaved();                  // false: the current network is not on the card (no card / write failed)

// Change the home network (the Wi-Fi menu). It is checked first (wififile::check): if it can't be
// used, false + why in msg, and nothing changes. Otherwise it is used at once and saved to
// /wifi.txt, and msg says whether the save worked (without a card, or if the write fails, it
// lasts until the remote restarts). The caller then calls wifimgr::reconnect().
bool setWifi(const String &ssid, const String &pass, char *msg, size_t msg_len);

}  // namespace settings
