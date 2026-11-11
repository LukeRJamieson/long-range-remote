#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string>
#include <algorithm>
using std::min; using std::max; using std::isnan;
struct String : std::string {
  String() {} String(const char *s) : std::string(s ? s : "") {} String(const std::string &s) : std::string(s) {}
};
extern uint32_t sim_ms;
inline uint32_t millis() { return sim_ms; }
