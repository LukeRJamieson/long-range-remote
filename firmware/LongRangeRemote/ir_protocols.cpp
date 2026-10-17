// ir_protocols.cpp - IR protocol encoders (see ir_protocols.h).
// Each encoder is a direct transcription of the protocol's IRP definition, quoted above it.
#include "ir_protocols.h"
#include <string.h>
#include <ctype.h>

namespace {

// ---------------------------------------------------------------------------
// Timing sequence builder: merges adjacent marks/spaces, drops a leading space,
// and tracks elapsed time so "^extent" (fixed frame period) can be honoured.
// ---------------------------------------------------------------------------
struct Seq {
  uint32_t *buf;
  uint16_t *n;
  uint16_t cap;
  uint32_t frame_start = 0;
  uint32_t elapsed = 0;

  Seq(uint32_t *b, uint16_t *count, uint16_t capacity) : buf(b), n(count), cap(capacity) { *n = 0; }

  // d > 0: mark of d us, d < 0: space of -d us
  void add(int32_t d) {
    if (d == 0) return;
    bool mark = d > 0;
    uint32_t us = mark ? (uint32_t)d : (uint32_t)(-d);
    if (*n == 0 && !mark) return;               // leading space is just silence
    bool last_is_mark = (*n % 2) == 1;           // index 0 = mark, 1 = space, ...
    if (*n > 0 && last_is_mark == mark) {
      buf[*n - 1] += us;                         // merge with previous same-level segment
    } else if (*n < cap) {
      buf[(*n)++] = us;
    }
    elapsed += us;
  }
  void mark(uint32_t us)  { add((int32_t)us); }
  void space(uint32_t us) { add(-(int32_t)us); }
  void beginFrame() { frame_start = elapsed; }
  // Pad with space so that the frame (since beginFrame) lasts `total_us` ("^extent" in IRP).
  void extent(uint32_t total_us) {
    uint32_t used = elapsed - frame_start;
    space(used < total_us ? total_us - used : 0);
  }
  // Emit `nbits` of `value` using the given bit spec (each symbol = two signed durations).
  void bits(uint32_t value, int nbits, bool msb_first, int32_t z0, int32_t z1, int32_t o0, int32_t o1) {
    for (int i = 0; i < nbits; i++) {
      int bit = msb_first ? (value >> (nbits - 1 - i)) & 1 : (value >> i) & 1;
      if (bit) { add(o0); add(o1); } else { add(z0); add(z1); }
    }
  }
};

// Convenience: pulse-distance bits "<1,-a|1,-b>" in units of `u` microseconds.
inline void pdBits(Seq &s, uint32_t v, int n, bool msb, uint32_t u, uint32_t a, uint32_t b) {
  s.bits(v, n, msb, (int32_t)u, -(int32_t)(a), (int32_t)u, -(int32_t)(b));
}

// ---------------------------------------------------------------------------
// NEC family.  {38.4k,564}<1,-1|1,-3>(16,-8,D:8,S:8,F:8,~F:8,1,^108m,(16,-4,1,^108m)*)
// Firmware stores the four bytes directly: D = byte0|byte1<<8, F = byte2|byte3<<8.
// ---------------------------------------------------------------------------
void encNecLike(const IrCodeParams &c, IrSignal &o, uint32_t freq, uint16_t hdr_units, int rep_style) {
  const uint32_t U = 564;
  o.carrier_hz = freq;
  uint32_t data = (uint32_t)c.d | ((uint32_t)c.f << 16);
  {
    Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
    s.beginFrame();
    s.mark(hdr_units * U); s.space(8 * U);
    pdBits(s, data, 32, false, U, U, 3 * U);
    s.mark(U);
    s.extent(108000);
  }
  Seq r(o.repeat, &o.n_repeat, IrSignal::MAX_TIMINGS);
  r.beginFrame();
  if (rep_style == 0) {            // NEC1 ditto: (16,-4,1,^108m)
    r.mark(16 * U); r.space(4 * U); r.mark(U); r.extent(108000);
  } else if (rep_style == 2) {     // NECx1 ditto: (8,-8,~D:1,1,^108m)
    r.mark(8 * U); r.space(8 * U);
    pdBits(r, (~c.d) & 1, 1, false, U, U, 3 * U);
    r.mark(U); r.extent(108000);
  } else {                         // full frame repeat (NEC2 / Pioneer)
    memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
    o.n_repeat = o.n_first;
  }
}

// Aiwa / NEC42: {38.123k,550}<1,-1|1,-3>(16,-8,D:8,S:5,~D:8,~S:5,F:8,~F:8,1,-42,(16,-8,1,-165)*)
// Firmware D = 13-bit address (D | S<<8).
void encAiwa(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 550;
  o.carrier_hz = 38123;
  uint32_t a = c.d & 0x1FFF, na = (~c.d) & 0x1FFF;
  uint8_t f = c.f & 0xFF;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.mark(16 * U); s.space(8 * U);
  pdBits(s, a, 13, false, U, U, 3 * U);
  pdBits(s, na, 13, false, U, U, 3 * U);
  pdBits(s, f, 8, false, U, U, 3 * U);
  pdBits(s, (uint8_t)~f, 8, false, U, U, 3 * U);
  s.mark(U); s.space(42 * U);
  Seq r(o.repeat, &o.n_repeat, IrSignal::MAX_TIMINGS);
  r.mark(16 * U); r.space(8 * U); r.mark(U); r.space(165 * U);
}

// Kaseikyo 48-bit: {37k,432}<1,-1|1,-3>(8,-4, 6 bytes LSB first ,1,-173)*
// D = vendor (byte0 | byte1<<8), S = byte2, F = byte3 | byte4<<8, byte5 = byte2^byte3^byte4.
void encKaseikyo(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 432;
  o.carrier_hz = 37000;
  uint8_t b[6];
  b[0] = c.d & 0xFF; b[1] = c.d >> 8; b[2] = c.s & 0xFF;
  b[3] = c.f & 0xFF; b[4] = c.f >> 8; b[5] = b[2] ^ b[3] ^ b[4];
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.mark(8 * U); s.space(4 * U);
  for (int i = 0; i < 6; i++) pdBits(s, b[i], 8, false, U, U, 3 * U);
  s.mark(U); s.space(173 * U);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// Teac-K: {37k,432}<1,-1|1,-3>(8,-4,67:8,83:8,X:4,D:4,S:8,F:8,T:8,1,-100,(8,-8,1,-100)*)
//         {T=D+S:4:0+S:4:4+F:4:0+F:4:4} X=1
void encTeacK(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 432;
  o.carrier_hz = 37000;
  uint8_t d = c.d & 0xF, sb = c.s & 0xFF, f = c.f & 0xFF;
  uint8_t t = (uint8_t)(d + (sb & 0xF) + (sb >> 4) + (f & 0xF) + (f >> 4));
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.mark(8 * U); s.space(4 * U);
  pdBits(s, 67, 8, false, U, U, 3 * U);
  pdBits(s, 83, 8, false, U, U, 3 * U);
  pdBits(s, 1, 4, false, U, U, 3 * U);
  pdBits(s, d, 4, false, U, U, 3 * U);
  pdBits(s, sb, 8, false, U, U, 3 * U);
  pdBits(s, f, 8, false, U, U, 3 * U);
  pdBits(s, t, 8, false, U, U, 3 * U);
  s.mark(U); s.space(100 * U);
  Seq r(o.repeat, &o.n_repeat, IrSignal::MAX_TIMINGS);
  r.mark(8 * U); r.space(8 * U); r.mark(U); r.space(100 * U);
}

// Sony SIRC: {40k,600}<1,-1|2,-1>(4,-1,F:7,D:n,^45m)*  - Sony gear wants >= 3 frames.
void encSony(const IrCodeParams &c, IrSignal &o, int dbits) {
  const uint32_t U = 600;
  o.carrier_hz = 40000;
  o.min_frames = 3;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.beginFrame();
  s.mark(4 * U); s.space(U);
  s.bits(c.f & 0x7F, 7, false, U, -(int32_t)U, 2 * U, -(int32_t)U);
  s.bits(c.d, dbits, false, U, -(int32_t)U, 2 * U, -(int32_t)U);
  s.extent(45000);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// RC5: {36k,msb,889}<1,-1|-1,1>((1,~F:1:6,T:1,D:5,F:6,^114m)*,T=1-T)
void encRc5(const IrCodeParams &c, bool t, IrSignal &o) {
  const int32_t U = 889;
  o.carrier_hz = 36000;
  uint32_t v = 1;                                   // start bit
  v = (v << 1) | ((~c.f >> 6) & 1);                 // field bit = inverted command bit 6
  v = (v << 1) | (t ? 1 : 0);
  v = (v << 5) | (c.d & 0x1F);
  v = (v << 6) | (c.f & 0x3F);
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.beginFrame();
  s.bits(v, 14, true, U, -U, -U, U);
  s.extent(114000);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// RC5x (Marantz extended): {36k,msb,889}<1,-1|-1,1>((1,~S:1:6,T:1,D:5,-4,S:6,F:6,^114m)*,T=1-T)
void encRc5x(const IrCodeParams &c, bool t, IrSignal &o) {
  const int32_t U = 889;
  o.carrier_hz = 36000;
  uint32_t head = 1;
  head = (head << 1) | ((~c.s >> 6) & 1);
  head = (head << 1) | (t ? 1 : 0);
  head = (head << 5) | (c.d & 0x1F);
  uint32_t tail = ((uint32_t)(c.s & 0x3F) << 6) | (c.f & 0x3F);
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.beginFrame();
  s.bits(head, 8, true, U, -U, -U, U);
  s.space(4 * U);
  s.bits(tail, 12, true, U, -U, -U, U);
  s.extent(114000);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// RC6 mode 0: {36k,444,msb}<-1,1|1,-1>((6,-2,1:1,0:3,<-2,2|2,-2>(T:1),D:8,F:8,^107m)*,T=1-T)
void encRc6(const IrCodeParams &c, bool t, IrSignal &o) {
  const int32_t U = 444;
  o.carrier_hz = 36000;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.beginFrame();
  s.mark(6 * U); s.space(2 * U);
  s.bits(1, 1, true, -U, U, U, -U);                 // start bit
  s.bits(0, 3, true, -U, U, U, -U);                 // mode 0
  s.bits(t ? 1 : 0, 1, true, -2 * U, 2 * U, 2 * U, -2 * U);   // double-width trailer/toggle bit
  s.bits(c.d & 0xFF, 8, true, -U, U, U, -U);
  s.bits(c.f & 0xFF, 8, true, -U, U, U, -U);
  s.extent(107000);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// Denon / Sharp: {38k,264}<1,-3|1,-7>(D:5,F:8,x:2,1,^67m, D:5,~F:8,y:2,1,^67m)
// Denon x=0,y=3 ; Sharp x=1,y=2.  The second (inverted) half lets the receiver validate.
void denonHalf(Seq &s, uint8_t d, uint8_t f, uint8_t ext, bool use_extent) {
  const uint32_t U = 264;
  s.beginFrame();
  pdBits(s, d & 0x1F, 5, false, U, 3 * U, 7 * U);
  pdBits(s, f, 8, false, U, 3 * U, 7 * U);
  pdBits(s, ext, 2, false, U, 3 * U, 7 * U);
  s.mark(U);
  if (use_extent) s.extent(67000); else s.space(165 * U);
}
void encDenon(const IrCodeParams &c, IrSignal &o, int variant) {
  o.carrier_hz = 38000;
  uint8_t d = c.d & 0x1F, f = c.f & 0xFF;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  switch (variant) {
    case 0: denonHalf(s, d, f, 0, true); denonHalf(s, d, (uint8_t)~f, 3, true); break;   // Denon
    case 1: denonHalf(s, d, f, 0, false); break;                                         // Denon{1}
    case 2: denonHalf(s, d, (uint8_t)~f, 3, false); break;                               // Denon{2}
    case 3: denonHalf(s, d, f, 1, true); denonHalf(s, d, (uint8_t)~f, 2, true); break;   // Sharp
  }
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// JVC: {37.9k,527,33%}<1,-1|1,-3>(16,-8,D:8,F:8,1,^59.08m,(D:8,F:8,1,^46.42m)*)
void encJvc(const IrCodeParams &c, IrSignal &o, bool leader) {
  const uint32_t U = 527;
  o.carrier_hz = 37900;
  o.min_frames = 2;                                  // JVC decks like to see one repeat
  uint32_t v = (c.d & 0xFF) | ((c.f & 0xFF) << 8);
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.beginFrame();
  if (leader) { s.mark(16 * U); s.space(8 * U); }
  pdBits(s, v, 16, false, U, U, 3 * U);
  s.mark(U);
  s.extent(leader ? 59080 : 46420);
  Seq r(o.repeat, &o.n_repeat, IrSignal::MAX_TIMINGS);
  r.beginFrame();
  pdBits(r, v, 16, false, U, U, 3 * U);
  r.mark(U);
  r.extent(46420);
}

// Mitsubishi: {32.6k,300}<1,-3|1,-7>(D:8,F:8,1,-80)*
void encMitsubishi(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 300;
  o.carrier_hz = 32600;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  pdBits(s, c.d & 0xFF, 8, false, U, 3 * U, 7 * U);
  pdBits(s, c.f & 0xFF, 8, false, U, 3 * U, 7 * U);
  s.mark(U); s.space(80 * U);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// RECS80: {38k,158,msb}<1,-31|1,-47>(1:1,T:1,D:3,F:6,1,-45m)*
void encRecs80(const IrCodeParams &c, bool t, IrSignal &o) {
  const uint32_t U = 158;
  o.carrier_hz = 38000;
  uint32_t v = (1u << 10) | ((t ? 1u : 0u) << 9) | ((c.d & 7u) << 6) | (c.f & 0x3Fu);
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  pdBits(s, v, 11, true, U, 31 * U, 47 * U);
  s.mark(U); s.space(45000);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// Bose: {38k,500,msb}<1,-1|1,-3>(2,-3,F:8,~F:8,1,-50m)*
void encBose(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 500;
  o.carrier_hz = 38000;
  uint8_t f = c.f & 0xFF;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.mark(2 * U); s.space(3 * U);
  pdBits(s, f, 8, true, U, U, 3 * U);
  pdBits(s, (uint8_t)~f, 8, true, U, U, 3 * U);
  s.mark(U); s.space(50000);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// RCA: {58k,460,msb}<1,-2|1,-4>(8,-8,D:4,F:8,~D:4,~F:8,1,-16)*   (RCA-38: 38.7 kHz)
// RCA-38(Old): ([40][8],-8,D:4,F:8,~D:4,~F:8,2,-16)+
void encRca(const IrCodeParams &c, IrSignal &o, uint32_t freq, bool old) {
  const uint32_t U = 460;
  o.carrier_hz = freq;
  uint32_t v = ((uint32_t)(c.d & 0xF) << 20) | ((uint32_t)(c.f & 0xFF) << 12) |
               ((uint32_t)(~c.d & 0xF) << 8) | (uint32_t)(~c.f & 0xFF);
  for (int pass = 0; pass < 2; pass++) {
    Seq s(pass ? o.repeat : o.first, pass ? &o.n_repeat : &o.n_first, IrSignal::MAX_TIMINGS);
    s.mark((old && pass == 0 ? 40 : 8) * U); s.space(8 * U);
    pdBits(s, v, 24, true, U, 2 * U, 4 * U);
    s.mark((old ? 2 : 1) * U); s.space(16 * U);
  }
}

// Akai: {38k,289}<1,-2.6|1,-6.3>(D:3,F:7,1,^25.3m)*
void encAkai(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 289;
  o.carrier_hz = 38000;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.beginFrame();
  uint32_t zero = (uint32_t)(2.6f * U + 0.5f), one = (uint32_t)(6.3f * U + 0.5f);
  pdBits(s, c.d & 7, 3, false, U, zero, one);
  pdBits(s, c.f & 0x7F, 7, false, U, zero, one);
  s.mark(U);
  s.extent(25300);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

// Emerson: {36.7k,872}<1,-1|1,-3>(4,-4,D:6,F:6,~D:6,~F:6,1,-39)*
void encEmerson(const IrCodeParams &c, IrSignal &o) {
  const uint32_t U = 872;
  o.carrier_hz = 36700;
  Seq s(o.first, &o.n_first, IrSignal::MAX_TIMINGS);
  s.mark(4 * U); s.space(4 * U);
  pdBits(s, c.d & 0x3F, 6, false, U, U, 3 * U);
  pdBits(s, c.f & 0x3F, 6, false, U, U, 3 * U);
  pdBits(s, ~c.d & 0x3F, 6, false, U, U, 3 * U);
  pdBits(s, ~c.f & 0x3F, 6, false, U, U, 3 * U);
  s.mark(U); s.space(39 * U);
  memcpy(o.repeat, o.first, o.n_first * sizeof(uint32_t));
  o.n_repeat = o.n_first;
}

const char *const PROTO_NAMES[IRP__COUNT] = {
  "Raw", "NEC", "NEC2", "NECx", "Pioneer", "Aiwa", "Kaseikyo", "Sony12", "Sony15", "Sony20",
  "RC5", "RC5x", "RC6", "Denon", "Denon1", "Denon2", "Sharp", "JVC", "JVC2", "Mitsubishi",
  "RECS80", "Bose", "RCA", "RCA38", "Teac-K", "Akai", "Emerson", "RCA38old",
};

bool ieq(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++; b++;
  }
  return *a == *b;
}

}  // namespace

const char *irProtoName(uint8_t proto) {
  return proto < IRP__COUNT ? PROTO_NAMES[proto] : "?";
}

bool irParseManual(const char *name, int d, int s, int f, IrCodeParams &o) {
  o.d = o.s = o.f = 0;
  auto nec = [&](uint8_t p, int e) {
    o.proto = p;
    int sub = (s < 0) ? (~d & 0xFF) : s;
    o.d = (uint16_t)((d & 0xFF) | ((sub & 0xFF) << 8));
    o.f = (uint16_t)((f & 0xFF) | (((e < 0 ? ~f : e) & 0xFF) << 8));
    return true;
  };
  if (ieq(name, "NEC") || ieq(name, "NEC1")) return nec(IRP_NEC, -1);
  if (ieq(name, "NEC2")) return nec(IRP_NEC2, -1);
  if (ieq(name, "NECx") || ieq(name, "NECx1") || ieq(name, "NECx2") || ieq(name, "Samsung32")) {
    if (s < 0) s = d;                                   // Samsung repeats the address byte
    return nec(IRP_NECX, -1);
  }
  if (ieq(name, "Pioneer")) return nec(IRP_PIONEER, -1);
  if (ieq(name, "NEC1-y1")) return nec(IRP_NEC, (f ^ 0x7F) & 0xFF);
  if (ieq(name, "NEC1-y2")) return nec(IRP_NEC, (f ^ 0xFE) & 0xFF);
  if (ieq(name, "NEC1-y3")) return nec(IRP_NEC, (f ^ 0x7E) & 0xFF);
  if (ieq(name, "NEC-raw")) {                           // D = 16-bit address, F = 16-bit command as sent
    o.proto = IRP_NEC; o.d = (uint16_t)d; o.f = (uint16_t)f; return true;
  }
  if (ieq(name, "Aiwa") || ieq(name, "NEC42")) {
    o.proto = IRP_AIWA; o.d = (uint16_t)((d & 0xFF) | (((s < 0 ? 0 : s) & 0x1F) << 8)); o.f = f & 0xFF; return true;
  }
  auto kas = [&](uint16_t vendor, int b2, int b3, int b4) {
    o.proto = IRP_KASEIKYO; o.d = vendor; o.s = b2 & 0xFF; o.f = (uint16_t)((b3 & 0xFF) | ((b4 & 0xFF) << 8));
    return true;
  };
  if (ieq(name, "Panasonic")) return kas(0x2002, d, s < 0 ? 0 : s, f);
  if (ieq(name, "Denon-K"))   return kas(0x3254, (d & 0xF) << 4, ((s < 0 ? 0 : s) & 0xF) | ((f & 0xF) << 4), (f >> 4) & 0xFF);
  if (ieq(name, "JVC-48"))    return kas(0x0103, d, s < 0 ? 0 : s, f);
  if (ieq(name, "Kaseikyo")) {                          // generic: D = vendor, S = byte2, F = byte3|byte4<<8
    o.proto = IRP_KASEIKYO; o.d = (uint16_t)d; o.s = (uint16_t)(s < 0 ? 0 : s); o.f = (uint16_t)f; return true;
  }
  if (ieq(name, "Teac-K")) { o.proto = IRP_TEACK; o.d = d & 0xF; o.s = (s < 0 ? 0 : s) & 0xFF; o.f = f & 0xFF; return true; }
  if (ieq(name, "Sony12") || ieq(name, "SIRC")) { o.proto = IRP_SONY12; o.d = d & 0x1F; o.f = f & 0x7F; return true; }
  if (ieq(name, "Sony15") || ieq(name, "SIRC15")) { o.proto = IRP_SONY15; o.d = d & 0xFF; o.f = f & 0x7F; return true; }
  if (ieq(name, "Sony20") || ieq(name, "SIRC20")) {
    o.proto = IRP_SONY20; o.d = (uint16_t)((d & 0x1F) | (((s < 0 ? 0 : s) & 0xFF) << 5)); o.f = f & 0x7F; return true;
  }
  if (ieq(name, "RC5"))  { o.proto = IRP_RC5; o.d = d & 0x1F; o.f = f & 0x7F; return true; }
  if (ieq(name, "RC5x")) { o.proto = IRP_RC5X; o.d = d & 0x1F; o.s = (s < 0 ? 0 : s) & 0x7F; o.f = f & 0x3F; return true; }
  if (ieq(name, "RC6") || ieq(name, "RC6-0-16")) { o.proto = IRP_RC6; o.d = d & 0xFF; o.f = f & 0xFF; return true; }
  struct Simple { const char *n; uint8_t p; uint16_t dm, fm; };
  static const Simple simple[] = {
    {"Denon", IRP_DENON, 0x1F, 0xFF}, {"Denon{1}", IRP_DENON1, 0x1F, 0xFF}, {"Denon1", IRP_DENON1, 0x1F, 0xFF},
    {"Denon{2}", IRP_DENON2, 0x1F, 0xFF}, {"Denon2", IRP_DENON2, 0x1F, 0xFF}, {"Sharp", IRP_SHARP, 0x1F, 0xFF},
    {"JVC", IRP_JVC, 0xFF, 0xFF}, {"JVC{2}", IRP_JVC2, 0xFF, 0xFF}, {"JVC2", IRP_JVC2, 0xFF, 0xFF},
    {"Mitsubishi", IRP_MITSUBISHI, 0xFF, 0xFF}, {"RECS80", IRP_RECS80, 0x7, 0x3F}, {"Bose", IRP_BOSE, 0, 0xFF},
    {"RCA", IRP_RCA, 0xF, 0xFF}, {"RCA-38", IRP_RCA38, 0xF, 0xFF}, {"RCA38", IRP_RCA38, 0xF, 0xFF},
    {"RCA-38(Old)", IRP_RCA38OLD, 0xF, 0xFF}, {"Akai", IRP_AKAI, 0x7, 0x7F}, {"Emerson", IRP_EMERSON, 0x3F, 0x3F},
  };
  for (const Simple &x : simple) {
    if (ieq(name, x.n)) { o.proto = x.p; o.d = d & x.dm; o.f = f & x.fm; return true; }
  }
  return false;
}

bool irEncode(const IrCodeParams &c, bool toggle, IrSignal &o) {
  o.carrier_hz = 38000;
  o.duty_pct = 33;
  o.min_frames = 1;
  o.n_first = o.n_repeat = 0;
  switch (c.proto) {
    case IRP_NEC:        encNecLike(c, o, 38400, 16, 0); break;
    case IRP_NEC2:       encNecLike(c, o, 38400, 16, 1); break;
    case IRP_NECX:       encNecLike(c, o, 38400, 8, 2); break;
    case IRP_PIONEER:    encNecLike(c, o, 40000, 16, 1); break;
    case IRP_AIWA:       encAiwa(c, o); break;
    case IRP_KASEIKYO:   encKaseikyo(c, o); break;
    case IRP_TEACK:      encTeacK(c, o); break;
    case IRP_SONY12:     encSony(c, o, 5); break;
    case IRP_SONY15:     encSony(c, o, 8); break;
    case IRP_SONY20:     encSony(c, o, 13); break;
    case IRP_RC5:        encRc5(c, toggle, o); break;
    case IRP_RC5X:       encRc5x(c, toggle, o); break;
    case IRP_RC6:        encRc6(c, toggle, o); break;
    case IRP_DENON:      encDenon(c, o, 0); break;
    case IRP_DENON1:     encDenon(c, o, 1); break;
    case IRP_DENON2:     encDenon(c, o, 2); break;
    case IRP_SHARP:      encDenon(c, o, 3); break;
    case IRP_JVC:        encJvc(c, o, true); break;
    case IRP_JVC2:       encJvc(c, o, false); break;
    case IRP_MITSUBISHI: encMitsubishi(c, o); break;
    case IRP_RECS80:     encRecs80(c, toggle, o); break;
    case IRP_BOSE:       encBose(c, o); break;
    case IRP_RCA:        encRca(c, o, 58000, false); break;
    case IRP_RCA38:      encRca(c, o, 38700, false); break;
    case IRP_RCA38OLD:   encRca(c, o, 38700, true); break;
    case IRP_AKAI:       encAkai(c, o); break;
    case IRP_EMERSON:    encEmerson(c, o); break;
    default: return false;
  }
  return o.n_first > 0;
}
