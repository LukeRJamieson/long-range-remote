// power.h - idle sleep. A remote spends most of its life waiting, so after IDLE_SLEEP_S without a
// key press (and with no phone on the Wi-Fi access point) it switches Wi-Fi off and enters ESP32
// light sleep. Any key wakes it in milliseconds and that key press is acted on straight away (the
// screen keeps its picture while asleep - it is a reflective LCD). Wi-Fi comes back a moment later.
//
// Joined to the home network, it first spends HOME_STANDBY_MIN in standby: still connected and
// serving the web page (asleep it can't be reached), with the access point off, Wi-Fi power saving
// on and the processor slowed. A page request speeds it up again; a key press ends standby.
#pragma once

namespace power {
void begin();
void loop();          // call from the Arduino loop()
bool inStandby();      // on the home network with the access point off (see config.h, HOME_STANDBY_MIN)
void activity();      // something happened (key, web request): restart the idle timer
}
