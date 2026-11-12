// ble_scan.h - the Bluetooth scanner: a list of the BLE devices heard nearby, what each one is
// (ble_ident.h), and how strong its signal is. Shown on the remote's screen (Menu > Bluetooth scanner)
// and on the web page (Bluetooth tab); both read the same list.
//
// The ESP32-S3 has Bluetooth LE only: devices that use nothing but classic Bluetooth (older headsets
// and car kits) aren't heard. Phones, earbuds, watches, trackers, TVs, speakers and most new
// gadgets advertise over BLE.
//
// The radio scans only while a scanner screen or the web tab is open (want()), and shares its time with
// Wi-Fi, so the web page keeps working.
//
// The list (ble_scan.cpp) has no Arduino dependencies; the radio side is in ble_glue.cpp.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ble_ident.h"

namespace blescan {

const int MAX_DEVICES = 96;             // when full, the device unheard for longest makes room
const uint32_t STALE_MS = 30000;        // not heard for this long: listed after the others, greyed

struct Device {
  uint8_t mac[6];                       // mac[0] first, as written: AA:BB:CC:DD:EE:FF
  uint8_t addr_type;                    // 0 public, 1 random (as reported)
  bleid::AddrKind kind;
  int8_t rssi, rssi_max;                // dBm: last, strongest
  float rssi_avg;                       // smoothed
  uint32_t first_ms, last_ms, count;    // first / last heard, adverts heard
  uint8_t raw[62]; uint8_t raw_len;     // the last advertisement (+ scan response)
  bleid::Adv adv;                       // parsed; the name and other fields are kept when a later advert lacks them
  bleid::Ident id;
  char oui[40];                         // MAC prefix vendor (public addresses, with oui.bin on the card)
};

// One advertisement heard (from the radio, or a test). now = milliseconds.
void feed(const uint8_t mac[6], int addr_type, int rssi, const uint8_t *payload, int len, uint32_t now);
int count();
const Device &device(int i);
int find(const uint8_t mac[6]);         // index, -1 if not in the list
void clear();

enum Sort : uint8_t { SORT_SIGNAL, SORT_NAME, SORT_NEWEST, N_SORTS };
const char *sortName(int s);            // "signal", "name", "newest"
// Indexes into the list in display order (heard recently first, then by the sort). Returns how many.
int sorted(int *idx, int max, int sort, uint32_t now);

// What to call it in a list: its name, else the detail ("AirPods Pro"), else brand + type
void label(const Device &d, char *out, size_t n);
void macText(const uint8_t mac[6], char *out, size_t n);       // "AA:BB:CC:DD:EE:FF"
// Signal strength as 0..4 bars (-55 dBm and stronger = 4)
int bars(int rssi);

// MAC prefix lookup (set by the glue: oui.bin on the SD card). Without it, no MAC vendors.
void setOuiLookup(bool (*fn)(const uint8_t mac[6], char *out, size_t n));
void refreshOui();                      // look the public addresses up again (oui.bin was replaced)

// ---- radio (ble_glue.cpp) ----
void want(uint32_t ms = 15000);        // keep scanning for at least this long (the screen calls it while shown)
void loop();                            // from the Arduino loop: starts / stops the radio, takes in what it heard
bool scanning();
const char *status();                   // "scanning", "off", "Bluetooth failed to start", ...
bool ouiLoaded();                       // oui.bin was found and read
void reloadOui();                       // read oui.bin again (after a web upload) and update the list

}  // namespace blescan
