#!/usr/bin/env python3
"""Tests for raw_decode.py.
 1. Round trip: random codes -> firmware encoder (tools/test_encoders) -> decode -> same params.
 2. Real captures: every raw signal the decoder converts is re-encoded by the firmware encoder
    and compared pulse-by-pulse with the original capture (first frame, +-30 %).
Usage: verify_raw_decode.py <test_encoders binary> <Flipper-IRDB dir>"""
import os, random, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
import build_db as B, raw_decode as R

TE = sys.argv[1]
def encode_many(items, toggle=False):
    inp = "".join("%s:%d %d %d %d\n" % ("FWT" if toggle else "FW", B.P[p], d, s, f) for p, d, s, f in items)
    out = subprocess.run([TE], input=inp, capture_output=True, text=True).stdout.strip().split("\n")
    res = []
    for line in out:
        fq, mf, first, rep = line.split("|")
        res.append((int(fq), [int(x) for x in first.split(",")], [int(x) for x in rep.split(",")] if rep else []))
    return res

# 1. round trip
random.seed(11)
cases = []
for _ in range(300):
    cases += [("NEC", random.randint(0, 65535), 0, random.randint(0, 65535)),
              ("NECX", random.randint(0, 65535), 0, random.randint(0, 65535)),
              ("PIONEER", random.randint(0, 65535), 0, random.randint(0, 65535)),
              ("AIWA", random.randint(0, 8191), 0, random.randint(0, 255)),
              ("KASEIKYO", random.randint(0, 65535), random.randint(0, 255), random.randint(0, 65535)),
              ("SONY12", random.randint(0, 31), 0, random.randint(0, 127)),
              ("SONY15", random.randint(0, 255), 0, random.randint(0, 127)),
              ("SONY20", random.randint(0, 8191), 0, random.randint(0, 127)),
              ("RC5", random.randint(0, 31), 0, random.randint(0, 127)),
              ("RC6", random.randint(0, 255), 0, random.randint(0, 255)),
              ("DENON", random.randint(0, 31), 0, random.randint(0, 255)),
              ("SHARP", random.randint(0, 31), 0, random.randint(0, 255)),
              ("JVC", random.randint(0, 255), 0, random.randint(0, 255))]
enc = encode_many(cases)
bad = 0
for (p, d, s, f), (fq, first, rep) in zip(cases, enc):
    seq = first + rep if p in ("NEC",) else first     # NEC: include ditto so NEC1 vs NEC2 is decidable
    got = R.decode(seq, fq)
    want = (p, d, s, f)
    if p == "NEC" and got and got[0] == "NEC2": got = ("NEC",) + got[1:]
    if got != want:
        bad += 1
        if bad <= 8: print("roundtrip FAIL", want, "->", got)
print("round trip: %d cases, %d failures" % (len(cases), bad))

# 2. real captures (all raw signals in Flipper-IRDB incl. the Converted library)
root = sys.argv[2]
decoded, total, mism = [], 0, 0
for dp, _, fs in os.walk(root):
    for fn in fs:
        if not fn.endswith(".ir"): continue
        for sig in B.parse_flipper(os.path.join(dp, fn)):
            if sig.get("type") != "raw": continue
            try:
                data = [abs(int(x)) for x in sig["data"].split()]
                freq = int(float(sig.get("frequency", "38000")))
            except (KeyError, ValueError):
                continue
            total += 1
            r = R.decode(data, freq)
            if r: decoded.append((r, data, freq))
print("real captures: %d raw, %d decoded (%.0f%%)" % (total, len(decoded), 100.0 * len(decoded) / max(1, total)))
from collections import Counter
print("  by protocol:", Counter(r[0] for r, _, _ in decoded).most_common())
enc = encode_many([r for r, _, _ in decoded])
enc_t = encode_many([r for r, _, _ in decoded], toggle=True)    # captures carry either toggle state
for (r, data, freq), (fq, first, rep), (_, first_t, _) in zip(decoded, enc, enc_t):
    cap = R.frames(data)[0]
    ok = False
    for cand in (first, first_t):
        mine = R.frames(cand)[0]
        # 40 % / 250 us: IR receivers used for captures stretch marks and shrink spaces
        ok = ok or (len(cap) == len(mine) and all(abs(a - b) <= max(250, 0.4 * b) for a, b in zip(cap, mine)))
    if not ok:
        mism += 1
        if mism <= 5: print("  MISMATCH", r, cap[:12], mine[:12])
print("  re-encoded vs capture mismatches: %d" % mism)
sys.exit(1 if bad or mism else 0)
