// log.cpp - see log.h
#include "log.h"
#include <Arduino.h>
#include <stdarg.h>

void logBegin() {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);        // native USB: never wait for a host to read
  Serial.begin(115200);
#else
  Serial.setTxBufferSize(4096);    // UART (CP2102 port): room for start-up bursts; must precede begin()
  Serial.begin(115200);
#endif
}

static void writeNonBlocking(const char *buf, size_t n) {
  if (n == 0) return;
  int room = Serial.availableForWrite();
  if (room >= (int)n) Serial.write((const uint8_t *)buf, n);   // fits: send; otherwise drop the line
}

void logf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
  writeNonBlocking(buf, n);
}

void logln(const char *s) {
  char buf[256];
  int n = snprintf(buf, sizeof(buf), "%s\n", s);
  if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
  writeNonBlocking(buf, n > 0 ? n : 0);
}
