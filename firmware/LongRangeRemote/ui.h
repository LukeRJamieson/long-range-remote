// ui.h - on-device user interface (400x300 reflective LCD, five keys on the control board).
#pragma once

namespace ui {
void begin();
void loop();                 // call frequently from Arduino loop()
void requestRedraw();
void setAsleep(bool asleep);   // redraws at once: header shows "asleep - any key"
void setNetworkInfo(const char *ap_ip, const char *sta_ip, const char *sta_ssid);
}
