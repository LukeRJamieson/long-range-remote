// Host test of discover.cpp against the real database.
//   g++ -O2 -std=c++17 -I. tools/discover_test/test.cpp discover.cpp ir_protocols.cpp ir_db.cpp flipper_ir.cpp -o /tmp/discover_test
//   /tmp/discover_test ../sd_card/irdb.bin
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "discover.h"
#include "flipper_ir.h"
#include "ir_db.h"
#include "ir_keys.h"

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  [FAIL] %s (line %d)\n", what, __LINE__); fails++; } } while (0)

// fake transmitter: each frame takes 70 ms
static uint32_t t_ms = 1000, busy_until = 0;
static std::vector<IrCodeParams> sent;
static void send(const IrCodeParams &p, const char *) { sent.push_back(p); busy_until = t_ms + 70; }
static bool busy() { return (int32_t)(busy_until - t_ms) > 0; }
static uint32_t now() { return t_ms; }
static void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 5) { t_ms += 5; discover::loop(); } }

int main(int argc, char **argv) {
  if (argc < 2) { printf("usage: %s path/to/irdb.bin\n", argv[0]); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *img = (uint8_t *)malloc(len);
  if (fread(img, 1, len, f) != (size_t)len) return 2;
  fclose(f);
  char err[120];
  if (!irdb::useImage(img, len, err, sizeof(err))) { printf("database rejected: %s\n", err); return 1; }
  discover::setHooks({send, busy, now});

  puts("1. families: addresses, probe functions and what they usually are");
  int total_addr = 0;
  for (int fam = 0; fam < discover::numFamilies(); fam++) {
    uint8_t pr[8];
    int np = discover::probes(fam, pr, 8), na = discover::numAddrs(fam);
    total_addr += na;
    printf("  %-16s %4d addresses, probes:", discover::family(fam).name, na);
    for (int i = 0; i < np; i++) printf(" %d=%s", pr[i], IR_KEY_LABELS[discover::guessKey(fam, pr[i])]);
    printf("\n");
    CHECK(na > 0 && np >= 4, "has addresses and probes");
    for (int i = 0; i < np; i++) for (int j = 0; j < i; j++) CHECK(pr[i] != pr[j], "probes are distinct");
  }
  printf("  all protocols: %d addresses, about %d min\n", total_addr, total_addr * 6 * 135 / 60000);

  puts("2. every key id in a saved file maps back to the same key");
  for (int k = 1; k < KEY__COUNT; k++)
    if (irdb::canonKey(discover::keyFileName(k)) != k) { printf("  [FAIL] %s -> %d, not %d\n", discover::keyFileName(k), irdb::canonKey(discover::keyFileName(k)), k); fails++; }

  puts("3. sweep of NEC: every address gets the probes; 'it reacted' steps back; learn; save; reload");
  discover::sweepStart(0);
  run(3000);
  discover::State s = discover::state();
  CHECK(s.mode == discover::SWEEP && s.pos >= 3, "sweep is running");
  printf("     %zu probes in 3 s (%.0f ms each), at address %d of %d, about %u s left\n", sent.size(), 3000.0 / sent.size(), s.pos, s.total, s.eta_s);
  int at = s.pos;
  discover::reacted();
  s = discover::state();
  CHECK(s.mode == discover::NARROW && s.pos == at - 3, "narrowing starts 3 addresses back");
  size_t n0 = sent.size(); run(2000);
  CHECK(sent.size() - n0 == 6, "one burst of 6 probes for that address, then quiet");
  discover::narrowStep(1); n0 = sent.size(); run(2000);
  CHECK(discover::state().pos == at - 2 && sent.size() - n0 == 6, "next address tried");
  int addr = discover::state().addr;
  discover::learnPick();
  s = discover::state();
  CHECK(s.mode == discover::LEARN && s.addr == addr, "learning that address");
  n0 = sent.size(); discover::learnStep(0); run(500);
  CHECK(sent.size() - n0 == 1, "one function sent");
  int f0 = discover::state().func;
  CHECK(discover::learnName(KEY_POWER, nullptr), "named Power");
  discover::learnStep(1); run(300); int f1 = discover::state().func;
  CHECK(f1 != f0, "next function is a different one");
  CHECK(discover::learnName(KEY_VOL_UP, nullptr), "named Vol +");
  discover::learnStep(1); run(300); int f2 = discover::state().func;
  CHECK(discover::learnName(0, "Party: mode#1"), "custom name");
  CHECK(discover::state().n_learned == 3, "three keys learned");
  discover::learnName(0, "Party mode");
  CHECK(discover::state().n_learned == 3, "renaming doesn't add a key");
  std::string text = discover::fileText();
  char brand[24], remote[40];
  discover::fileNames(brand, sizeof(brand), remote, sizeof(remote));
  printf("     saved as %s/%s.ir, %zu bytes\n", brand, remote, text.size());
  irdb::userBegin();
  flipperir::FileStats st;
  CHECK(flipperir::addFile(text.data(), text.size(), brand, remote, IR_NUM_CATS - 1, st), "the file loads");
  irdb::userFinish();
  int p = irdb::findProfile(brand, remote);
  CHECK(p >= 0 && irdb::profileCodeCount(p) == 3 && st.skipped == 0, "as a remote with 3 keys");
  if (p >= 0) {
    int cp = irdb::profileFindKey(p, KEY_POWER), cv = irdb::profileFindKey(p, KEY_VOL_UP);
    IrCodeParams want = discover::code(0, addr, f0), got = irdb::codeParams(cp >= 0 ? cp : 0);
    CHECK(cp >= 0 && got.proto == want.proto && got.d == want.d && got.s == want.s && got.f == want.f, "Power is the code that was named");
    want = discover::code(0, addr, f1); got = irdb::codeParams(cv >= 0 ? cv : 0);
    CHECK(cv >= 0 && got.d == want.d && got.f == want.f, "Vol + too");
  }
  puts("4. learning the same address again starts with the saved keys");
  discover::learnStart(0, addr);
  CHECK(discover::state().n_learned == 3, "three keys preloaded");
  bool has_custom = false;
  for (int i = 0; i < discover::state().n_learned; i++) if (discover::learned(i).func == f2 && !strcmp(discover::learned(i).name, "Party mode")) has_custom = true;
  CHECK(has_custom, "including the custom one");
  discover::learnForget(f2);
  CHECK(discover::state().n_learned == 2, "forget removes one");

  puts("5. every family: a saved file gives back exactly the codes that were sent");
  for (int fam = 0; fam < discover::numFamilies(); fam++) {
    int na = discover::numAddrs(fam), nf = discover::numFuncs(fam);
    int a = discover::state().mode, dummy = a; (void)dummy;
    // an address from the middle of the order, and three functions incl. the highest
    discover::sweepStart(fam);
    for (int i = 0; i < na / 2; i++) { discover::reacted(); discover::narrowStep(1); discover::narrowStep(1); discover::narrowStep(1); discover::narrowStep(1); break; }
    discover::reacted();
    int ad = discover::state().addr;
    discover::learnStart(fam, ad);
    int fs[3] = {0, nf / 2 + 1, nf - 1};
    for (int i = 0; i < 3; i++) { discover::learnGoto(fs[i]); char nm[16]; snprintf(nm, sizeof(nm), "T%d", i); discover::learnName(0, nm); }
    std::string tx = discover::fileText();
    discover::fileNames(brand, sizeof(brand), remote, sizeof(remote));
    irdb::userBegin();
    flipperir::FileStats s2;
    bool ok = flipperir::addFile(tx.data(), tx.size(), brand, remote, IR_NUM_CATS - 1, s2);
    irdb::userFinish();
    int pp = irdb::findProfile(brand, remote);
    bool same = ok && pp >= 0 && irdb::profileCodeCount(pp) == 3;
    for (int i = 0; same && i < 3; i++) {
      IrCodeParams w = discover::code(fam, ad, fs[i]), g = irdb::codeParams(irdb::profileFirstCode(pp) + i);
      same = w.proto == g.proto && w.d == g.d && w.s == g.s && w.f == g.f;
      IrSignal sig;
      if (!irEncode(w, false, sig) || sig.n_first == 0) same = false;
    }
    if (!same) { printf("  [FAIL] %s (%s)\n", discover::family(fam).name, remote); fails++; }
  }
  run(3000);

  puts("6. sweeping everything runs through all families to the end");
  sent.clear();
  discover::sweepStart(-1);
  int guard = 0, fam_seen = 0, last_fam = -1;
  while (discover::state().mode == discover::SWEEP && guard++ < 4000) {
    run(1000);
    if (discover::state().fam != last_fam) { last_fam = discover::state().fam; fam_seen++; }
  }
  s = discover::state();
  printf("     %zu probes in %d s simulated; ended in family %d of %d\n", sent.size(), guard, s.fam + 1, s.fam_total);
  CHECK(s.mode == discover::NARROW && s.done && fam_seen == discover::numFamilies(), "reached the end, paused on the last address");
  CHECK((int)sent.size() >= total_addr * 4, "every address was probed");
  discover::reacted(); discover::resumeSweep();
  CHECK(discover::state().mode == discover::NARROW, "a finished sweep doesn't restart by itself");
  discover::narrowStep(-1); n0 = sent.size(); run(2000);
  CHECK(sent.size() > n0, "but its addresses can still be tried by hand");
  discover::stop();
  CHECK(discover::state().mode == discover::IDLE, "stop");

  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails != 0;
}
