// ir_db.cpp - accessors for the IR database (irdb.bin from the SD card, plus your own .ir files).
// No Arduino dependencies: tools/test_all_codes.cpp compiles this file on a PC.
#include "ir_db.h"
#include "ir_keys.h"
#include <algorithm>
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

// One set of tables (the main database image, or your own remotes)
struct View {
  const char *str = nullptr;      uint32_t str_len = 0;
  const IrBrand *brands = nullptr; int nb = 0;
  const IrGroup *groups = nullptr; int ng = 0;
  const uint16_t *group_brands = nullptr; int ngb = 0;
  const IrProfile *profiles = nullptr; int np = 0;
  const IrCode *codes = nullptr; int nc = 0;
  const IrRawSig *raw = nullptr; int nr = 0;
  const uint8_t *raw_data = nullptr; uint32_t raw_data_len = 0;
  const uint16_t *raw_table = nullptr; uint32_t raw_table_len = 0;
};
View A;            // main database
View U;            // your own remotes (indices continue after A's)
char g_source[48] = "none";

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

inline bool isU(int i, int nA) { return i >= nA; }

inline const IrBrand &brandRow(int b) { return b < A.nb ? A.brands[b] : U.brands[b - A.nb]; }
inline const IrProfile &profRow(int p) { return p < A.np ? A.profiles[p] : U.profiles[p - A.np]; }
inline const IrCode &codeRow(int c) { return c < A.nc ? A.codes[c] : U.codes[c - A.nc]; }
inline const char *strA(uint32_t off) { return off < A.str_len ? A.str + off : ""; }
inline const char *strU(uint32_t off) { return off < U.str_len ? U.str + off : ""; }

int totalBrands() { return A.nb + U.nb; }
int totalProfiles() { return A.np + U.np; }
int totalCodes() { return A.nc + U.nc; }

void setErr(char *err, size_t n, const char *msg) {
  if (err && n) { strncpy(err, msg, n - 1); err[n - 1] = 0; }
}

uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

}  // namespace

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

bool useImage(const uint8_t *img, size_t len, char *err, size_t en) {
  if (!checkHeader(img, len, err, en)) return false;
  if (((uintptr_t)img & 3) != 0) { setErr(err, en, "image not 4-byte aligned"); return false; }
  uint32_t nsec = rd32(img + 16);
  if (nsec != 9 || len < 20 + 9 * 8) { setErr(err, en, "unexpected section count"); return false; }
  static const uint32_t ESZ[9] = {1, sizeof(IrBrand), sizeof(IrGroup), 2, sizeof(IrProfile), sizeof(IrCode),
                                  sizeof(IrRawSig), 1, 2};
  uint32_t off[9], cnt[9];
  for (int i = 0; i < 9; i++) {
    off[i] = rd32(img + 20 + 8 * i);
    cnt[i] = rd32(img + 24 + 8 * i);
    if ((off[i] & 3) || (uint64_t)off[i] + (uint64_t)cnt[i] * ESZ[i] > len) {
      setErr(err, en, "section outside the file (truncated?)"); return false;
    }
  }
  View v;
  v.str = (const char *)img + off[0]; v.str_len = cnt[0];
  v.brands = (const IrBrand *)(img + off[1]); v.nb = cnt[1];
  v.groups = (const IrGroup *)(img + off[2]); v.ng = cnt[2];
  v.group_brands = (const uint16_t *)(img + off[3]); v.ngb = cnt[3];
  v.profiles = (const IrProfile *)(img + off[4]); v.np = cnt[4];
  v.codes = (const IrCode *)(img + off[5]); v.nc = cnt[5];
  v.raw = (const IrRawSig *)(img + off[6]); v.nr = cnt[6];
  v.raw_data = img + off[7]; v.raw_data_len = cnt[7];
  v.raw_table = (const uint16_t *)(img + off[8]); v.raw_table_len = cnt[8];
  // Every index must stay inside its table, so a damaged file can't crash the remote later.
  if (v.str_len == 0 || v.str[v.str_len - 1] != 0) { setErr(err, en, "bad string pool"); return false; }
  for (int b = 0; b < v.nb; b++) {
    const IrBrand &r = v.brands[b];
    if (r.name >= v.str_len || r.first_prof + r.n_prof > v.np) { setErr(err, en, "bad brand table"); return false; }
  }
  for (int g = 0; g < v.ng; g++) {
    const IrGroup &r = v.groups[g];
    if (r.name >= v.str_len || r.first + r.count > v.ngb) { setErr(err, en, "bad group table"); return false; }
  }
  for (int i = 0; i < v.ngb; i++) if (v.group_brands[i] >= v.nb) { setErr(err, en, "bad group table"); return false; }
  for (int p = 0; p < v.np; p++) {
    const IrProfile &r = v.profiles[p];
    if (r.name >= v.str_len || r.brand >= v.nb || r.cat >= IR_NUM_CATS || (uint64_t)r.first_code + r.n_codes > (uint64_t)v.nc) {
      setErr(err, en, "bad remote table"); return false;
    }
  }
  for (int c = 0; c < v.nc; c++) {
    const IrCode &r = v.codes[c];
    if (r.name >= v.str_len || r.proto >= IRP__COUNT || r.key >= KEY__COUNT || (r.proto == IRP_RAW && r.d >= v.nr)) {
      setErr(err, en, "bad code table"); return false;
    }
  }
  for (int r = 0; r < v.nr; r++) {
    const IrRawSig &x = v.raw[r];
    uint32_t bytes = x.table_len <= 16 ? (x.count + 1u) / 2 : x.count;
    if (x.count == 0 || (uint64_t)x.data_off + bytes > v.raw_data_len ||
        (uint64_t)x.table_off + x.table_len > v.raw_table_len || x.table_len == 0) {
      setErr(err, en, "bad raw table"); return false;
    }
  }
  if (U.np) { setErr(err, en, "load the main database before your own remotes"); return false; }
  A = v;
  return true;
}

const char *sourceName() { return g_source; }
void setSourceName(const char *s) { strncpy(g_source, s, sizeof(g_source) - 1); g_source[sizeof(g_source) - 1] = 0; }

// =================================================================================== your own remotes
void userBegin() {
  UT = UserTables();
  U = View();
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
  if (A.nr + UT.raw.size() >= 65535) return false;      // IrCode::d is 16-bit
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
  return userAdd(name, IRP_RAW, (uint16_t)(A.nr + UT.raw.size() - 1), 0, 0);
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

int profileFindKey(int p, uint8_t key) {
  if (!okP(p) || key == 0) return -1;
  int c0 = profileFirstCode(p), n = profileCodeCount(p);
  for (int i = 0; i < n; i++) {
    if (codeRow(c0 + i).key == key) return c0 + i;
  }
  return -1;
}

static const IrCode kNullCode = {0, 0, 0, 0, 0, 0};   // name, d, s, f, proto, key
const IrCode &code(int c)     { return okC(c) ? codeRow(c) : kNullCode; }
const char *codeName(int c) {
  if (!okC(c)) return "";
  return c < A.nc ? strA(A.codes[c].name) : strU(U.codes[c - A.nc].name);
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
  const IrCode &x = code(c);
  IrCodeParams p;
  p.proto = x.proto; p.d = x.d; p.s = x.s; p.f = x.f;
  return p;
}

static const IrRawSig kNullRaw = {0, 0, 38000, 0, 1, 33};
const IrRawSig &raw(int r) {
  if (r >= 0 && r < A.nr) return A.raw[r];
  if (r >= A.nr && r < A.nr + U.nr) return U.raw[r - A.nr];
  return kNullRaw;
}

uint16_t rawSample(const IrRawSig &r, int i) {
  if (i < 0 || i >= r.count) return 0;
  // which table set does this signal belong to?
  const bool user = U.nr && &r >= U.raw && &r < U.raw + U.nr;
  const uint8_t *data = user ? U.raw_data : A.raw_data;
  const uint16_t *table = user ? U.raw_table : A.raw_table;
  if (!data || !table) return 0;
  uint8_t idx;
  if (r.table_len <= 16) {
    uint8_t b = data[r.data_off + i / 2];
    idx = (i & 1) ? (b >> 4) : (b & 0x0F);
  } else {
    idx = data[r.data_off + i];
  }
  if (idx >= r.table_len) idx = 0;
  return table[r.table_off + idx];
}

bool sameSignal(int a, int b) {
  const IrCode &x = code(a), &y = code(b);
  // NEC1 and NEC2 differ only in how a HELD key repeats; a single press is the same frame on air.
  auto norm = [](uint8_t p) { return p == IRP_NEC2 ? (uint8_t)IRP_NEC : p; };
  if (norm(x.proto) != norm(y.proto)) return false;
  if (x.proto != IRP_RAW) return x.d == y.d && x.s == y.s && x.f == y.f;
  if (x.d == y.d) return true;
  const IrRawSig &rx = raw(x.d), &ry = raw(y.d);
  if (rx.count != ry.count || rx.freq != ry.freq) return false;
  for (int i = 0; i < rx.count; i++) {
    if (rawSample(rx, i) != rawSample(ry, i)) return false;
  }
  return true;
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
  forEachProfile(flt, [&](int p) {
    int c = profileFindKey(p, key);
    if (c < 0 && key == KEY_POWER) c = profileFindKey(p, KEY_POWER_ON);
    if (c < 0) return;
    for (int j = 0; j < found; j++) {
      if (sameSignal(out[j].code, c)) {           // same signal already queued: just count it
        out[j].shared++;
        if (profileCodeCount(p) > profileCodeCount(out[j].profile)) { out[j].profile = p; out[j].code = c; }
        return;
      }
    }
    if (found < max_out) {
      out[found].code = c; out[found].profile = p; out[found].shared = 1;
      found++;
    }
  });
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
