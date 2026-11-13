// ir_db.cpp - accessors for the IR database (irdb.bin on the SD card, plus your own .ir files).
// No Arduino dependencies: tools/test_all_codes.cpp compiles this file on a PC.
//
// Only the index of irdb.bin (names, brands, groups, remotes) is kept in memory. The codes, the raw
// signals and the key index stay in the file and are read through a small page cache (readA), so the
// database can be far bigger than the PSRAM. Reads are serialised by a mutex: the transmit task
// fetches raw signals while the main loop browses.
#include "ir_db.h"
#include "ir_keys.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static_assert(sizeof(IrBrand) == 8, "IrBrand layout must match tools/build_db.py");
static_assert(sizeof(IrGroup) == 8, "IrGroup layout must match tools/build_db.py");
static_assert(sizeof(IrProfile) == 20, "IrProfile layout must match tools/build_db.py");
static_assert(sizeof(IrCode) == 12, "IrCode layout must match tools/build_db.py");
static_assert(sizeof(IrRawSig) == 16, "IrRawSig layout must match tools/build_db.py");

namespace irdb {
namespace {

// One set of tables. For the main database (A) only the index tables are pointers into memory; its
// codes / raw signals are read from the file (sections below). Your own remotes (U) are all in memory.
struct View {
  const char *str = nullptr;      uint32_t str_len = 0;
  const IrBrand *brands = nullptr; int nb = 0;
  const IrGroup *groups = nullptr; int ng = 0;
  const uint16_t *group_brands = nullptr; int ngb = 0;
  const IrProfile *profiles = nullptr; int np = 0;
  const IrCode *codes = nullptr; int nc = 0;            // codes: U only
  const IrRawSig *raw = nullptr; int nr = 0;            // U only
  const uint8_t *raw_data = nullptr; uint32_t raw_data_len = 0;
  const uint16_t *raw_table = nullptr; uint32_t raw_table_len = 0;
};
View A;            // main database
View U;            // your own remotes (indices continue after A's)
char g_source[48] = "none";

// Where the main database's on-card sections are (file offsets), and how to read them
struct Store {
  Reader rd = {nullptr, nullptr, 0};
  const uint8_t *mem = nullptr;        // whole image in memory (PC tools) instead of a reader
  uint32_t size = 0;
  uint32_t off_codes = 0, off_raw = 0, off_raw_data = 0, off_raw_table = 0, off_keydir = 0, off_keyidx = 0;
  uint32_t n_keyidx = 0;
  void *ram = nullptr;                 // malloc'ed copy of the index sections (reader mode)
} S;

// ---- page cache ----
const uint32_t PG = 4096;
const int NPG = 48;                    // 192 KB
struct Page { uint32_t no = UINT32_MAX; uint32_t tick = 0; };
Page g_pg[NPG];
uint8_t *g_pages = nullptr;
uint32_t g_tick = 0;
int g_last = -1;
std::mutex g_mx;

void cacheReset() {
  for (auto &p : g_pg) p = Page();
  g_last = -1;
}

// caller holds g_mx
const uint8_t *page(uint32_t no) {
  if (g_last >= 0 && g_pg[g_last].no == no) { g_pg[g_last].tick = ++g_tick; return g_pages + (size_t)g_last * PG; }
  int victim = 0;
  for (int i = 0; i < NPG; i++) {
    if (g_pg[i].no == no) { g_last = i; g_pg[i].tick = ++g_tick; return g_pages + (size_t)i * PG; }
    if (g_pg[i].tick < g_pg[victim].tick) victim = i;
  }
  if (!g_pages) g_pages = (uint8_t *)malloc((size_t)NPG * PG);
  if (!g_pages || !S.rd.read) return nullptr;
  uint64_t off = (uint64_t)no * PG;
  if (off >= S.size) return nullptr;
  uint32_t n = (uint32_t)std::min<uint64_t>(PG, S.size - off);
  g_pg[victim].no = UINT32_MAX;
  if (!S.rd.read(S.rd.ctx, (uint32_t)off, g_pages + (size_t)victim * PG, n)) { g_last = -1; return nullptr; }
  if (n < PG) memset(g_pages + (size_t)victim * PG + n, 0, PG - n);
  g_pg[victim].no = no; g_pg[victim].tick = ++g_tick; g_last = victim;
  return g_pages + (size_t)victim * PG;
}

// Bytes of the main database file. false (buffer zeroed) if they can't be read (card pulled out).
bool readA(uint32_t off, void *buf, uint32_t len) {
  if ((uint64_t)off + len > S.size) { memset(buf, 0, len); return false; }
  if (S.mem) { memcpy(buf, S.mem + off, len); return true; }
  std::lock_guard<std::mutex> lk(g_mx);
  uint8_t *out = (uint8_t *)buf;
  while (len) {
    const uint8_t *p = page(off / PG);
    uint32_t in = off % PG, n = std::min(len, PG - in);
    if (!p) { memset(out, 0, len); return false; }
    memcpy(out, p + in, n);
    out += n; off += n; len -= n;
  }
  return true;
}

// User tables under construction / in use (std::vector: on the ESP32-S3 these large blocks land in
// PSRAM through the normal allocator)
struct UserTables {
  std::string str;
  std::vector<IrBrand> brands;
  std::vector<IrProfile> profiles;
  std::vector<IrCode> codes;
  std::vector<IrRawSig> raw;
  std::vector<uint8_t> raw_data;
  std::vector<uint16_t> raw_table;
  std::vector<uint8_t> used_keys;      // per profile: canonical keys already taken
} UT;
const int USER_MAX_PROFILES = 2000, USER_MAX_CODES = 60000;

inline const IrBrand &brandRow(int b) { return b < A.nb ? A.brands[b] : U.brands[b - A.nb]; }
inline const IrProfile &profRow(int p) { return p < A.np ? A.profiles[p] : U.profiles[p - A.np]; }
inline const char *strA(uint32_t off) { return off < A.str_len ? A.str + off : ""; }
inline const char *strU(uint32_t off) { return off < U.str_len ? U.str + off : ""; }

int totalBrands() { return A.nb + U.nb; }
int totalProfiles() { return A.np + U.np; }
int totalCodes() { return A.nc + U.nc; }

void setErr(char *err, size_t n, const char *msg) {
  if (err && n) { strncpy(err, msg, n - 1); err[n - 1] = 0; }
}

uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

const IrCode kNullCode = {0, 0, 0, 0, 0, 0};   // name, d, s, f, proto, key
const IrRawSig kNullRaw = {0, 0, 38000, 0, 1, 33};

// A code row of the main database is only trusted after this (the file isn't checked row by row at
// start-up any more: that would mean reading all of it).
inline void sanitize(IrCode &c) {
  if (c.proto >= IRP__COUNT || c.key >= KEY__COUNT || c.name >= A.str_len) c = kNullCode;
}

// profileFindKey(): the keys of the remote asked about last
int g_km_prof = -1;
int32_t g_km[KEY__COUNT];

const int N_SECTIONS = 11;
enum { SEC_STR, SEC_BRANDS, SEC_GROUPS, SEC_GROUP_BRANDS, SEC_PROFILES, SEC_CODES, SEC_RAW, SEC_RAW_DATA,
       SEC_RAW_TABLE, SEC_KEYDIR, SEC_KEYIDX };
struct KeyDir { uint32_t first, count; };
struct KeyEnt { uint32_t code; uint16_t profile; uint16_t pad; uint32_t sig_lo, sig_hi; };

}  // namespace

static_assert(sizeof(KeyEnt) == 16, "KeyEnt layout must match tools/build_db.py");

// =================================================================================== image loading
bool checkHeader(const uint8_t *img, size_t len, char *err, size_t en) {
  if (len < 20 || memcmp(img, "IRDB", 4) != 0) { setErr(err, en, "not an IRDB file"); return false; }
  uint32_t ver = rd32(img + 4), schema = rd32(img + 8);
  if (ver != IRDB_BIN_VERSION) {
    char b[64]; snprintf(b, sizeof(b), "file version %u, firmware reads %u", (unsigned)ver, IRDB_BIN_VERSION);
    setErr(err, en, b); return false;
  }
  if (schema != IRDB_SCHEMA) {
    setErr(err, en, "built for a different firmware (key/protocol numbering differs)");
    return false;
  }
  return true;
}

// Open a database: header and index tables are read (and checked) now, the rest on demand.
static bool openStore(const Reader &rd, const uint8_t *mem, uint32_t size, char *err, size_t en) {
  if (U.np) { setErr(err, en, "load the main database before your own remotes"); return false; }
  uint8_t hdr[20 + N_SECTIONS * 8];
  auto rdraw = [&](uint32_t off, void *buf, uint32_t n) -> bool {
    if ((uint64_t)off + n > size) return false;
    if (mem) { memcpy(buf, mem + off, n); return true; }
    return rd.read && rd.read(rd.ctx, off, buf, n);
  };
  if (size < sizeof(hdr) || !rdraw(0, hdr, sizeof(hdr))) { setErr(err, en, "file too short"); return false; }
  if (!checkHeader(hdr, sizeof(hdr), err, en)) return false;
  if (rd32(hdr + 16) != (uint32_t)N_SECTIONS) { setErr(err, en, "unexpected section count"); return false; }
  static const uint32_t ESZ[N_SECTIONS] = {1, sizeof(IrBrand), sizeof(IrGroup), 2, sizeof(IrProfile), sizeof(IrCode),
                                           sizeof(IrRawSig), 1, 2, sizeof(KeyDir), sizeof(KeyEnt)};
  uint32_t off[N_SECTIONS], cnt[N_SECTIONS];
  for (int i = 0; i < N_SECTIONS; i++) {
    off[i] = rd32(hdr + 20 + 8 * i);
    cnt[i] = rd32(hdr + 24 + 8 * i);
    if ((off[i] & 3) || (uint64_t)off[i] + (uint64_t)cnt[i] * ESZ[i] > size) {
      setErr(err, en, "section outside the file (truncated?)"); return false;
    }
  }
  // the index: sections 0..4, one after the other at the start of the file
  uint32_t i0 = off[SEC_STR], i1 = off[SEC_PROFILES] + cnt[SEC_PROFILES] * (uint32_t)sizeof(IrProfile);
  for (int i = SEC_STR; i < SEC_PROFILES; i++)
    if (off[i + 1] < off[i] || off[i + 1] > i1) { setErr(err, en, "bad section order"); return false; }
  if (i1 <= i0 || i1 - i0 > 64u * 1024 * 1024) { setErr(err, en, "bad index size"); return false; }
  void *ram = nullptr;
  const uint8_t *base;                          // base + file offset = the index tables
  if (mem) {
    if (((uintptr_t)mem & 3) != 0) { setErr(err, en, "image not 4-byte aligned"); return false; }
    base = mem;
  } else {
    ram = malloc(i1 - i0);
    if (!ram) { setErr(err, en, "not enough memory for the index (OPI PSRAM enabled in the board settings?)"); return false; }
    for (uint32_t done = 0; done < i1 - i0;) {
      uint32_t n = std::min<uint32_t>(32768, i1 - i0 - done);
      if (!rd.read(rd.ctx, i0 + done, (uint8_t *)ram + done, n)) { free(ram); setErr(err, en, "read error"); return false; }
      done += n;
    }
    base = (const uint8_t *)ram - i0;
  }
  View v;
  v.str = (const char *)base + off[SEC_STR]; v.str_len = cnt[SEC_STR];
  v.brands = (const IrBrand *)(base + off[SEC_BRANDS]); v.nb = cnt[SEC_BRANDS];
  v.groups = (const IrGroup *)(base + off[SEC_GROUPS]); v.ng = cnt[SEC_GROUPS];
  v.group_brands = (const uint16_t *)(base + off[SEC_GROUP_BRANDS]); v.ngb = cnt[SEC_GROUP_BRANDS];
  v.profiles = (const IrProfile *)(base + off[SEC_PROFILES]); v.np = cnt[SEC_PROFILES];
  v.nc = cnt[SEC_CODES]; v.nr = cnt[SEC_RAW];
  v.raw_data_len = cnt[SEC_RAW_DATA]; v.raw_table_len = cnt[SEC_RAW_TABLE];
  // Every index must stay inside its table, so a damaged file can't crash the remote later.
  const char *bad = nullptr;
  if (v.str_len == 0 || v.str[v.str_len - 1] != 0) bad = "bad string pool";
  if (v.np > 65535 || v.nb > 65535 || v.nc < 0) bad = "too many remotes";
  for (int b = 0; b < v.nb && !bad; b++) {
    const IrBrand &r = v.brands[b];
    if (r.name >= v.str_len || r.first_prof + r.n_prof > v.np) bad = "bad brand table";
  }
  for (int g = 0; g < v.ng && !bad; g++) {
    const IrGroup &r = v.groups[g];
    if (r.name >= v.str_len || r.first + r.count > v.ngb) bad = "bad group table";
  }
  for (int i = 0; i < v.ngb && !bad; i++) if (v.group_brands[i] >= v.nb) bad = "bad group table";
  for (int p = 0; p < v.np && !bad; p++) {
    const IrProfile &r = v.profiles[p];
    if (r.name >= v.str_len || r.brand >= v.nb || r.cat >= IR_NUM_CATS || (uint64_t)r.first_code + r.n_codes > (uint64_t)v.nc)
      bad = "bad remote table";
  }
  if (!bad && cnt[SEC_KEYDIR] != (uint32_t)KEY__COUNT) bad = "bad key index";
  if (bad) { free(ram); setErr(err, en, bad); return false; }
  {
    std::lock_guard<std::mutex> lk(g_mx);
    cacheReset();
    free(S.ram);
    S = Store();
    S.rd = rd; S.mem = mem; S.size = size; S.ram = ram;
    S.off_codes = off[SEC_CODES]; S.off_raw = off[SEC_RAW]; S.off_raw_data = off[SEC_RAW_DATA];
    S.off_raw_table = off[SEC_RAW_TABLE]; S.off_keydir = off[SEC_KEYDIR]; S.off_keyidx = off[SEC_KEYIDX];
    S.n_keyidx = cnt[SEC_KEYIDX];
  }
  A = v;
  g_km_prof = -1;
  return true;
}

bool useFile(const Reader &rd, char *err, size_t en) {
  if (!rd.read) { setErr(err, en, "no reader"); return false; }
  return openStore(rd, nullptr, rd.size, err, en);
}

bool useImage(const uint8_t *img, size_t len, char *err, size_t en) {
  Store oldS = S; View oldA = A;
  if (!openStore(Reader{nullptr, nullptr, 0}, img, (uint32_t)len, err, en)) return false;
  if (!verify(err, en)) {                       // in memory the whole file can be checked at once
    if (oldS.ram) { S = Store(); A = View(); }  // (its index was freed: nothing to go back to)
    else { S = oldS; A = oldA; }
    g_km_prof = -1;
    return false;
  }
  return true;
}

bool verify(char *err, size_t en) {
  const int CH = 512;
  std::vector<IrCode> buf(CH);
  for (int c = 0; c < A.nc; c += CH) {
    int n = std::min(CH, A.nc - c);
    if (!readA(S.off_codes + (uint32_t)c * sizeof(IrCode), buf.data(), n * sizeof(IrCode))) { setErr(err, en, "read error"); return false; }
    for (int i = 0; i < n; i++) {
      const IrCode &r = buf[i];
      if (r.name >= A.str_len || r.proto >= IRP__COUNT || r.key >= KEY__COUNT ||
          (r.proto == IRP_RAW && irRawIndex(r.d, r.s) >= (uint32_t)A.nr)) { setErr(err, en, "bad code table"); return false; }
    }
  }
  for (int r = 0; r < A.nr; r++) {
    IrRawSig x;
    if (!readA(S.off_raw + (uint32_t)r * sizeof(IrRawSig), &x, sizeof(x))) { setErr(err, en, "read error"); return false; }
    uint32_t bytes = x.table_len <= 16 ? (x.count + 1u) / 2 : x.count;
    if (x.count == 0 || (uint64_t)x.data_off + bytes > A.raw_data_len ||
        (uint64_t)x.table_off + x.table_len > A.raw_table_len || x.table_len == 0) { setErr(err, en, "bad raw table"); return false; }
  }
  for (int k = 0; k < KEY__COUNT; k++) {
    KeyDir d;
    if (!readA(S.off_keydir + k * sizeof(KeyDir), &d, sizeof(d)) || (uint64_t)d.first + d.count > S.n_keyidx) { setErr(err, en, "bad key index"); return false; }
  }
  for (uint32_t i = 0; i < S.n_keyidx; i++) {
    KeyEnt e;
    if (!readA(S.off_keyidx + i * sizeof(KeyEnt), &e, sizeof(e)) || e.code >= (uint32_t)A.nc || e.profile >= A.np) { setErr(err, en, "bad key index"); return false; }
  }
  return true;
}

const char *sourceName() { return g_source; }
void setSourceName(const char *s) { strncpy(g_source, s, sizeof(g_source) - 1); g_source[sizeof(g_source) - 1] = 0; }

// =================================================================================== your own remotes
void userBegin() {
  UT = UserTables();
  U = View();
  g_km_prof = -1;
}

static uint32_t userStr(const char *s) {
  uint32_t off = (uint32_t)UT.str.size();
  UT.str.append(s);
  UT.str.push_back(0);
  return off;
}

bool userProfile(const char *brand, const char *name, uint8_t cat) {
  if ((int)UT.profiles.size() >= USER_MAX_PROFILES || A.np + (int)UT.profiles.size() >= 65535) return false;
  // brands are merged by name (files in the same folder = one brand)
  int b = -1;
  for (size_t i = 0; i < UT.brands.size(); i++) {
    if (strcasecmp(UT.str.c_str() + UT.brands[i].name, brand) == 0) { b = (int)i; break; }
  }
  if (b < 0) {
    IrBrand nb;
    nb.name = userStr(brand); nb.first_prof = 0; nb.n_prof = 0;
    UT.brands.push_back(nb);
    b = (int)UT.brands.size() - 1;
  }
  IrProfile p;
  p.brand = (uint16_t)(A.nb + b);
  p.name = userStr(name);
  p.cat = cat < IR_NUM_CATS ? cat : (uint8_t)(IR_NUM_CATS - 1);
  p.src = SRC_USER;
  p.first_code = (uint32_t)(A.nc + UT.codes.size());
  p.n_codes = 0;
  UT.profiles.push_back(p);
  UT.used_keys.assign(KEY__COUNT, 0);
  return true;
}

static bool userAdd(const char *name, uint8_t proto, uint16_t d, uint16_t s, uint16_t f) {
  if (UT.profiles.empty() || (int)UT.codes.size() >= USER_MAX_CODES) return false;
  IrProfile &p = UT.profiles.back();
  if (p.n_codes >= 65535) return false;
  IrCode c;
  c.name = userStr(name);
  c.d = d; c.s = s; c.f = f; c.proto = proto;
  uint8_t k = canonKey(name);
  if (k && !UT.used_keys[k]) { UT.used_keys[k] = 1; c.key = k; }    // first code wins a key slot
  else c.key = 0;
  UT.codes.push_back(c);
  p.n_codes++;
  return true;
}

bool userCode(const char *name, const IrCodeParams &p) {
  if (p.proto == IRP_RAW || p.proto >= IRP__COUNT) return false;
  return userAdd(name, p.proto, p.d, p.s, p.f);
}

bool userRawCode(const char *name, const uint16_t *dur, int count, uint32_t freq, uint8_t duty) {
  if (count < 2 || count > 2000) return false;
  // Compress like the generator: one table of distinct durations per signal (clustered within 12 %
  // while there are more than 255), 4-bit indices when <= 16 values, else 8-bit.
  std::vector<uint16_t> vals(dur, dur + count);
  std::vector<uint16_t> uniq(vals);
  std::sort(uniq.begin(), uniq.end());
  uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
  std::vector<std::vector<uint16_t>> cl;
  for (uint16_t v : uniq) {
    if (!cl.empty() && v && cl.back()[0] && v <= std::max<uint32_t>(cl.back()[0] * 112u / 100u, cl.back()[0] + 40u))
      cl.back().push_back(v);
    else
      cl.push_back({v});
  }
  while (cl.size() > 255) {                               // merge the closest neighbours
    size_t best = 0; uint32_t gap = UINT32_MAX;
    for (size_t i = 0; i + 1 < cl.size(); i++) {
      uint32_t g = cl[i + 1].front() - cl[i].back();
      if (g < gap) { gap = g; best = i; }
    }
    cl[best].insert(cl[best].end(), cl[best + 1].begin(), cl[best + 1].end());
    cl.erase(cl.begin() + best + 1);
  }
  IrRawSig r;
  r.table_off = (uint32_t)UT.raw_table.size();
  r.table_len = (uint8_t)cl.size();
  for (auto &c : cl) {
    uint32_t sum = 0;
    for (uint16_t v : c) sum += v;
    UT.raw_table.push_back((uint16_t)((sum + c.size() / 2) / c.size()));
  }
  auto idxOf = [&](uint16_t v) -> uint8_t {
    for (size_t i = 0; i < cl.size(); i++)
      if (v >= cl[i].front() && v <= cl[i].back()) return (uint8_t)i;
    return 0;
  };
  r.data_off = (uint32_t)UT.raw_data.size();
  r.count = (uint16_t)count;
  r.freq = freq ? freq : 38000;
  r.duty = duty ? duty : 33;
  if (cl.size() <= 16) {
    for (int i = 0; i < count; i += 2)
      UT.raw_data.push_back(idxOf(vals[i]) | ((i + 1 < count ? idxOf(vals[i + 1]) : 0) << 4));
  } else {
    for (int i = 0; i < count; i++) UT.raw_data.push_back(idxOf(vals[i]));
  }
  UT.raw.push_back(r);
  uint32_t idx = (uint32_t)(A.nr + UT.raw.size() - 1);      // raw index: d = low 16 bits, s = high
  return userAdd(name, IRP_RAW, (uint16_t)idx, (uint16_t)(idx >> 16), 0);
}

void userFinish() {
  // drop remotes that ended up without a single usable code
  std::vector<IrProfile> keep;
  for (auto &p : UT.profiles) if (p.n_codes) keep.push_back(p);
  UT.profiles.swap(keep);
  // brands: profile ranges (profiles were added brand by brand only if files were sorted, so sort
  // the profiles by brand - codes stay where they are, profiles just point at them)
  std::stable_sort(UT.profiles.begin(), UT.profiles.end(),
                   [](const IrProfile &a, const IrProfile &b) { return a.brand < b.brand; });
  for (auto &b : UT.brands) { b.n_prof = 0; b.first_prof = 0; }
  for (size_t i = 0; i < UT.profiles.size(); i++) {
    IrBrand &b = UT.brands[UT.profiles[i].brand - A.nb];
    if (b.n_prof == 0) b.first_prof = (uint16_t)(A.np + i);
    b.n_prof++;
  }
  // codeProfile() binary-searches profiles by first_code: keep user codes in profile order
  std::vector<IrCode> codes;
  codes.reserve(UT.codes.size());
  for (auto &p : UT.profiles) {
    uint32_t old = p.first_code - A.nc;
    p.first_code = (uint32_t)(A.nc + codes.size());
    for (int i = 0; i < p.n_codes; i++) codes.push_back(UT.codes[old + i]);
  }
  UT.codes.swap(codes);
  U.str = UT.str.c_str(); U.str_len = (uint32_t)UT.str.size();
  U.brands = UT.brands.data(); U.nb = (int)UT.brands.size();
  U.profiles = UT.profiles.data(); U.np = (int)UT.profiles.size();
  U.codes = UT.codes.data(); U.nc = (int)UT.codes.size();
  U.raw = UT.raw.data(); U.nr = (int)UT.raw.size();
  U.raw_data = UT.raw_data.data(); U.raw_data_len = (uint32_t)UT.raw_data.size();
  U.raw_table = UT.raw_table.data(); U.raw_table_len = (uint32_t)UT.raw_table.size();
  if (U.np == 0) { U.nb = 0; }
  g_km_prof = -1;
}

int numUserProfiles() { return U.np; }

// =================================================================================== queries
int numBrands()   { return totalBrands(); }
int numProfiles() { return totalProfiles(); }
int numCodes()    { return totalCodes(); }
int numRaw()      { return A.nr + U.nr; }

static inline bool okB(int b) { return b >= 0 && b < totalBrands(); }
static inline bool okP(int p) { return p >= 0 && p < totalProfiles(); }
static inline bool okC(int c) { return c >= 0 && c < totalCodes(); }

const char *brandName(int b) {
  if (!okB(b)) return "";
  return b < A.nb ? strA(A.brands[b].name) : strU(U.brands[b - A.nb].name);
}
int brandFirstProfile(int b)      { return okB(b) ? brandRow(b).first_prof : 0; }
int brandProfileCount(int b)      { return okB(b) ? brandRow(b).n_prof : 0; }

const char *profileName(int p) {
  if (!okP(p)) return "";
  return p < A.np ? strA(A.profiles[p].name) : strU(U.profiles[p - A.np].name);
}
int profileBrand(int p)           { return okP(p) ? profRow(p).brand : 0; }
int profileCategory(int p)        { return okP(p) ? profRow(p).cat : 0; }
int profileSource(int p)          { return okP(p) ? profRow(p).src : 0; }
bool profileFromIrdb(int p)       { return okP(p) && profRow(p).src == SRC_IRDB; }
int profileFirstCode(int p)       { return okP(p) ? (int)profRow(p).first_code : 0; }
int profileCodeCount(int p)       { return okP(p) ? profRow(p).n_codes : 0; }

int readCodes(int first, IrCode *out, int n) {
  if (first < 0 || n <= 0) return 0;
  n = std::min(n, totalCodes() - first);
  if (n <= 0) return 0;
  int done = 0;
  if (first < A.nc) {
    int na = std::min(n, A.nc - first);
    readA(S.off_codes + (uint32_t)first * sizeof(IrCode), out, (uint32_t)na * sizeof(IrCode));   // zeroes = null codes on failure
    for (int i = 0; i < na; i++) sanitize(out[i]);
    done = na;
  }
  for (; done < n; done++) out[done] = U.codes[first + done - A.nc];
  return n;
}

IrCode code(int c) {
  IrCode x = kNullCode;
  if (okC(c)) readCodes(c, &x, 1);
  return x;
}

int profileFindKey(int p, uint8_t key) {
  if (!okP(p) || key == 0 || key >= KEY__COUNT) return -1;
  if (p != g_km_prof) {                      // read the remote's codes once, then answer from the table
    for (auto &k : g_km) k = -1;
    int c0 = profileFirstCode(p), n = profileCodeCount(p);
    IrCode buf[64];
    for (int i = 0; i < n; i += 64) {
      int m = readCodes(c0 + i, buf, std::min(64, n - i));
      for (int j = 0; j < m; j++)
        if (buf[j].key && g_km[buf[j].key] < 0) g_km[buf[j].key] = c0 + i + j;
    }
    g_km_prof = p;
  }
  return g_km[key];
}

const char *codeName(int c) {
  if (!okC(c)) return "";
  return c < A.nc ? strA(code(c).name) : strU(U.codes[c - A.nc].name);
}

int codeProfile(int c) {
  int lo = 0, hi = totalProfiles() - 1;
  if (hi < 0) return 0;
  while (lo < hi) {                       // last profile whose first_code <= c
    int mid = (lo + hi + 1) / 2;
    if ((int)profRow(mid).first_code <= c) lo = mid; else hi = mid - 1;
  }
  return lo;
}

IrCodeParams codeParams(int c) {
  IrCode x = code(c);
  IrCodeParams p;
  p.proto = x.proto; p.d = x.d; p.s = x.s; p.f = x.f;
  return p;
}

IrRawSig raw(int r) {
  IrRawSig x = kNullRaw;
  if (r >= 0 && r < A.nr) {
    if (!readA(S.off_raw + (uint32_t)r * sizeof(IrRawSig), &x, sizeof(x))) return kNullRaw;
    uint32_t bytes = x.table_len <= 16 ? (x.count + 1u) / 2 : x.count;
    if (x.count == 0 || x.table_len == 0 || (uint64_t)x.data_off + bytes > A.raw_data_len ||
        (uint64_t)x.table_off + x.table_len > A.raw_table_len) return kNullRaw;
  } else if (r >= A.nr && r < A.nr + U.nr) {
    x = U.raw[r - A.nr];
  }
  return x;
}

int rawRead(int r, uint16_t *out, int max) {
  IrRawSig x = raw(r);
  if (x.count == 0 || !out) return 0;
  const bool user = r >= A.nr;
  int n = std::min<int>(x.count, max);
  uint16_t table[256];
  if (user) memcpy(table, U.raw_table + x.table_off, x.table_len * 2);
  else if (!readA(S.off_raw_table + x.table_off * 2, table, x.table_len * 2u)) return 0;
  const bool nib = x.table_len <= 16;
  uint8_t buf[256];
  for (int i = 0; i < n;) {
    int per = nib ? 512 : 256;                               // samples per buffer
    int m = std::min(per, n - i);
    uint32_t bytes = nib ? (m + 1u) / 2 : (uint32_t)m, boff = x.data_off + (nib ? i / 2 : i);
    if (user) memcpy(buf, U.raw_data + boff, bytes);
    else if (!readA(S.off_raw_data + boff, buf, bytes)) return 0;
    for (int k = 0; k < m; k++) {
      uint8_t idx = nib ? ((k & 1) ? (buf[k / 2] >> 4) : (buf[k / 2] & 0x0F)) : buf[k];
      if (idx >= x.table_len) idx = 0;
      out[i + k] = table[idx];
    }
    i += m;
  }
  return n;
}

// ---- signal identity -----------------------------------------------------------------------------
// A 64-bit id of what goes on air, the same as tools/build_db.py stores in the key index: FNV-1a of
// protocol + D + S + F, or for a raw signal CRC-32 (high half) and Adler-32 (low half) of its
// carrier frequency and durations.
// NEC1 and NEC2 differ only in how a HELD key repeats; a single press is the same frame.
static uint64_t fnv(const uint8_t *p, size_t n, uint64_t h = 0xcbf29ce484222325ull) {
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
  return h;
}

static uint64_t codeSig(const IrCode &x) {
  if (x.proto != IRP_RAW) {
    uint8_t pr = x.proto == IRP_NEC2 ? (uint8_t)IRP_NEC : x.proto;
    uint8_t b[7] = {pr, (uint8_t)x.d, (uint8_t)(x.d >> 8), (uint8_t)x.s, (uint8_t)(x.s >> 8), (uint8_t)x.f, (uint8_t)(x.f >> 8)};
    return fnv(b, 7);
  }
  int r = (int)irRawIndex(x.d, x.s);
  IrRawSig rs = raw(r);
  std::vector<uint16_t> t(rs.count);
  int n = rawRead(r, t.data(), rs.count);
  uint32_t crc = 0xFFFFFFFFu, a1 = 1, a2 = 0;
  auto put = [&](uint8_t b) {
    crc ^= b;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    a1 = (a1 + b) % 65521; a2 = (a2 + a1) % 65521;
  };
  put(0xFF);
  for (int k = 0; k < 4; k++) put((uint8_t)(rs.freq >> (8 * k)));
  for (int i = 0; i < n; i++) { put((uint8_t)t[i]); put((uint8_t)(t[i] >> 8)); }
  return ((uint64_t)(crc ^ 0xFFFFFFFFu) << 32) | ((a2 << 16) | a1);
}

bool sameSignal(int a, int b) {
  if (a == b) return true;
  IrCode x = code(a), y = code(b);
  if (x.proto == IRP_RAW && y.proto == IRP_RAW && x.d == y.d && x.s == y.s) return true;
  if ((x.proto == IRP_RAW) != (y.proto == IRP_RAW)) return false;
  return codeSig(x) == codeSig(y);
}

int numGroups() { return A.ng; }
const char *groupName(int g) { return (g >= 0 && g < A.ng) ? strA(A.groups[g].name) : ""; }
int groupBrandCount(int g) { return (g >= 0 && g < A.ng) ? A.groups[g].count : 0; }
int groupBrand(int g, int i) {
  if (g < 0 || g >= A.ng || i < 0 || i >= A.groups[g].count) return -1;
  return A.group_brands[A.groups[g].first + i];
}

// Calls fn(profile) for every profile the filter covers, in database order.
template <typename F>
static void forEachProfile(const ScanFilter &flt, F fn) {
  auto brandRange = [&](int b) {
    int p0 = brandFirstProfile(b), n = brandProfileCount(b);
    for (int i = 0; i < n; i++) {
      if (flt.category < 0 || profileCategory(p0 + i) == flt.category) fn(p0 + i);
    }
  };
  if (flt.brand >= 0 && flt.brand < totalBrands()) {
    brandRange(flt.brand);
  } else if (flt.group >= 0 && flt.group < A.ng) {
    for (int i = 0; i < groupBrandCount(flt.group); i++) brandRange(groupBrand(flt.group, i));
  } else {
    for (int b = 0; b < totalBrands(); b++) brandRange(b);
  }
}

int countProfiles(const ScanFilter &flt) {
  int n = 0;
  forEachProfile(flt, [&](int) { n++; });
  return n;
}

int buildScan(const ScanFilter &flt, uint8_t key, ScanCand *out, int max_out) {
  int found = 0;
  if (key == 0 || key >= KEY__COUNT) return 0;
  std::unordered_map<uint64_t, int> seen;                 // signal -> index in out[]
  auto take = [&](int c, int p, uint64_t sig) {
    auto it = seen.find(sig);
    if (it != seen.end()) {                               // same signal already queued: just count it
      ScanCand &o = out[it->second];
      if (o.shared < 0xFFFF) o.shared++;
      if (profileCodeCount(p) > profileCodeCount(o.profile)) { o.profile = p; o.code = c; }
      return;
    }
    if (found < max_out) {
      out[found].code = c; out[found].profile = p; out[found].shared = 1;
      seen[sig] = found++;
    }
  };
  // ---- main database: the key index lists (code, remote, signal) for every remote that has the key
  std::vector<bool> brand_ok;                             // group filter: which brands
  if (flt.brand < 0 && flt.group >= 0 && flt.group < A.ng) {
    brand_ok.assign(A.nb, false);
    for (int i = 0; i < groupBrandCount(flt.group); i++) brand_ok[groupBrand(flt.group, i)] = true;
  }
  auto inFilter = [&](int p) {
    const IrProfile &r = A.profiles[p];
    if (flt.category >= 0 && r.cat != flt.category) return false;
    if (flt.brand >= 0) return (int)r.brand == flt.brand;
    if (!brand_ok.empty()) return (bool)brand_ok[r.brand];
    return true;
  };
  std::vector<bool> has;                                  // Power: remotes that have it (others may have "On")
  auto pass = [&](uint8_t k, bool mark, bool skip_marked) {
    KeyDir d;
    if (!readA(S.off_keydir + k * sizeof(KeyDir), &d, sizeof(d)) || (uint64_t)d.first + d.count > S.n_keyidx) return;
    KeyEnt buf[128];
    for (uint32_t i = 0; i < d.count;) {
      uint32_t m = std::min<uint32_t>(128, d.count - i);
      if (!readA(S.off_keyidx + (d.first + i) * sizeof(KeyEnt), buf, m * sizeof(KeyEnt))) return;
      for (uint32_t j = 0; j < m; j++) {
        const KeyEnt &e = buf[j];
        if (e.profile >= A.np || e.code >= (uint32_t)A.nc) continue;
        if (skip_marked && has[e.profile]) continue;
        if (mark) has[e.profile] = true;
        if (inFilter(e.profile)) take((int)e.code, e.profile, ((uint64_t)e.sig_hi << 32) | e.sig_lo);
      }
      i += m;
    }
  };
  if (A.np && !(flt.brand >= A.nb)) {
    if (key == KEY_POWER) {
      has.assign(A.np, false);
      pass(KEY_POWER, true, false);
      pass(KEY_POWER_ON, true, true);                     // only a discrete "On"
      pass(KEY_POWER_OFF, false, true);                   // only "Off" (many air conditioners)
    } else {
      pass(key, false, false);
    }
  }
  // ---- your own remotes (in memory)
  if (U.np && (flt.brand < 0 || flt.brand >= A.nb) && !(flt.brand < 0 && flt.group >= 0)) {
    for (int p = A.np; p < totalProfiles(); p++) {
      const IrProfile &r = profRow(p);
      if (flt.brand >= 0 && (int)r.brand != flt.brand) continue;
      if (flt.category >= 0 && r.cat != flt.category) continue;
      int c = profileFindKey(p, key);
      if (c < 0 && key == KEY_POWER) c = profileFindKey(p, KEY_POWER_ON);
      if (c < 0 && key == KEY_POWER) c = profileFindKey(p, KEY_POWER_OFF);
      if (c >= 0) take(c, p, codeSig(code(c)));
    }
  }
  // Most widely shared codes first: one of those is the likeliest to work, so a search usually
  // succeeds in the first few steps instead of wading through one-off remotes in brand order.
  std::stable_sort(out, out + found, [](const ScanCand &x, const ScanCand &y) { return x.shared > y.shared; });
  return found;
}

int buildScan(int brand, uint8_t key, ScanCand *out, int max_out) {
  ScanFilter f;
  f.brand = brand;
  return buildScan(f, key, out, max_out);
}

static bool ieq(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
  }
  return *a == *b;
}

int findProfile(const char *brand, const char *profile) {
  for (int b = 0; b < totalBrands(); b++) {
    if (!ieq(brandName(b), brand)) continue;
    int p0 = brandFirstProfile(b), n = brandProfileCount(b);
    for (int i = 0; i < n; i++) {
      if (strcmp(profileName(p0 + i), profile) == 0) return p0 + i;
    }
  }
  return -1;
}

int searchBrands(const char *needle, int *out, int max_out) {
  int found = 0;
  size_t nl = strlen(needle);
  for (int b = 0; b < totalBrands() && found < max_out; b++) {
    const char *h = brandName(b);
    if (nl == 0) { out[found++] = b; continue; }
    for (const char *p = h; *p; p++) {
      size_t i = 0;
      while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
      if (i == nl) { out[found++] = b; break; }
    }
  }
  return found;
}

// =================================================================================== key names
// Port of tools/build_db.py compact_name() + canon_key(); the regexes themselves are expanded by the
// generator into IR_KEY_NAMES (sorted), so this only needs exact look-ups.
static uint8_t lookupKey(const char *c) {
  int lo = 0, hi = IR_NUM_KEY_NAMES - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int r = strcmp(IR_KEY_NAMES[mid].name, c);
    if (r == 0) return IR_KEY_NAMES[mid].key;
    if (r < 0) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}

uint8_t canonKey(const char *name) {
  // upper-case, trim
  char up[64];
  size_t n = 0;
  while (*name == ' ' || *name == '\t') name++;
  for (const char *p = name; *p && n < sizeof(up) - 1; p++) up[n++] = (char)toupper((unsigned char)*p);
  while (n && (up[n - 1] == ' ' || up[n - 1] == '\t')) n--;
  up[n] = 0;
  // trailing "+" = UP, trailing "-" after a letter/digit/space = DOWN
  std::string s(up);
  {
    size_t e = s.size();
    while (e && s[e - 1] == ' ') e--;
    if (e && s[e - 1] == '+') { s = s.substr(0, e - 1) + " UP"; }
    else if (e >= 2 && s[e - 1] == '-' && (isalnum((unsigned char)s[e - 2]) || s[e - 2] == ' ')) {
      s = s.substr(0, e - 1) + " DOWN";
    }
  }
  // compact (A-Z0-9 only) and spaced (words) forms
  std::string compact, spaced;
  bool sep = false;
  for (char ch : s) {
    if (isalnum((unsigned char)ch)) {
      compact.push_back(ch);
      if (sep && !spaced.empty()) spaced.push_back(' ');
      spaced.push_back(ch);
      sep = false;
    } else {
      sep = true;
    }
  }
  if (compact.empty()) return 0;
  // zone / second-room / record-out buttons are never mapped to a main key
  static const char *const ZONE_WORDS[] = {"ZONE", "ZN", "Z2", "Z3", "ZONE2", "ZONE3", "2ND", "MULTI", "ROOM",
                                           "RECOUT"};
  std::string padded = " " + spaced + " ";
  for (const char *w : ZONE_WORDS) {
    if (padded.find(std::string(" ") + w + " ") != std::string::npos) return 0;
  }
  static const char *const ZONE_PHRASES[] = {" DECK B ", " DECKB ", " B DECK ", " BDECK ", " REMOTE 2 ", " REMOTE2 ",
                                             " REC OUT "};
  for (const char *w : ZONE_PHRASES) if (padded.find(w) != std::string::npos) return 0;
  uint8_t k = lookupKey(compact.c_str());
  if (k) return k;
  // "POWER ON ASSERT" style discrete codes, "INPUT CD" / "SOURCE TUNER" style input selects
  std::string c2 = compact;
  for (const char *suf : {"ASSERT", "DISCRETE"}) {
    size_t l = strlen(suf);
    if (c2.size() > l && c2.compare(c2.size() - l, l, suf) == 0) { c2.resize(c2.size() - l); break; }
  }
  // same alternation order as the generator's regex (INPUT|SOURCE|SRC|FUNC|FUNCTION|SELECT|SEL)
  for (const char *pre : {"INPUT", "SOURCE", "SRC", "FUNC", "FUNCTION", "SELECT", "SEL"}) {
    size_t l = strlen(pre);
    if (c2.size() >= l + 2 && c2.compare(0, l, pre) == 0) { c2 = c2.substr(l); break; }
  }
  if (c2 != compact) return lookupKey(c2.c_str());
  return 0;
}

}  // namespace irdb
