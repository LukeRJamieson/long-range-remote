#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <functional>
extern uint32_t sim_ms;
inline uint32_t millis() { return sim_ms; }
inline void delay(uint32_t ms) { sim_ms += ms; }
class String { public: std::string s; String() {} String(const char *c) : s(c ? c : "") {}
  size_t length() const { return s.size(); } const char *c_str() const { return s.c_str(); }
  bool operator==(const String &o) const { return s == o.s; } };
struct SerialC { bool quiet = true; template <typename... A> void printf(const char *f, A... a) { if (!quiet) ::printf(f, a...); }
  void println(const char *s) { if (!quiet) ::puts(s); } };
extern SerialC Serial;
