#!/usr/bin/env python3
"""
raw_decode.py - turn raw IR captures (mark/space microseconds) back into protocol codes.

Used by build_db.py so the thousands of raw captures in Flipper-IRDB's "Converted" library
take 8 bytes each instead of ~150. Every decoder checks EVERY pulse against the protocol's
nominal timing (relative + absolute tolerance), so a signal is only converted when it
unambiguously is that protocol; anything else stays raw. tools/verify_raw_decode.py then
re-encodes every decoded signal with the firmware encoder and compares it to the capture.

decode(timings, freq) -> (proto_name, D, S, F) in firmware terms (see build_db.PROTOS), or None.
"""

REL_TOL = 0.30     # +-30 % per pulse ...
ABS_TOL = 180      # ... or +-180 us, whichever is larger (captures are noisy)
GAP = 5500         # a space longer than this ends a frame (longest in-frame space: 4.5 ms NEC leader)


def near(x, nominal):
    return abs(x - nominal) <= max(ABS_TOL, REL_TOL * nominal)


def frames(t):
    """Split a capture into frames (each starts with a mark, trailing gap removed)."""
    out, cur = [], []
    for i, d in enumerate(t):
        if i % 2 == 1 and d > GAP:
            if cur:
                out.append(cur)
            cur = []
        else:
            cur.append(d)
    if cur:
        out.append(cur)
    return [f for f in out if len(f) >= 3]


# ---------------------------------------------------------------------------
# Pulse-distance protocols with a leader: NEC, NECx, Pioneer, Aiwa, Kaseikyo, JVC
# ---------------------------------------------------------------------------
def pulse_distance(f, hdr_mark, hdr_space, unit, zero_sp, one_sp, nbits):
    """Return the bits (LSB-first list) or None. Frame = leader, nbits, stop mark."""
    if len(f) != 2 + 2 * nbits + 1:
        return None
    if not (near(f[0], hdr_mark) and near(f[1], hdr_space)):
        return None
    bits = []
    for i in range(nbits):
        m, s = f[2 + 2 * i], f[3 + 2 * i]
        if not near(m, unit):
            return None
        if near(s, zero_sp) and not near(s, one_sp):
            bits.append(0)
        elif near(s, one_sp) and not near(s, zero_sp):
            bits.append(1)
        else:
            return None
    if not near(f[-1], unit):
        return None
    return bits


def to_int(bits):
    return sum(b << i for i, b in enumerate(bits))


def is_nec_ditto(f):
    return len(f) == 3 and near(f[0], 9000) and near(f[1], 2250) and near(f[2], 560)


def dec_nec_family(fr, freq):
    f = fr[0]
    # standard NEC leader 9 ms / 4.5 ms (Pioneer: 8.5/4.2 @ 40 kHz)
    bits = pulse_distance(f, 9000, 4500, 560, 560, 1690, 32)
    if bits is not None:
        v = to_int(bits)
        d, fn = v & 0xFFFF, v >> 16
        if freq and freq >= 39500:
            return ("PIONEER", d, 0, fn)
        full_repeat = len(fr) > 1 and pulse_distance(fr[1], 9000, 4500, 560, 560, 1690, 32) is not None
        return ("NEC2" if full_repeat else "NEC", d, 0, fn)
    bits = pulse_distance(f, 4500, 4500, 560, 560, 1690, 32)          # NECx / Samsung
    if bits is not None:
        v = to_int(bits)
        return ("NECX", v & 0xFFFF, 0, v >> 16)
    bits = pulse_distance(f, 8800, 4400, 550, 550, 1650, 42)          # Aiwa / NEC42
    if bits is not None:
        v = to_int(bits)
        a, na, c, nc = v & 0x1FFF, (v >> 13) & 0x1FFF, (v >> 26) & 0xFF, (v >> 34) & 0xFF
        if na == (~a & 0x1FFF) and nc == (~c & 0xFF):
            return ("AIWA", a, 0, c)
    return None


def dec_kaseikyo(fr, freq):
    bits = pulse_distance(fr[0], 3456, 1728, 432, 432, 1296, 48)
    if bits is None:
        return None
    b = [to_int(bits[8 * i:8 * i + 8]) for i in range(6)]
    if b[5] != (b[2] ^ b[3] ^ b[4]):
        return None
    return ("KASEIKYO", b[0] | (b[1] << 8), b[2], b[3] | (b[4] << 8))


def dec_jvc(fr, freq):
    bits = pulse_distance(fr[0], 8440, 4220, 527, 527, 1581, 16)
    if bits is None:
        return None
    v = to_int(bits)
    return ("JVC", v & 0xFF, 0, v >> 8)


# ---------------------------------------------------------------------------
# Sony SIRC: leader 2400/600, bit = mark 600 (0) or 1200 (1) + space 600
# ---------------------------------------------------------------------------
def dec_sony(fr, freq):
    f = fr[0]
    if not (near(f[0], 2400) and near(f[1], 600)):
        return None
    marks = f[2::2]
    spaces = f[3::2]
    n = len(marks)
    if n not in (12, 15, 20):
        return None
    bits = []
    for i, m in enumerate(marks):
        if i < len(spaces) and not near(spaces[i], 600):
            return None
        if near(m, 600) and not near(m, 1200):
            bits.append(0)
        elif near(m, 1200) and not near(m, 600):
            bits.append(1)
        else:
            return None
    v = to_int(bits)
    cmd, addr = v & 0x7F, v >> 7
    return ({12: "SONY12", 15: "SONY15", 20: "SONY20"}[n], addr, 0, cmd)


# ---------------------------------------------------------------------------
# Manchester helpers (RC5 / RC6)
# ---------------------------------------------------------------------------
def halves(f, unit, lead_level=None):
    """Expand a frame into half-bit levels (1 = mark). Each pulse must be 1 or 2 units
    (RC6 trailer bit: up to 3). Returns list or None."""
    out = [] if lead_level is None else [lead_level]
    for i, d in enumerate(f):
        level = 1 if i % 2 == 0 else 0
        k = round(d / unit)
        if k < 1 or k > 3 or not near(d, k * unit):
            return None
        out += [level] * k
    return out


def dec_rc5(fr, freq):
    f = fr[0]
    h = halves(f, 889, lead_level=0)      # first bit "1" = space,mark; leading space lost in capture
    if h is None:
        return None
    if len(h) % 2:
        h.append(0)                        # trailing space of a final "0" bit is lost
    if len(h) != 28:
        return None
    bits = []
    for i in range(0, 28, 2):
        pair = (h[i], h[i + 1])
        if pair == (0, 1):
            bits.append(1)
        elif pair == (1, 0):
            bits.append(0)
        else:
            return None
    if bits[0] != 1:
        return None
    field, toggle = bits[1], bits[2]
    d = int("".join(map(str, bits[3:8])), 2)
    c = int("".join(map(str, bits[8:14])), 2) | ((0 if field else 1) << 6)
    return ("RC5", d, 0, c)


def dec_rc6(fr, freq):
    f = fr[0]
    if not (near(f[0], 2664) and near(f[1], 888)):
        return None
    h = halves(f[2:], 444)
    if h is None:
        return None
    if len(h) % 2:
        h.append(0)
    # start(2) + mode 3 bits (6) + trailer (4) + 16 bits (32) = 44 halves
    if len(h) != 44:
        return None
    def bit(a, b):
        return 1 if (a, b) == (1, 0) else 0 if (a, b) == (0, 1) else None
    if bit(h[0], h[1]) != 1:
        return None
    mode = [bit(h[2 + 2 * i], h[3 + 2 * i]) for i in range(3)]
    if mode != [0, 0, 0]:
        return None
    if not ((h[8:12] == [1, 1, 0, 0]) or (h[8:12] == [0, 0, 1, 1])):
        return None
    data = [bit(h[12 + 2 * i], h[13 + 2 * i]) for i in range(16)]
    if None in data:
        return None
    v = int("".join(map(str, data)), 2)
    return ("RC6", v >> 8, 0, v & 0xFF)


# ---------------------------------------------------------------------------
# Denon / Sharp 15-bit: no leader, mark 264, space 792 (0) / 1848 (1)
# ---------------------------------------------------------------------------
def dec_denon(fr, freq):
    f = fr[0]
    if len(f) != 31:
        return None
    bits = []
    for i in range(15):
        m, s = f[2 * i], f[2 * i + 1]
        if not near(m, 264):
            return None
        if near(s, 792) and not near(s, 1848):
            bits.append(0)
        elif near(s, 1848) and not near(s, 792):
            bits.append(1)
        else:
            return None
    if not near(f[30], 264):
        return None
    v = to_int(bits)
    d, fn, ext = v & 0x1F, (v >> 5) & 0xFF, v >> 13
    if ext == 0:
        return ("DENON", d, 0, fn)
    if ext == 1:
        return ("SHARP", d, 0, fn)
    if ext == 3:                       # capture starts with the inverted half
        return ("DENON", d, 0, (~fn) & 0xFF)
    if ext == 2:
        return ("SHARP", d, 0, (~fn) & 0xFF)
    return None


DECODERS = [dec_nec_family, dec_kaseikyo, dec_sony, dec_rc5, dec_rc6, dec_denon, dec_jvc]


def decode(timings, freq=38000):
    fr = frames([abs(int(x)) for x in timings])
    if not fr:
        return None
    for dec in DECODERS:
        try:
            r = dec(fr, freq)
        except (IndexError, ValueError):
            r = None
        if r:
            return r
    return None
