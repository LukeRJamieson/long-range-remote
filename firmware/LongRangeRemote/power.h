// power.h - idle sleep. A remote spends most of its life waiting, so after IDLE_SLEEP_S without a
// key press (and with no phone on the Wi-Fi access point) it switches Wi-Fi off and enters ESP32
// light sleep. Any key wakes it in milliseconds and that key press is acted on straight away (the
// screen keeps its picture while asleep - it is a reflective LCD). Wi-Fi comes back a moment later.
#pragma once

namespace power {
void begin();
void loop();          // call from the Arduino loop()
void activity();      // something happened (key, web request): restart the idle timer
}
