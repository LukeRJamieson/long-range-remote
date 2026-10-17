#!/usr/bin/env python3
"""End-to-end check of build_db.py conversions: random IRDB rows -> firmware params -> firmware
encoder, compared with IrpTransmogrifier rendering the ORIGINAL row (protocol, D, S, F).
Usage: verify_db_conversion.py <irdb dir> <test_encoders binary> <IrpTransmogrifier jar> [samples]"""
import csv, os, random, re, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
import build_db as B

IRDB, TE, JAR = sys.argv[1], sys.argv[2], sys.argv[3]
N = int(sys.argv[4]) if len(sys.argv) > 4 else 120
rows = []
base = os.path.join(IRDB, "codes")
for brand in os.listdir(base):
    bd = os.path.join(base, brand)
    if not os.path.isdir(bd): continue
    for typ in os.listdir(bd):
        if typ not in B.IRDB_TYPES: continue
        for fn in os.listdir(os.path.join(bd, typ)):
            for r in csv.DictReader(open(os.path.join(bd, typ, fn), errors="ignore")):
                try: rows.append((r["protocol"], int(r["device"]), int(r["subdevice"]), int(r["function"])))
                except Exception: pass
random.seed(7)
byproto = {}
for r in rows: byproto.setdefault(r[0], []).append(r)
sample = []
for p, rs in byproto.items():            # stratified: every protocol gets tested
    if B.irdb_convert(p, *rs[0][1:]) is None: continue
    sample += random.sample(rs, min(len(rs), max(3, N * len(rs) // len(rows))))

def seq(t): return [abs(int(x)) for x in t.split(",") if x.strip()]
def irpt(p, d, s, f):
    name = {"nec1": "NEC1", "nec": "NEC1", "denon": "Denon", "nec1-f16": "NEC1", "nec2-f16": "NEC2"}.get(p.lower(), p)
    ps = []
    if name.startswith("NEC1-y"):
        ps.append("E=%d" % ((f ^ {"NEC1-y1": 0x7F, "NEC1-y2": 0xFE, "NEC1-y3": 0x7E}[name]) & 0xFF)); name = "NEC1-f16"
    if name.lower() in ("necx1", "necx2") and s < 0: s = d          # generator default for NECx
    if name != "Bose": ps.append("D=%d" % d)
    if s >= 0 and name not in ("Denon", "Sharp", "JVC", "RC5", "RC6", "Mitsubishi", "RECS80", "Bose", "Akai", "Emerson", "Sony12", "Sony15", "RCA", "RCA-38", "RCA-38(Old)", "JVC{2}", "Denon{1}", "Denon{2}"):
        ps.append("S=%d" % s)
    ps.append("F=%d" % f)
    o = subprocess.run(["java", "-jar", JAR, "render", "-r", "-n", ",".join(ps), name], capture_output=True, text=True)
    m = re.search(r"Freq=(\d+)Hz\[([^\]]*)\]\[([^\]]*)\]", o.stdout + o.stderr)
    return None if not m else (int(m.group(1)), seq(m.group(2)), seq(m.group(3)))

fails = tested = skipped = 0
for p, d, s, f in sample:
    fw = B.irdb_convert(p, d, s, f)
    ref = irpt(p, d, s, f)
    if ref is None: skipped += 1; continue
    out = subprocess.run([TE], input="FW:%d %d %d %d\n" % fw, capture_output=True, text=True).stdout.strip()
    fq, mf, first, rep = out.split("|")
    mi, mr = seq(first), seq(rep)
    ri, rr = ref[1], ref[2]
    n = len(ri) + 2 * len(rr) if rr else len(ri)
    def st(a, b):
        x = list(a)
        while len(x) < n and b: x += b
        return x[:n]
    a, b = st(ri, rr), st(mi, mr)
    ok = abs(ref[0] - int(fq)) <= 50 and len(a) == len(b) and all(abs(x - y) <= max(3, .01 * x) for x, y in zip(a, b))
    tested += 1
    if not ok:
        fails += 1
        print("FAIL", p, d, s, f, "->", fw, "\n  ref", a[:40], "\n  fw ", b[:40])
print("tested %d rows across %d protocols, skipped %d, failures %d" % (tested, len(set(r[0] for r in sample)), skipped, fails))
