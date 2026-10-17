// flipper_ir.cpp - Flipper ".ir" files -> user part of the IR database (see flipper_ir.h).
//
// File format (one block per button, blocks separated by "#" lines):
//   Filetype: IR signals file          name: Power            name: Vol_up
//   Version: 1                         type: parsed           type: raw
//   #                                  protocol: NEC          frequency: 38000
//                                      address: 07 00 00 00   duty_cycle: 0.330000
//                                      command: 02 00 00 00   data: 9024 4512 579 552 ...
// Parsed protocols are converted exactly like tools/build_db.py (flipper_convert); raw captures are
// stored as raw signals.
#include "flipper_ir.h"
#include "ir_db.h"
#include "ir_keys.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <vector>

namespace flipperir {
namespace {

uint32_t leHex(const char *s) {
  uint32_t v = 0;
  int shift = 0;
  while (*s && shift < 32) {
    while (*s == ' ') s++;
    if (!isxdigit((unsigned char)s[0])) break;
    char *e;
    unsigned long b = strtoul(s, &e, 16);
    if (e == s) break;
    v |= (uint32_t)(b & 0xFF) << shift;
    shift += 8;
    s = e;
  }
  return v;
}

inline uint16_t inv8(uint32_t x) { return (uint16_t)(~x & 0xFF); }

int catIndex(const char *name) {
  for (int i = 0; i < IR_NUM_CATS; i++) if (strcmp(IR_CAT_NAMES[i], name) == 0) return i;
  return IR_NUM_CATS - 1;     // "Other"
}

}  // namespace

int categoryForFolder(const char *folder) {
  // same mapping as FLIPPER_DIRS in tools/build_db.py
  static const char *const MAP[][2] = {
      {"TVs", "TV"}, {"Universal_TV_Remotes", "TV"}, {"Audio_and_Video_Receivers", "AVR"},
      {"SoundBars", "Soundbar"}, {"Speakers", "Speaker"}, {"CD_Players", "CD"}, {"MiniDisc", "MD"},
      {"Projectors", "Projector"}, {"Cable_Boxes", "SetTop"}, {"DVB-T", "SetTop"}, {"TV_Tuner", "SetTop"},
      {"Streaming_Devices", "Streaming"}, {"Blu-Ray", "Disc"}, {"DVD_Players", "Disc"}, {"Laserdisc", "Disc"},
      {"VCR", "VCR"}, {"Consoles", "Console"}, {"Monitors", "Monitor"}, {"Touchscreen_Displays", "Monitor"},
      {"Digital_Signs", "Monitor"}, {"Fans", "Climate"}, {"Heaters", "Climate"}, {"Air_Purifiers", "Climate"},
      {"Humidifiers", "Climate"}, {"Fireplaces", "Climate"}, {"ACs", "AC"}, {"LED_Lighting", "Light"},
      {"Car_Multimedia", "Car"}, {"Head_Units", "Car"}, {"Cameras", "Camera"}, {"CCTV", "Camera"},
  };
  for (auto &m : MAP) if (strcasecmp(m[0], folder) == 0) return catIndex(m[1]);
  // plain-English folder names people may use for their own files
  for (int i = 0; i < IR_NUM_CATS; i++) if (strcasecmp(IR_CAT_NAMES[i], folder) == 0) return i;
  return -1;
}

bool convertParsed(const char *proto, const char *address, const char *command, IrCodeParams &o) {
  uint32_t addr = leHex(address), cmd = leHex(command);
  uint32_t a = addr & 0xFF, c = cmd & 0xFF;
  o.s = 0;
  if (!strcmp(proto, "NEC")) {
    o.proto = IRP_NEC; o.d = (uint16_t)(a | (inv8(a) << 8)); o.f = (uint16_t)(c | (inv8(c) << 8));
  } else if (!strcmp(proto, "NECext")) {
    o.proto = IRP_NEC; o.d = (uint16_t)(addr & 0xFFFF); o.f = (uint16_t)(cmd & 0xFFFF);
  } else if (!strcmp(proto, "NEC42")) {
    o.proto = IRP_AIWA; o.d = (uint16_t)(addr & 0x1FFF); o.f = (uint16_t)c;
  } else if (!strcmp(proto, "Samsung32")) {
    o.proto = IRP_NECX; o.d = (uint16_t)(a | (a << 8)); o.f = (uint16_t)(c | (inv8(c) << 8));
  } else if (!strcmp(proto, "Pioneer")) {
    o.proto = IRP_PIONEER; o.d = (uint16_t)(a | (inv8(a) << 8)); o.f = (uint16_t)(c | (inv8(c) << 8));
  } else if (!strcmp(proto, "Kaseikyo")) {
    uint32_t ident = (addr >> 24) & 3, vendor = (addr >> 8) & 0xFFFF;
    uint32_t g1 = (addr >> 4) & 0xF, g2 = addr & 0xF;
    uint32_t vp = (vendor & 0xFF) ^ (vendor >> 8);
    vp = (vp & 0xF) ^ (vp >> 4);
    uint32_t b2 = (vp & 0xF) | (g1 << 4), b3 = g2 | ((cmd & 0xF) << 4), b4 = ((ident << 6) | (cmd >> 4)) & 0xFF;
    o.proto = IRP_KASEIKYO; o.d = (uint16_t)vendor; o.s = (uint16_t)(b2 & 0xFF);
    o.f = (uint16_t)((b3 & 0xFF) | (b4 << 8));
  } else if (!strcmp(proto, "RC5")) {
    o.proto = IRP_RC5; o.d = (uint16_t)(addr & 0x1F); o.f = (uint16_t)(cmd & 0x3F);
  } else if (!strcmp(proto, "RC5X")) {
    o.proto = IRP_RC5; o.d = (uint16_t)(addr & 0x1F); o.f = (uint16_t)((cmd & 0x3F) | 0x40);
  } else if (!strcmp(proto, "RC6")) {
    o.proto = IRP_RC6; o.d = (uint16_t)a; o.f = (uint16_t)c;
  } else if (!strcmp(proto, "SIRC")) {
    o.proto = IRP_SONY12; o.d = (uint16_t)(addr & 0x1F); o.f = (uint16_t)(cmd & 0x7F);
  } else if (!strcmp(proto, "SIRC15")) {
    o.proto = IRP_SONY15; o.d = (uint16_t)a; o.f = (uint16_t)(cmd & 0x7F);
  } else if (!strcmp(proto, "SIRC20")) {
    o.proto = IRP_SONY20; o.d = (uint16_t)(addr & 0x1FFF); o.f = (uint16_t)(cmd & 0x7F);
  } else if (!strcmp(proto, "RCA")) {
    o.proto = IRP_RCA; o.d = (uint16_t)(addr & 0xF); o.f = (uint16_t)c;
  } else {
    return false;
  }
  return true;
}

bool addFile(const char *text, size_t len, const char *brand, const char *name, uint8_t cat, FileStats &st) {
  // one signal = the key/value lines between "name:" lines
  struct Sig { char name[32] = ""; char type[8] = ""; char proto[16] = ""; char addr[24] = ""; char cmd[24] = "";
               uint32_t freq = 38000; float duty = 0.33f; const char *data = nullptr; size_t data_len = 0; };
  bool started = false;
  int added = 0;
  Sig s;
  std::vector<uint16_t> dur;
  auto flush = [&]() {
    if (!s.name[0]) return;
    if (!started) { if (!irdb::userProfile(brand, name, cat)) return; started = true; }
    if (!strcmp(s.type, "parsed")) {
      IrCodeParams p;
      if (convertParsed(s.proto, s.addr, s.cmd, p) && irdb::userCode(s.name, p)) { st.parsed++; added++; }
      else st.skipped++;
    } else if (!strcmp(s.type, "raw") && s.data) {
      dur.clear();
      const char *q = s.data, *end = s.data + s.data_len;
      while (q < end && dur.size() <= 2000) {
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        if (q >= end || !(isdigit((unsigned char)*q) || *q == '-')) break;
        long v = strtol(q, (char **)&q, 10);
        if (v < 0) v = -v;
        dur.push_back((uint16_t)(v > 65535 ? 65535 : v));
      }
      if (dur.size() >= 4 && dur.size() <= 2000 &&
          irdb::userRawCode(s.name, dur.data(), (int)dur.size(), s.freq, (uint8_t)(s.duty * 100 + 0.5f))) {
        st.raw++; added++;
      } else {
        st.skipped++;
      }
    } else {
      st.skipped++;
    }
    s = Sig();
  };
  size_t i = 0;
  while (i < len) {
    size_t e = i;
    while (e < len && text[e] != '\n') e++;
    const char *line = text + i;
    size_t ll = e - i;
    while (ll && (line[ll - 1] == '\r' || line[ll - 1] == ' ')) ll--;
    while (ll && (*line == ' ' || *line == '\t')) { line++; ll--; }
    i = e + 1;
    if (ll == 0 || line[0] == '#') continue;
    const char *colon = (const char *)memchr(line, ':', ll);
    if (!colon) continue;
    size_t kl = colon - line;
    const char *v = colon + 1;
    size_t vl = ll - kl - 1;
    while (vl && *v == ' ') { v++; vl--; }
    auto key = [&](const char *k) { return kl == strlen(k) && strncasecmp(line, k, kl) == 0; };
    auto copy = [&](char *dst, size_t n) { size_t m = vl < n - 1 ? vl : n - 1; memcpy(dst, v, m); dst[m] = 0; };
    if (key("name")) { flush(); copy(s.name, sizeof(s.name)); }
    else if (key("type")) copy(s.type, sizeof(s.type));
    else if (key("protocol")) copy(s.proto, sizeof(s.proto));
    else if (key("address")) copy(s.addr, sizeof(s.addr));
    else if (key("command")) copy(s.cmd, sizeof(s.cmd));
    else if (key("frequency")) s.freq = (uint32_t)strtoul(v, nullptr, 10);
    else if (key("duty_cycle")) s.duty = strtof(v, nullptr);
    else if (key("data")) { s.data = v; s.data_len = vl; }
  }
  flush();
  return added > 0;
}

}  // namespace flipperir
