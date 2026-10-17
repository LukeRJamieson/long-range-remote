// Host test: encode every code in the generated database, check sizes/limits and
// that RMT symbol conversion (same algorithm as ir_tx.cpp) fits the buffers.
// Also: reloading the image and refusing damaged / foreign ones, your-own-remotes (.ir files) parsing,
// and the firmware's key-name matching against the generator's.
// Build (from the sketch folder):
//   g++ -O2 -std=c++17 -I. tools/test_all_codes.cpp ir_protocols.cpp ir_db.cpp flipper_ir.cpp -o /tmp/test_all_codes
//   /tmp/test_all_codes ../sd_card/irdb.bin
#include "../ir_db.h"
#include "../ir_keys.h"
#include "../flipper_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static size_t symbolsFor(const uint32_t *t, int n) {       // mirrors SymWriter in ir_tx.cpp
  size_t halves = 0;
  for (int i = 0; i < n; i++) { uint32_t d = t[i]; if (d == 0) d = 1; halves += (d + 32766) / 32767; }
  return (halves + 1) / 2;
}

int main(int argc, char **argv) {
  // The database lives on the SD card in the firmware; here it is read from the file given.
  if (argc < 2) { printf("usage: %s path/to/irdb.bin\n", argv[0]); return 2; }
  {
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *img = (uint8_t *)malloc(len);          // malloc: suitably aligned, like heap_caps_malloc
    if (fread(img, 1, len, f) != (size_t)len) { printf("short read\n"); return 1; }
    fclose(f);
    char err[128];
    if (!irdb::useImage(img, len, err, sizeof(err))) { printf("database rejected: %s\n", err); return 1; }
  }
  static IrSignal sig;
  int fails = 0, raw = 0; size_t max_first = 0, max_rep = 0, max_syms = 0, max_raw_syms = 0;
  unsigned long long max_ms = 0;
  for (int c = 0; c < irdb::numCodes(); c++) {
    IrCodeParams p = irdb::codeParams(c);
    if (p.proto == IRP_RAW) {
      const IrRawSig &r = irdb::raw(p.d);
      uint32_t t[2100]; int n = r.count;
      for (int i = 0; i < n; i++) t[i] = irdb::rawSample(r, i);
      size_t s = symbolsFor(t, n); if (s > max_raw_syms) max_raw_syms = s;
      if (r.count % 2 || r.freq < 20000 || r.freq > 60000) { printf("bad raw %d\n", c); fails++; }
      raw++; continue;
    }
    if (!irEncode(p, false, sig)) { printf("encode fail code %d proto %d\n", c, p.proto); fails++; continue; }
    if (sig.n_first % 2 || sig.n_repeat % 2) { printf("odd timing count code %d proto %s\n", c, irProtoName(p.proto)); fails++; }
    unsigned long long tot = 0; for (int i = 0; i < sig.n_first; i++) tot += sig.first[i];
    if (tot > max_ms) max_ms = tot;
    if (sig.n_first > max_first) max_first = sig.n_first;
    if (sig.n_repeat > max_rep) max_rep = sig.n_repeat;
    size_t s1 = symbolsFor(sig.first, sig.n_first), s2 = symbolsFor(sig.repeat, sig.n_repeat);
    if (s1 > max_syms) max_syms = s1; if (s2 > max_syms) max_syms = s2;
    // every profile code must resolve back to its profile
    int pr = irdb::codeProfile(c);
    if (c < irdb::profileFirstCode(pr) || c >= irdb::profileFirstCode(pr) + irdb::profileCodeCount(pr)) { printf("codeProfile wrong %d\n", c); fails++; }
  }
  printf("codes=%d raw=%d fails=%d  max timings first=%zu repeat=%zu (cap %zu)  max symbols=%zu (cap 800)  raw max symbols=%zu (cap 1600)  longest first frame=%llu us\n",
         irdb::numCodes(), raw, fails, max_first, max_rep, IrSignal::MAX_TIMINGS, max_syms, max_raw_syms, max_ms);
  // string pool round-trip against tools/db_expect.txt (written by build_db.py)
  FILE *ex = fopen("tools/db_expect.txt", "r");
  int checked = 0, bad = 0;
  if (ex) {
    char line[256];
    while (fgets(line, sizeof(line), ex)) {
      char kind; int idx; int off = 0;
      if (sscanf(line, "%c %d %n", &kind, &idx, &off) < 2) continue;
      if (kind == 'R') {                        // raw: sample count, sum of samples, first sample
        int n = 0; long total = 0; int first = 0;
        sscanf(line + off, "%d %ld %d", &n, &total, &first);
        const IrRawSig &r = irdb::raw(idx);
        long got = 0; for (int i = 0; i < r.count; i++) got += irdb::rawSample(r, i);
        checked++;
        if (r.count != n || got != total || irdb::rawSample(r, 0) != first) {
          if (bad++ < 5) printf("raw %d mismatch: count %d/%d sum %ld/%ld\n", idx, r.count, n, got, total);
        }
        continue;
      }
      char *want = line + off; want[strcspn(want, "\n")] = 0;
      const char *got = kind == 'B' ? irdb::brandName(idx) : kind == 'P' ? irdb::profileName(idx) : irdb::codeName(idx);
      checked++;
      if (strcmp(want, got)) { if (bad++ < 5) printf("name mismatch %c%d: want '%s' got '%s'\n", kind, idx, want, got); }
    }
    fclose(ex);
  }
  printf("names checked=%d mismatches=%d\n", checked, bad);
  fails += bad;
  // Code search de-duplication: every remote with the key must be covered by exactly one
  // candidate that sends the identical signal, and candidates must be pairwise distinct.
  {
    std::vector<irdb::ScanCand> cv(irdb::numProfiles());
    irdb::ScanCand *cands = cv.data(); const int CAP = (int)cv.size();
    const uint8_t keys[] = {1 /*POWER*/, 4 /*VOL_UP*/, 6 /*MUTE*/};
    for (uint8_t key : keys) {
      for (int b = -1; b < irdb::numBrands(); b++) {
        int n = irdb::buildScan(b, key, cands, CAP);
        int p0 = b < 0 ? 0 : irdb::brandFirstProfile(b), np = b < 0 ? irdb::numProfiles() : irdb::brandProfileCount(b);
        int remotes = 0, shared_sum = 0;
        for (int i = 0; i < n; i++) {
          shared_sum += cands[i].shared;
          if (b >= 0 || key == 1)               // all-brands pairwise check only once (slow)
            for (int j = i + 1; j < n; j++) if (irdb::sameSignal(cands[i].code, cands[j].code)) { printf("dup cand\n"); fails++; }
          if (irdb::codeProfile(cands[i].code) != cands[i].profile) { printf("cand profile mismatch\n"); fails++; }
        }
        for (int i = 0; i < np; i++) {
          int c = irdb::profileFindKey(p0 + i, key);
          if (c < 0 && key == 1) c = irdb::profileFindKey(p0 + i, 2);
          if (c < 0) continue;
          remotes++;
          int hits = 0;
          for (int j = 0; j < n; j++) if (irdb::sameSignal(cands[j].code, c)) hits++;
          if (hits != 1) { printf("remote %d covered %d times\n", p0 + i, hits); fails++; }
        }
        if (shared_sum != remotes) { printf("shared sum %d != remotes %d\n", shared_sum, remotes); fails++; }
        if (b < 0 || !strcmp(irdb::brandName(b), "Samsung") || !strcmp(irdb::brandName(b), "LG") ||
            !strcmp(irdb::brandName(b), "Sony") || !strcmp(irdb::brandName(b), "Denon") || !strcmp(irdb::brandName(b), "Yamaha"))
          printf("scan key %d %-12s remotes=%4d -> steps=%4d\n", key, b < 0 ? "ALL BRANDS" : irdb::brandName(b), remotes, n);
      }
    }
  }
  // Brand groups and device-type filter: a filtered scan must cover exactly the remotes of the
  // group's brands (of that type) that have the key, each by exactly one candidate.
  {
    std::vector<irdb::ScanCand> cv(irdb::numProfiles());
    irdb::ScanCand *cands = cv.data(); const int CAP = (int)cv.size();
    for (int g = 0; g < irdb::numGroups(); g++) {
      for (int cat = -1; cat < IR_NUM_CATS; cat++) {
        irdb::ScanFilter f; f.group = g; f.category = cat;
        int n = irdb::buildScan(f, 1 /*POWER*/, cands, CAP);
        int remotes = 0, shared = 0;
        for (int i = 0; i < n; i++) shared += cands[i].shared;
        for (int k = 0; k < irdb::groupBrandCount(g); k++) {
          int b = irdb::groupBrand(g, k);
          for (int i = 0; i < irdb::brandProfileCount(b); i++) {
            int p = irdb::brandFirstProfile(b) + i;
            if (cat >= 0 && irdb::profileCategory(p) != cat) continue;
            int c = irdb::profileFindKey(p, 1);
            if (c < 0) c = irdb::profileFindKey(p, 2);
            if (c < 0) continue;
            remotes++;
            int hits = 0;
            for (int j = 0; j < n; j++) if (irdb::sameSignal(cands[j].code, c)) hits++;
            if (hits != 1) { printf("group %d cat %d: remote %d covered %d times\n", g, cat, p, hits); fails++; }
          }
        }
        if (shared != remotes) { printf("group %d cat %d: shared %d != remotes %d\n", g, cat, shared, remotes); fails++; }
        if (cat < 0) printf("group %-26s brands=%3d remotes=%4d  Power scan steps=%4d\n", irdb::groupName(g),
                            irdb::groupBrandCount(g), irdb::countProfiles(f), n);
      }
    }
    for (int cat = 0; cat < IR_NUM_CATS; cat++) {
      irdb::ScanFilter f; f.category = cat;
      printf("type %-10s remotes=%4d  Power scan steps=%4d\n", IR_CAT_NAMES[cat], irdb::countProfiles(f), irdb::buildScan(f, 1, cands, CAP));
    }
  }
  printf("find: %d, brands=%d profiles=%d\n", irdb::findProfile(irdb::brandName(3), irdb::profileName(irdb::brandFirstProfile(3))), irdb::numBrands(), irdb::numProfiles());

  // Key-name matching: the firmware's canonKey() must give the key the generator gave every
  // Flipper-IRDB button (IRDB code sets get their key from the function name the same way).
  {
    int checked = 0, bad = 0;
    for (int c = 0; c < irdb::numCodes(); c++) {
      int p = irdb::codeProfile(c);
      if (irdb::profileSource(p) != SRC_FLIPPER) continue;
      uint8_t want = irdb::code(c).key, got = irdb::canonKey(irdb::codeName(c));
      checked++;
      // The generator keeps only the first button per key in a remote (later ones get no key).
      if (want == KEY_NONE && got != KEY_NONE) {
        int first = irdb::profileFindKey(p, got);
        if (first >= 0 && first < c) continue;
      }
      if (want != got && bad++ < 10)
        printf("canonKey('%s') = %s, generator: %s\n", irdb::codeName(c), IR_KEY_IDS[got], IR_KEY_IDS[want]);
    }
    printf("canonKey checked=%d mismatches=%d\n", checked, bad);
    fails += bad;
  }

  // Loading the image a second time (as after an upload + restart): must be accepted, identical.
  if (argc > 1) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *img = (uint8_t *)malloc(len);
    if (fread(img, 1, len, f) != (size_t)len) { printf("short read\n"); return 1; }
    fclose(f);
    char err[128] = "";
    int nb = irdb::numBrands(), np = irdb::numProfiles(), nc = irdb::numCodes();
    std::vector<std::string> names;
    for (int b = 0; b < nb; b += 97) names.push_back(irdb::brandName(b));
    if (!irdb::checkHeader(img, 64, err, sizeof(err))) { printf("irdb.bin header rejected: %s\n", err); fails++; }
    else if (!irdb::useImage(img, len, err, sizeof(err))) { printf("irdb.bin rejected: %s\n", err); fails++; }
    else {
      int bad = 0;
      for (size_t i = 0; i < names.size(); i++) if (names[i] != irdb::brandName(i * 97)) bad++;
      printf("irdb.bin: %ld bytes, brands=%d profiles=%d (first load %d/%d), name mismatches=%d\n", len,
             irdb::numBrands(), irdb::numProfiles(), nb, np, bad);
      if (bad || irdb::numBrands() != nb || irdb::numProfiles() != np) fails++;
      (void)nc;
    }
    // A corrupted image must be refused, leaving the database usable.
    // (on a copy: `img` is the database in use now)
    uint8_t *bad_img = (uint8_t *)malloc(len); memcpy(bad_img, img, len); img = bad_img;
    img[40] ^= 0xFF;
    if (irdb::useImage(img, len, err, sizeof(err))) { printf("corrupt image accepted\n"); fails++; }
    else printf("corrupt image refused: %s\n", err);
    memcpy(img + 8, "\x01\x02\x03\x04", 4);
    if (irdb::checkHeader(img, 64, err, sizeof(err))) { printf("wrong-schema image accepted\n"); fails++; }
    else printf("wrong-schema image refused: %s\n", err);
    if (irdb::numBrands() != nb) { printf("database lost after a refused image\n"); fails++; }
  }
  // Your own remotes: parse a Flipper .ir file into the user database and send its codes.
  {
    static const char IRFILE[] =
      "Filetype: IR signals file\nVersion: 1\n#\n"
      "name: Power\ntype: parsed\nprotocol: NECext\naddress: 04 F9 00 00\ncommand: 08 F7 00 00\n#\n"
      "name: Vol_up\ntype: parsed\nprotocol: Samsung32\naddress: 07 00 00 00\ncommand: 07 00 00 00\n#\n"
      "name: Ch_next\ntype: parsed\nprotocol: RC6\naddress: 00 00 00 00\ncommand: 20 00 00 00\n#\n"
      "name: Mute\ntype: parsed\nprotocol: SIRC\naddress: 01 00 00 00\ncommand: 14 00 00 00\n#\n"
      "name: Learned\ntype: raw\nfrequency: 38000\nduty_cycle: 0.330000\n"
      "data: 9024 4512 579 552 579 552 579 1683 579 552 579 40000\n#\n"
      "name: Weird\ntype: parsed\nprotocol: NoSuchProto\naddress: 00 00 00 00\ncommand: 00 00 00 00\n";
    int nc0 = irdb::numCodes(), np0 = irdb::numProfiles(), nb0 = irdb::numBrands();
    irdb::userBegin();
    flipperir::FileStats st;
    bool ok = flipperir::addFile(IRFILE, sizeof(IRFILE) - 1, "Living room", "TV", 0, st);
    flipperir::FileStats st2;
    ok = flipperir::addFile(IRFILE, sizeof(IRFILE) - 1, "Living room", "Spare", 0, st2) && ok;
    irdb::userFinish();
    int p = irdb::numProfiles() - 2;
    printf("user file: ok=%d parsed=%d raw=%d skipped=%d  brands %d->%d profiles %d->%d codes %d->%d\n", ok, st.parsed,
           st.raw, st.skipped, nb0, irdb::numBrands(), np0, irdb::numProfiles(), nc0, irdb::numCodes());
    if (!ok || st.parsed != 4 || st.raw != 1 || st.skipped != 1 || irdb::numBrands() != nb0 + 1 ||
        irdb::numProfiles() != np0 + 2 || irdb::profileSource(p) != SRC_USER || irdb::numUserProfiles() != 2) {
      printf("user file FAILED\n"); fails++;
    }
    const uint8_t want[] = {KEY_POWER, KEY_VOL_UP, KEY_CH_UP, KEY_MUTE};
    for (uint8_t k : want) if (irdb::profileFindKey(p, k) < 0) { printf("user file: key %s missing\n", IR_KEY_IDS[k]); fails++; }
    for (int c = irdb::profileFirstCode(p); c < irdb::profileFirstCode(p) + irdb::profileCodeCount(p); c++) {
      IrCodeParams cp = irdb::codeParams(c);
      if (cp.proto == IRP_RAW) {
        const IrRawSig &r = irdb::raw(cp.d);
        if (r.count != 12 || irdb::rawSample(r, 0) < 8900 || irdb::rawSample(r, 0) > 9150 || r.freq != 38000) {
          printf("user raw wrong: count %d first %d freq %u\n", r.count, irdb::rawSample(r, 0), (unsigned)r.freq); fails++;
        }
      } else if (!irEncode(cp, false, sig)) { printf("user code %s does not encode\n", irdb::codeName(c)); fails++; }
      if (irdb::codeProfile(c) != p) { printf("user codeProfile wrong\n"); fails++; }
    }
    if (irdb::findProfile("Living room", "TV") != p) { printf("user findProfile wrong\n"); fails++; }
    int found[4]; int nf = irdb::searchBrands("living room", found, 4);
    if (nf != 1 || found[0] != irdb::numBrands() - 1) { printf("user searchBrands wrong\n"); fails++; }
    irdb::ScanFilter f; f.category = 0;
    std::vector<irdb::ScanCand> cv(irdb::numProfiles());
    int n = irdb::buildScan(f, KEY_POWER, cv.data(), (int)cv.size());
    bool covered = false;
    for (int i = 0; i < n; i++) if (irdb::sameSignal(cv[i].code, irdb::profileFindKey(p, KEY_POWER))) covered = true;
    if (!covered) { printf("user remote not in code search\n"); fails++; }
  }

  printf("%s\n", fails ? "FAILED" : "ALL PASSED");
  return fails != 0;
}
