// Host-side test driver for ir_protocols.cpp.
// Reads "PROTO D S F" lines on stdin, prints "freq|first timings|repeat timings".
// Build: g++ -O1 -I.. test_encoders.cpp ../ir_protocols.cpp -o test_encoders
#include "../ir_protocols.h"
#include <stdio.h>
#include <stdlib.h>
int main() {
  char name[64]; int d, s, f;
  static IrSignal sig;
  while (scanf("%63s %d %d %d", name, &d, &s, &f) == 4) {
    IrCodeParams c;
    bool ok;
    if (name[0] == 'F' && name[1] == 'W' && (name[2] == ':' || name[2] == 'T')) {
      // "FW:<proto id> D S F" = firmware params, "FWT:..." = same with the RC5/RC6 toggle bit set
      bool t = name[2] == 'T';
      c.proto = (uint8_t)atoi(name + (t ? 4 : 3)); c.d = (uint16_t)d; c.s = (uint16_t)s; c.f = (uint16_t)f;
      ok = irEncode(c, t, sig);
    } else {
      ok = irParseManual(name, d, s, f, c) && irEncode(c, false, sig);
    }
    if (!ok) { printf("ERR\n"); continue; }
    printf("%u|%u|", sig.carrier_hz, sig.min_frames);
    for (int i = 0; i < sig.n_first; i++) printf("%s%u", i ? "," : "", sig.first[i]);
    printf("|");
    for (int i = 0; i < sig.n_repeat; i++) printf("%s%u", i ? "," : "", sig.repeat[i]);
    printf("\n");
  }
}
