// log.h - serial logging that NEVER blocks the firmware.
//
// Why: on the ESP32-S3's native USB port ("USB CDC On Boot: Enabled") every Serial write waits up
// to 100 ms for a terminal to read it once the small buffer is full - with no Serial Monitor open,
// a burst of Wi-Fi messages could stall start-up for seconds. Here a message is written only if
// the port can take it immediately; otherwise it is dropped. The remote works the same with or
// without a terminal attached.
#pragma once

void logBegin();                                 // call first in setup()
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logln(const char *s);
