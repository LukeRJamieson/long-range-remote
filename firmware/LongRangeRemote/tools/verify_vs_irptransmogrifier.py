#!/usr/bin/env python3
"""Compare firmware encoders (tools/test_encoders) against IrpTransmogrifier renders.
Usage: verify_vs_irptransmogrifier.py <test_encoders binary> <IrpTransmogrifier jar>"""
import random, re, subprocess, sys

TE, JAR = sys.argv[1], sys.argv[2]
# (IRP protocol name, D range, S range or None, F range)
CASES = [
    ("NEC1", 255, None, 255), ("NEC1", 255, 255, 255), ("NEC2", 255, None, 255), ("NECx1", 255, 255, 255),
    ("Pioneer", 255, None, 255), ("NEC1-y1", 255, None, 255), ("NEC1-y2", 255, None, 255), ("NEC1-y3", 255, None, 255),
    ("Aiwa", 255, 31, 255), ("Panasonic", 255, 255, 255), ("Denon-K", 15, 15, 4095), ("JVC-48", 255, 255, 255),
    ("Teac-K", 15, 255, 255), ("Sony12", 31, None, 127), ("Sony15", 255, None, 127), ("Sony20", 31, 255, 127),
    ("RC5", 31, None, 127), ("RC5x", 31, 127, 63), ("RC6", 255, None, 255), ("Denon", 31, None, 255),
    ("Denon{1}", 31, None, 255), ("Denon{2}", 31, None, 255), ("Sharp", 31, None, 255), ("JVC", 255, None, 255),
    ("JVC{2}", 255, None, 255), ("Mitsubishi", 255, None, 255), ("RECS80", 7, None, 63), ("Bose", None, None, 255),
    ("RCA", 15, None, 255), ("RCA-38", 15, None, 255), ("RCA-38(Old)", 15, None, 255), ("Akai", 7, None, 127),
    ("Emerson", 63, None, 63),
]

def parse_seq(txt):
    return [abs(int(x)) for x in txt.split(",") if x.strip()]

YMASK = {"NEC1-y1": 0x7F, "NEC1-y2": 0xFE, "NEC1-y3": 0x7E}

def irpt(proto, d, s, f):
    params = []
    if proto in YMASK:   # Yamaha styles = NEC1-f16 with E derived from F
        params.append("E=%d" % ((f ^ YMASK[proto]) & 0xFF))
        proto = "NEC1-f16"
    if d is not None: params.append("D=%d" % d)
    if s is not None: params.append("S=%d" % s)
    params.append("F=%d" % f)
    out = subprocess.run(["java", "-jar", JAR, "render", "-r", "-n", ",".join(params), proto],
                         capture_output=True, text=True)
    out = out.stdout + out.stderr
    m = re.search(r"Freq=(\d+)Hz\[([^\]]*)\]\[([^\]]*)\]\[([^\]]*)\]", out)
    if "not renderable" in out:
        return None
    if not m:
        raise RuntimeError("irpt failed: " + out)
    return int(m.group(1)), parse_seq(m.group(2)), parse_seq(m.group(3))

def mine(proto, d, s, f):
    line = "%s %d %d %d\n" % (proto, -1 if d is None else d, -1 if s is None else s, f)
    out = subprocess.run([TE], input=line, capture_output=True, text=True).stdout.strip()
    freq, minf, first, rep = out.split("|")
    return int(freq), parse_seq(first), parse_seq(rep)

def stream(intro, rep, n):
    s = list(intro)
    while len(s) < n and rep:
        s += rep
    return s[:n]

random.seed(1)
fails = 0
for proto, dr, sr, fr in CASES:
    for trial in range(4):
        d = None if dr is None else random.randint(0, dr)
        s = None if sr is None else random.randint(0, sr)
        f = random.randint(0, fr)
        r = irpt(proto, d, s, f)
        if r is None:
            print("%-12s (decode-only in IrpTransmogrifier, skipped)" % proto)
            break
        rf, ri, rr = r
        mf, mi, mr = mine(proto, d, s, f)
        n = len(ri) + 2 * len(rr) if rr else len(ri)
        a, b = stream(ri, rr, n), stream(mi, mr, n)
        ok = abs(rf - mf) <= 50 and len(a) == len(b) and all(abs(x - y) <= max(3, 0.01 * x) for x, y in zip(a, b))
        if not ok:
            fails += 1
            print("FAIL %-12s D=%s S=%s F=%s\n  irpt(%d): %s\n  mine(%d): %s" % (proto, d, s, f, rf, a[:60], mf, b[:60]))
    print("%-12s %s" % (proto, "ok" if not fails else "..."))
print("FAILURES:", fails)
sys.exit(1 if fails else 0)
