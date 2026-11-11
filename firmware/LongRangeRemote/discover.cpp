// discover.cpp - see discover.h
#include "discover.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "ir_db.h"
#include "ir_keys.h"

namespace discover {
namespace {

// ---------------------------------------------------------------------------
// Protocol families
// ---------------------------------------------------------------------------
enum Kind : uint8_t {
  K_NEC, K_NECEXT, K_NECX, K_SONY12, K_RC5, K_PANA, K_RC6, K_PIONEER, K_JVC, K_SONY15, K_DENON, K_SHARP,
  K_MITSU, K_RCA, K_AKAI, K_EMERSON, K_RECS80, K_BOSE,
};
struct Def {
  Family f;
  Kind kind;
  uint16_t n_addr;        // 0: the addresses come from the database (NEC extended)
  uint16_t n_func;
  const char *flipper;    // protocol name in the saved file
};
// Most common first: "all protocols" sweeps in this order.
const Def DEFS[] = {
  {{"NEC", "most Asian brands: TVs, amps, soundbars, LED lights", "NEC"}, K_NEC, 256, 256, "NEC"},
  {{"NEC extended", "newer TVs and boxes (TCL, Hisense, LG...)", "NECext"}, K_NECEXT, 0, 256, "NECext"},
  {{"Samsung", "Samsung, some LG", "Samsung"}, K_NECX, 256, 256, "Samsung32"},
  {{"Sony 12-bit", "Sony TVs and hi-fi", "Sony12"}, K_SONY12, 32, 128, "SIRC"},
  {{"Philips RC5", "Philips, Marantz, European hi-fi", "RC5"}, K_RC5, 32, 128, "RC5"},
  {{"Panasonic", "Panasonic, Technics", "Panasonic"}, K_PANA, 256, 256, "Panasonic"},
  {{"Philips RC6", "Philips, set-top boxes", "RC6"}, K_RC6, 256, 256, "RC6"},
  {{"Pioneer", "Pioneer", "Pioneer"}, K_PIONEER, 256, 256, "Pioneer"},
  {{"JVC", "JVC, some Kenwood", "JVC"}, K_JVC, 256, 256, "JVC"},
  {{"Sony 15-bit", "Sony receivers, players", "Sony15"}, K_SONY15, 256, 128, "SIRC15"},
  {{"Denon", "Denon, newer Marantz", "Denon"}, K_DENON, 32, 256, "Denon"},
  {{"Sharp", "Sharp", "Sharp"}, K_SHARP, 32, 256, "Sharp"},
  {{"Mitsubishi", "Mitsubishi, old Kenwood", "Mitsubishi"}, K_MITSU, 256, 256, "Mitsubishi"},
  {{"RCA", "RCA, Thomson", "RCA"}, K_RCA, 16, 256, "RCA"},
  {{"Akai", "Akai", "Akai"}, K_AKAI, 8, 128, "Akai"},
  {{"Emerson", "Emerson", "Emerson"}, K_EMERSON, 64, 64, "Emerson"},
  {{"Philips RECS80", "older Philips, Thomson hi-fi", "RECS80"}, K_RECS80, 8, 64, "RECS80"},
  {{"Bose", "Bose Wave, Lifestyle", "Bose"}, K_BOSE, 1, 256, "Bose"},
};
const int N_DEFS = sizeof(DEFS) / sizeof(DEFS[0]);
const int MAX_EXT_ADDRS = 1024;       // NEC extended: the most common database addresses
const int MAX_PROBES = 6;

inline uint16_t inv8(uint16_t x) { return (uint16_t)(~x & 0xFF); }
inline uint16_t withInv(uint16_t x) { return (uint16_t)((x & 0xFF) | (inv8(x) << 8)); }
inline bool isInvPair(uint16_t v) { return (v >> 8) == inv8(v); }

IrCodeParams makeCode(Kind k, int a, int fn) {
  IrCodeParams p;
  p.s = 0;
  switch (k) {
    case K_NEC:     p.proto = IRP_NEC; p.d = withInv(a); p.f = withInv(fn); break;
    case K_NECEXT:  p.proto = IRP_NEC; p.d = (uint16_t)a; p.f = withInv(fn); break;
    case K_NECX:    p.proto = IRP_NECX; p.d = (uint16_t)((a & 0xFF) | ((a & 0xFF) << 8)); p.f = withInv(fn); break;
    case K_PIONEER: p.proto = IRP_PIONEER; p.d = withInv(a); p.f = withInv(fn); break;
    case K_SONY12:  p.proto = IRP_SONY12; p.d = a & 0x1F; p.f = fn & 0x7F; break;
    case K_SONY15:  p.proto = IRP_SONY15; p.d = a & 0xFF; p.f = fn & 0x7F; break;
    case K_RC5:     p.proto = IRP_RC5; p.d = a & 0x1F; p.f = fn & 0x7F; break;
    case K_RC6:     p.proto = IRP_RC6; p.d = a & 0xFF; p.f = fn & 0xFF; break;
    case K_PANA:    p.proto = IRP_KASEIKYO; p.d = 0x2002; p.s = a & 0xFF; p.f = (uint16_t)((fn & 0xFF) << 8); break;
    case K_JVC:     p.proto = IRP_JVC; p.d = a & 0xFF; p.f = fn & 0xFF; break;
    case K_DENON:   p.proto = IRP_DENON; p.d = a & 0x1F; p.f = fn & 0xFF; break;
    case K_SHARP:   p.proto = IRP_SHARP; p.d = a & 0x1F; p.f = fn & 0xFF; break;
    case K_MITSU:   p.proto = IRP_MITSUBISHI; p.d = a & 0xFF; p.f = fn & 0xFF; break;
    case K_RCA:     p.proto = IRP_RCA; p.d = a & 0xF; p.f = fn & 0xFF; break;
    case K_AKAI:    p.proto = IRP_AKAI; p.d = a & 0x7; p.f = fn & 0x7F; break;
    case K_EMERSON: p.proto = IRP_EMERSON; p.d = a & 0x3F; p.f = fn & 0x3F; break;
    case K_RECS80:  p.proto = IRP_RECS80; p.d = a & 0x7; p.f = fn & 0x3F; break;
    default:        p.proto = IRP_BOSE; p.d = 0; p.f = fn & 0xFF; break;
  }
  return p;
}

// The reverse, for a database code: does it belong to this family, and which address / function?
bool decode(Kind k, const IrCode &c, int &a, int &fn) {
  switch (k) {
    case K_NEC:
      if ((c.proto != IRP_NEC && c.proto != IRP_NEC2) || !isInvPair(c.d) || !isInvPair(c.f)) return false;
      a = c.d & 0xFF; fn = c.f & 0xFF; return true;
    case K_NECEXT:
      if ((c.proto != IRP_NEC && c.proto != IRP_NEC2) || isInvPair(c.d) || !isInvPair(c.f)) return false;
      a = c.d; fn = c.f & 0xFF; return true;
    case K_NECX:
      if (c.proto != IRP_NECX || (c.d >> 8) != (c.d & 0xFF) || !isInvPair(c.f)) return false;
      a = c.d & 0xFF; fn = c.f & 0xFF; return true;
    case K_PIONEER:
      if (c.proto != IRP_PIONEER || !isInvPair(c.d) || !isInvPair(c.f)) return false;
      a = c.d & 0xFF; fn = c.f & 0xFF; return true;
    case K_SONY12: if (c.proto != IRP_SONY12) return false; a = c.d & 0x1F; fn = c.f & 0x7F; return true;
    case K_SONY15: if (c.proto != IRP_SONY15) return false; a = c.d & 0xFF; fn = c.f & 0x7F; return true;
    case K_RC5:    if (c.proto != IRP_RC5) return false; a = c.d & 0x1F; fn = c.f & 0x7F; return true;
    case K_RC6:    if (c.proto != IRP_RC6) return false; a = c.d & 0xFF; fn = c.f & 0xFF; return true;
    case K_PANA:
      if (c.proto != IRP_KASEIKYO || c.d != 0x2002 || (c.f & 0xFF) != 0) return false;
      a = c.s & 0xFF; fn = c.f >> 8; return true;
    case K_JVC:    if (c.proto != IRP_JVC && c.proto != IRP_JVC2) return false; a = c.d & 0xFF; fn = c.f & 0xFF; return true;
    case K_DENON:
      if (c.proto != IRP_DENON && c.proto != IRP_DENON1 && c.proto != IRP_DENON2) return false;
      a = c.d & 0x1F; fn = c.f & 0xFF; return true;
    case K_SHARP:  if (c.proto != IRP_SHARP) return false; a = c.d & 0x1F; fn = c.f & 0xFF; return true;
    case K_MITSU:  if (c.proto != IRP_MITSUBISHI) return false; a = c.d & 0xFF; fn = c.f & 0xFF; return true;
    case K_RCA:    if (c.proto != IRP_RCA && c.proto != IRP_RCA38) return false; a = c.d & 0xF; fn = c.f & 0xFF; return true;
    case K_AKAI:   if (c.proto != IRP_AKAI) return false; a = c.d & 0x7; fn = c.f & 0x7F; return true;
    case K_EMERSON: if (c.proto != IRP_EMERSON) return false; a = c.d & 0x3F; fn = c.f & 0x3F; return true;
    case K_RECS80: if (c.proto != IRP_RECS80) return false; a = c.d & 0x7; fn = c.f & 0x3F; return true;
    default:       if (c.proto != IRP_BOSE) return false; a = 0; fn = c.f & 0xFF; return true;
  }
}

// ---------------------------------------------------------------------------
// What the database says about one family (kept for the family in use only)
// ---------------------------------------------------------------------------
struct Prep {
  int fam = -1;
  std::vector<uint16_t> addr_order;    // addresses: the database's first (most used first), then the rest
  std::vector<uint8_t> func_order;     // functions: most used first, then the rest
  uint8_t guess[256];                  // function -> its most common key
  uint8_t probe[MAX_PROBES];
  int n_probe = 0;
};
Prep P;

void topInto(const uint32_t *hist, int n, int want, uint8_t *out, int &n_out, int max_out) {
  for (int t = 0; t < want && n_out < max_out; t++) {
    int best = -1;
    for (int i = 0; i < n; i++) {
      if (!hist[i]) continue;
      bool have = false;
      for (int j = 0; j < n_out; j++) if (out[j] == i) have = true;
      if (!have && (best < 0 || hist[i] > hist[best])) best = i;
    }
    if (best < 0) return;
    out[n_out++] = (uint8_t)best;
  }
}

void ensure(int fam) {
  if (P.fam == fam) return;
  const Def &d = DEFS[fam];
  P.fam = fam;
  P.addr_order.clear(); P.func_order.clear(); P.n_probe = 0;
  memset(P.guess, 0, sizeof(P.guess));
  const int n_addr_space = d.n_addr ? d.n_addr : 65536;
  std::vector<uint16_t> addr_cnt(n_addr_space, 0);
  std::vector<uint16_t> keyhist((size_t)d.n_func * KEY__COUNT, 0);
  uint32_t func_cnt[256] = {0}, pw[256] = {0}, vol[256] = {0}, mute[256] = {0};
  for (int c = 0, n = irdb::numCodes(); c < n; c++) {
    const IrCode &ic = irdb::code(c);
    int a, fn;
    if (!decode(d.kind, ic, a, fn) || a >= n_addr_space || fn >= d.n_func) continue;
    if (addr_cnt[a] < 0xFFFF) addr_cnt[a]++;
    func_cnt[fn]++;
    if (ic.key && ic.key < KEY__COUNT) {
      uint16_t &h = keyhist[(size_t)fn * KEY__COUNT + ic.key];
      if (h < 0xFFFF) h++;
    }
    if (ic.key == KEY_POWER) pw[fn] += 2;
    else if (ic.key == KEY_POWER_ON) pw[fn] += 1;
    else if (ic.key == KEY_VOL_UP) vol[fn]++;
    else if (ic.key == KEY_MUTE) mute[fn]++;
  }
  // addresses
  std::vector<uint16_t> known;
  for (int a = 0; a < n_addr_space; a++) if (addr_cnt[a]) known.push_back((uint16_t)a);
  std::stable_sort(known.begin(), known.end(), [&](uint16_t x, uint16_t y) { return addr_cnt[x] > addr_cnt[y]; });
  if (!d.n_addr && (int)known.size() > MAX_EXT_ADDRS) known.resize(MAX_EXT_ADDRS);
  P.addr_order = known;
  if (d.n_addr) for (int a = 0; a < d.n_addr; a++) if (!addr_cnt[a]) P.addr_order.push_back((uint16_t)a);
  // functions
  std::vector<uint8_t> used;
  for (int f = 0; f < d.n_func; f++) if (func_cnt[f]) used.push_back((uint8_t)f);
  std::stable_sort(used.begin(), used.end(), [&](uint8_t x, uint8_t y) { return func_cnt[x] > func_cnt[y]; });
  P.func_order = used;
  for (int f = 0; f < d.n_func; f++) if (!func_cnt[f]) P.func_order.push_back((uint8_t)f);
  for (int f = 0; f < d.n_func; f++) {
    const uint16_t *h = &keyhist[(size_t)f * KEY__COUNT];
    int best = 0;
    for (int k = 1; k < KEY__COUNT; k++) if (h[k] > h[best]) best = k;    // h[0] is never counted
    P.guess[f] = (uint8_t)best;
  }
  // probes: 3 x power, 2 x volume up, 1 x mute; then whatever is most used
  topInto(pw, d.n_func, 3, P.probe, P.n_probe, MAX_PROBES);
  topInto(vol, d.n_func, 2, P.probe, P.n_probe, MAX_PROBES);
  topInto(mute, d.n_func, 1, P.probe, P.n_probe, MAX_PROBES);
  topInto(func_cnt, d.n_func, MAX_PROBES, P.probe, P.n_probe, MAX_PROBES);
  for (int f = 0; P.n_probe < 4 && f < d.n_func; f++) {       // a family the database doesn't know at all
    bool have = false;
    for (int j = 0; j < P.n_probe; j++) if (P.probe[j] == f) have = true;
    if (!have) P.probe[P.n_probe++] = (uint8_t)f;
  }
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
const uint32_t MIN_POST_MS = 70;      // between two probes, at least
const uint32_t GAP_MS = 45;           // quiet time after a frame before the next one
const int STEP_BACK = 3;              // addresses to go back when the user says "it reacted"
const uint32_t PROBE_MS = 135;        // for the time estimate

Hooks H = {nullptr, nullptr, nullptr};

struct Engine {
  Mode mode = IDLE;
  bool all = false, done = false;
  int fam = 0, fam_i = 0;             // fam_i: index while sweeping all (== fam)
  int pos = 0;                        // index into P.addr_order
  // burst being sent
  int burst_addr = 0, burst_i = -1, burst_n = 0;
  uint8_t burst_f[MAX_PROBES];
  uint32_t last_post = 0, quiet_since = 0;
  bool posted = false;
  // learn
  int laddr = 0, fpos = 0;
  Learned L[MAX_LEARNED];
  int nl = 0;
} E;

int addrAt(int pos) { return pos >= 0 && pos < (int)P.addr_order.size() ? P.addr_order[pos] : 0; }

void startBurst(int addr) {
  E.burst_addr = addr;
  E.burst_n = P.n_probe;
  memcpy(E.burst_f, P.probe, sizeof(E.burst_f));
  E.burst_i = 0;
}

void sendOne(int addr, int fn) {
  E.burst_addr = addr; E.burst_n = 1; E.burst_f[0] = (uint8_t)fn; E.burst_i = 0;
}

// Move through the sweep order by `delta` addresses; when sweeping all, across family borders too.
void stepPos(int delta) {
  int p = E.pos + delta;
  while (p < 0) {
    if (!E.all || E.fam == 0) { p = 0; break; }
    E.fam--; E.fam_i = E.fam; ensure(E.fam);
    p += (int)P.addr_order.size();
  }
  while (p >= (int)P.addr_order.size()) {
    if (!E.all || E.fam + 1 >= N_DEFS) { p = (int)P.addr_order.size() - 1; break; }
    p -= (int)P.addr_order.size();
    E.fam++; E.fam_i = E.fam; ensure(E.fam);
  }
  E.pos = p < 0 ? 0 : p;
}

// The learned keys of an earlier run for this address, so adding keys later doesn't lose them
void preload() {
  char brand[24], remote[40];
  fileNames(brand, sizeof(brand), remote, sizeof(remote));
  int p = irdb::findProfile(brand, remote);
  if (p < 0) return;
  int c0 = irdb::profileFirstCode(p), n = irdb::profileCodeCount(p);
  for (int i = 0; i < n && E.nl < MAX_LEARNED; i++) {
    const IrCode &ic = irdb::code(c0 + i);
    int a, fn;
    if (!decode(DEFS[E.fam].kind, ic, a, fn) || a != E.laddr) continue;
    Learned &l = E.L[E.nl++];
    l.func = (uint8_t)fn; l.key = ic.key;
    snprintf(l.name, sizeof(l.name), "%s", irdb::codeName(c0 + i));
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
int numFamilies() { return N_DEFS; }
const Family &family(int fam) { return DEFS[fam < 0 || fam >= N_DEFS ? 0 : fam].f; }
int numAddrs(int fam) { ensure(fam); return (int)P.addr_order.size(); }
int numFuncs(int fam) { return DEFS[fam].n_func; }
IrCodeParams code(int fam, int addr, int func) { return makeCode(DEFS[fam].kind, addr, func); }

void addrText(int fam, int addr, char *out, size_t n) {
  switch (DEFS[fam].kind) {
    case K_NECEXT: snprintf(out, n, "device %d.%d", addr & 0xFF, addr >> 8); break;
    case K_BOSE:   snprintf(out, n, "the only address"); break;
    case K_RC5:    snprintf(out, n, "system %d", addr); break;
    default:       snprintf(out, n, "device %d", addr);
  }
}

uint8_t guessKey(int fam, int func) { ensure(fam); return func >= 0 && func < 256 ? P.guess[func] : 0; }

int probes(int fam, uint8_t *out, int max) {
  ensure(fam);
  int n = std::min(max, P.n_probe);
  memcpy(out, P.probe, n);
  return n;
}

void setHooks(const Hooks &h) { H = h; }

State state() {
  State s;
  memset(&s, 0, sizeof(s));
  s.mode = E.mode; s.all = E.all; s.done = E.done; s.fam = E.fam;
  s.fam_pos = E.fam; s.fam_total = N_DEFS;
  s.sending = E.burst_i >= 0 || (H.busy && H.busy());
  s.n_learned = E.nl;
  if (E.mode == IDLE) return s;
  ensure(E.fam);
  s.total = (int)P.addr_order.size();
  s.pos = E.pos;
  s.addr = E.mode == LEARN ? E.laddr : addrAt(E.pos);
  if (E.mode == LEARN) {
    s.func_pos = E.fpos;
    s.func = E.fpos < (int)P.func_order.size() ? P.func_order[E.fpos] : 0;
  }
  if (E.mode == SWEEP) {
    uint32_t left = (uint32_t)(s.total - s.pos);
    if (E.all) for (int f = E.fam + 1; f < N_DEFS; f++) left += DEFS[f].n_addr ? DEFS[f].n_addr : 400;
    s.eta_s = left * (uint32_t)P.n_probe * PROBE_MS / 1000;
  }
  return s;
}

const Learned &learned(int i) { return E.L[i < 0 || i >= E.nl ? 0 : i]; }

void sweepStart(int fam) {
  E = Engine();
  E.all = fam < 0;
  E.fam = E.all ? 0 : fam;
  E.fam_i = E.fam;
  ensure(E.fam);
  E.mode = P.addr_order.empty() ? IDLE : SWEEP;
}

void reacted() {
  if (E.mode != SWEEP) return;
  E.mode = NARROW;
  E.burst_i = -1;
  stepPos(-STEP_BACK);
  startBurst(addrAt(E.pos));
}

void narrowStep(int dir) {
  if (E.mode != NARROW) return;
  stepPos(dir);
  startBurst(addrAt(E.pos));
}

void resumeSweep() {
  if (E.mode != NARROW || E.done) return;
  E.burst_i = -1;
  E.mode = SWEEP;
}

void skipFamily() {
  if (E.mode != SWEEP || !E.all || E.fam + 1 >= N_DEFS) return;
  E.burst_i = -1;
  E.fam++; E.fam_i = E.fam; ensure(E.fam); E.pos = 0;
}

void learnStart(int fam, int addr) {
  bool all = E.all;
  E = Engine();
  E.all = all;
  E.fam = fam; E.fam_i = fam;
  ensure(fam);
  E.mode = LEARN;
  E.laddr = addr;
  for (size_t i = 0; i < P.addr_order.size(); i++) if (P.addr_order[i] == addr) E.pos = (int)i;
  preload();
}

void learnPick() {
  if (E.mode == NARROW) learnStart(E.fam, addrAt(E.pos));
}

void learnStep(int dir) {
  if (E.mode != LEARN) return;
  int n = (int)P.func_order.size();
  if (!n) return;
  E.fpos = (E.fpos + dir + n) % n;
  sendOne(E.laddr, P.func_order[E.fpos]);
}

void learnGoto(int func) {
  if (E.mode != LEARN) return;
  for (size_t i = 0; i < P.func_order.size(); i++) if (P.func_order[i] == func) E.fpos = (int)i;
  sendOne(E.laddr, P.func_order[E.fpos]);
}

bool learnName(uint8_t key, const char *custom) {
  if (E.mode != LEARN || key >= KEY__COUNT) return false;
  int fn = P.func_order[E.fpos];
  Learned *l = nullptr;
  for (int i = 0; i < E.nl; i++) if (E.L[i].func == fn) l = &E.L[i];
  if (!l) {
    if (E.nl >= MAX_LEARNED) return false;
    l = &E.L[E.nl++];
  }
  l->func = (uint8_t)fn;
  l->key = key;
  if (key) snprintf(l->name, sizeof(l->name), "%s", IR_KEY_LABELS[key]);
  else {
    // file-safe: printable ASCII, no colon (the file is "name: value" lines)
    size_t n = 0;
    for (const char *p = custom ? custom : ""; *p && n < sizeof(l->name) - 1; p++)
      if (*p >= 0x20 && *p < 0x7F && *p != ':' && *p != '#') l->name[n++] = *p;
    while (n && l->name[n - 1] == ' ') n--;
    l->name[n] = 0;
    if (!n) snprintf(l->name, sizeof(l->name), "F %d", fn);
  }
  return true;
}

void learnForget(int func) {
  int j = 0;
  for (int i = 0; i < E.nl; i++) if (E.L[i].func != func) E.L[j++] = E.L[i];
  E.nl = j;
}

void stop() { E = Engine(); }

void fileNames(char *brand, size_t bn, char *remote, size_t rn) {
  snprintf(brand, bn, "Discovered");
  const Def &d = DEFS[E.fam];
  if (d.kind == K_NECEXT) snprintf(remote, rn, "%s %d.%d", d.f.file_tag, E.laddr & 0xFF, E.laddr >> 8);
  else if (d.kind == K_BOSE) snprintf(remote, rn, "%s", d.f.file_tag);
  else snprintf(remote, rn, "%s %d", d.f.file_tag, E.laddr);
}

// The name a key gets in the file: one that the loader maps back to the same key.
const char *keyFileName(uint8_t key) {
  if (irdb::canonKey(IR_KEY_IDS[key]) == key) return IR_KEY_IDS[key];
  return IR_KEY_LABELS[key];
}

std::string fileText() {
  const Def &d = DEFS[E.fam];
  char b[160], at[40];
  addrText(E.fam, E.laddr, at, sizeof(at));
  std::string s = "Filetype: IR signals file\nVersion: 1\n";
  snprintf(b, sizeof(b), "# Found with the Long-Range Remote's Discover: %s, %s\n", d.f.name, at);
  s += b;
  for (int i = 0; i < E.nl; i++) {
    const Learned &l = E.L[i];
    const char *proto = d.flipper;
    unsigned a0 = E.laddr & 0xFF, a1 = 0, c0 = l.func, c1 = 0;
    switch (d.kind) {
      case K_NECEXT: a1 = (E.laddr >> 8) & 0xFF; c1 = inv8(l.func); break;
      case K_RC5:    if (l.func >= 64) { proto = "RC5X"; c0 = l.func & 0x3F; } break;
      default: break;
    }
    // a key name the loader maps back to the same key; a custom name as it is
    snprintf(b, sizeof(b), "#\nname: %s\ntype: parsed\nprotocol: %s\naddress: %02X %02X 00 00\ncommand: %02X %02X 00 00\n",
             l.key ? keyFileName(l.key) : l.name, proto, a0, a1, c0, c1);
    s += b;
  }
  return s;
}

void loop() {
  if (!H.send || !H.busy || !H.now) return;
  if (E.burst_i < 0) {
    if (E.mode != SWEEP) return;
    if (P.addr_order.empty()) {          // nothing to try in this family (no database): on to the next
      if (E.all && E.fam + 1 < N_DEFS) { E.fam++; E.fam_i = E.fam; ensure(E.fam); E.pos = 0; }
      else { E.mode = NARROW; E.done = true; }
      return;
    }
    startBurst(addrAt(E.pos));
  }
  uint32_t now = H.now();
  if (H.busy()) { E.quiet_since = 0; return; }
  if (!E.quiet_since) E.quiet_since = now ? now : 1;
  if (E.posted && ((int32_t)(now - E.last_post) < (int32_t)MIN_POST_MS || (int32_t)(now - E.quiet_since) < (int32_t)GAP_MS)) return;
  if (E.burst_i < E.burst_n) {
    int fn = E.burst_f[E.burst_i++];
    char label[64], at[32];
    addrText(E.fam, E.burst_addr, at, sizeof(at));
    snprintf(label, sizeof(label), "%s %s, function %d", DEFS[E.fam].f.file_tag, at, fn);
    H.send(makeCode(DEFS[E.fam].kind, E.burst_addr, fn), label);
    E.last_post = now; E.posted = true; E.quiet_since = 0;
    return;
  }
  E.burst_i = -1;                      // this address is done
  if (E.mode != SWEEP) return;
  if (E.pos + 1 < (int)P.addr_order.size()) { E.pos++; return; }
  if (E.all && E.fam + 1 < N_DEFS) { E.fam++; E.fam_i = E.fam; ensure(E.fam); E.pos = 0; return; }
  E.mode = NARROW;                     // the end, nothing reacted: stay on the last address, paused
  E.done = true;
}

}  // namespace discover

// ---------------------------------------------------------------------------
// Firmware glue
// ---------------------------------------------------------------------------
#ifdef ARDUINO
#include <Arduino.h>
#include "ir_tx.h"
#include "log.h"
#include "sd_store.h"
#include "settings.h"

namespace discover {
namespace {
uint32_t g_restart_at = 0;
uint32_t nowMs() { return millis(); }
void sendTap(const IrCodeParams &p, const char *label) { irtx::tapParams(p, label); }
bool txBusy() { return irtx::isSending(); }
}  // namespace

void begin() { setHooks({sendTap, txBusy, nowMs}); }

bool saveAndRestart(char *err, size_t en) {
  State s = state();
  if (s.mode != LEARN || s.n_learned == 0) { snprintf(err, en, "no keys named yet"); return false; }
  char brand[24], remote[40], rel[96];
  fileNames(brand, sizeof(brand), remote, sizeof(remote));
  snprintf(rel, sizeof(rel), "%s/%s.ir", brand, remote);
  if (!sdstore::saveUserRemote(rel, fileText(), err, en)) return false;
  settings::setActiveByName(brand, remote);     // becomes the device in use after the restart
  logf("Discover: saved %d keys to %s/%s - restarting to load it\n", s.n_learned, SD_USER_DIR, rel);
  g_restart_at = millis() + 1200;               // time for the screen and the web page to say so
  if (!g_restart_at) g_restart_at = 1;
  return true;
}

bool restartPending() { return g_restart_at != 0; }

void glueLoop() {
  if (g_restart_at && (int32_t)(millis() - g_restart_at) >= 0) ESP.restart();
}
}  // namespace discover
#endif
