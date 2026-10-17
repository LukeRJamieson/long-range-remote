// wifi_mgr.h - Wi-Fi: always-on access point + home network that is retried until it connects.
//
// Design (why it is event-driven): the Arduino-ESP32 3.x core rejects WiFi.begin() while an
// attempt is still in progress ("sta is connecting, cannot set config"). So exactly one attempt
// runs at a time, it is never interrupted, and the next one starts only after the driver has
// reported that the previous one ended (disconnect event). A watchdog resets an attempt only if
// the driver reports nothing at all for 60 s.
// The core also retries once by itself after the first failure since boot; the driver reports
// that internal restart as an extra disconnect, reason 36 STA_LEAVING. That echo is not a result:
// the attempt carries on, and the reason shown stays the real one from the event before it.
#pragma once
#include <Arduino.h>

namespace wifimgr {

enum State : uint8_t {
  NOT_CONFIGURED,   // no home network set: access point only
  CONNECTING,       // attempt in progress / about to retry
  CONNECTED,        // joined the home network and got an IP address
  FAILING,          // repeated failures (wrong password, network not found, ...) - still retrying
};

void begin();
void loop();
void reconnect();               // credentials changed: drop the current attempt and start again
// Radio off while the remote sleeps (power.cpp) / back on afterwards: access point and home-network
// attempts start again from scratch.
void suspend();
void resume();
bool suspended();

State state();
const char *statusText();       // human readable, e.g. "network not found - retrying"
uint32_t attempts();            // attempts since the last successful connection
String ssid();                  // home network name ("" if none)
String ip();                    // home network IP ("" if not connected)
String apIp();
uint32_t changeCounter();       // increments whenever the connection state changes

// Nearby networks, for the Wi-Fi menu. requestNetworks() asks for a fresh scan. It runs as soon as
// the radio is free - never during a connection attempt, which the driver refuses - so usually
// within a few seconds (up to ~20 s while an attempt to a missing network is under way). A scan
// for a connection attempt counts too. networks() then gives the list: one entry per name,
// strongest first, hidden networks left out; -1 while the requested scan hasn't finished.
struct Network { char ssid[33]; int8_t rssi; bool open; };
void requestNetworks();
int networks(Network *out, int max);

}  // namespace wifimgr
