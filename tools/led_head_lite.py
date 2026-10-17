#!/usr/bin/env python3
"""
Long-range remote LITE - LED HEAD board. 99.8 x 44.8 mm with R3 corners, 2 layers: a wide rectangle
matching the Lite housing's body cross-section. Runs straight off a 4 x AA NiMH pack (4.4-5.4 V under
load, 4.8 V nominal). Through-hole LEDs and connector, SMD (1206) string resistors on the back.

  * 66 x Osram SFH 4545 (950 nm, +/-5 deg) on the FRONT in a 14 x 5 grid (6.8 mm along a row,
    7.0 mm between rows), with the four corner positions left
    empty for the M3 holes: rows of 12 / 14 / 14 / 14 / 12.
  * 33 strings of 2, all inside one row (every row holds an even count). One ballast resistor
    per string:
        LED_PWR (the AA pack, VBAT) -> 2 x SFH 4545 -> 24R -> LED_RTN
        I = (VBAT - 2 x 1.5) / 24:  fresh cells 5.4 V -> 100 mA (the SFH 4545's rating),
        most of the discharge 4.8-5.0 V -> 75-83 mA, nearly flat 4.4 V -> 58 mA
    33 strings -> up to 3.3 A peak, switched at 38/40 kHz (~1/3 duty): ~0.9 A average while a code
    is sent, drawn from the AA pack (the control board's 940 uF supplies the peaks).
  * The 33 x 24R resistors are 1206 pulse-withstanding thick film, 0.66 W (Panasonic ERJ-P08J240V),
    hand-solder pads, on the BACK in the bands between LED rows, each directly behind its string's
    second cathode, each row's in the gap on its outer side where there is room. Dissipation 0.24 W
    max, only while a carrier pulse is on.
  * Copper: F.Cu = LED_PWR pour (every string's first anode), B.Cu = LED_RTN pour (every resistor's far end).
  * J1: 2x5 0.1in pin header on the BACK, lying along the bottom edge (front view) in the margin below
    the LED field, centred; odd pins = LED_PWR, even = LED_RTN (the control board's J5 pinout,
    10-way ribbon). The ribbon leaves the head straight back at floor level.
  * 4 x M3 holes at (+-44.2, +-14.0): the empty corner positions of the grid.
  * No right-angle trace corners.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kigen  # noqa: E402
from kigen import G  # noqa: E402

NAME = "led_head_lite"
DIR = kigen.ROOT / NAME
SCH, PCB, NET = DIR / f"{NAME}.kicad_sch", DIR / f"{NAME}.kicad_pcb", DIR / "build" / f"{NAME}.net"

CX, CY = 150.0, 100.0
BOARD_W, BOARD_H, CORNER_R = 99.8, 44.8, 3.0
COLS, ROWS = 14, 5
COL_PITCH, ROW_PITCH = 6.8, 7.0
LEDS_PER_STRING = 2
R_PITCH = 3.1                                # pad pitch of R_1206_3216Metric_Pad1.30x1.75mm_HandSolder
HOLE_U, HOLE_V = 44.2, 14.0                  # the grid's corner positions
HOLES = [(CX + sx * HOLE_U, CY + sy * HOLE_V) for sx, sy in ((1, -1), (-1, -1), (-1, 1), (1, 1))]
J1_V = 19.3                                  # J1 centre line (v, the two pin rows at +-1.27) in the bottom margin
CORNER_U, CORNER_V = 40.3, 10.1              # |u| and |v| beyond these (both) = corner holes: nothing on the back
J1_ZONE = (-8.5, 8.5, 15.6, 22.4)            # back-side keep-out for J1 + its mated IDC socket (u0, u1, v0, v1)

LED_FP = "LED_THT:LED_D5.0mm"
LED_DS = "https://look.ams-osram.com/m/3456970c8eccd2cb/original/SFH-4545.pdf"
R_FP = "Resistor_SMD:R_1206_3216Metric_Pad1.30x1.75mm_HandSolder"
R_VAL = "24R 1206 0.66W"
R_MPN = "Panasonic ERJ-P08J240V"
CHAMFER = 1.5                                 # mm cut off each 90 degree corner (45 degree segment)
J1_FP = "Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical"


def col_u(c):
    return round(-((COLS - 1) / 2) * COL_PITCH + c * COL_PITCH, 4)


def row_v(r):
    return round(-((ROWS - 1) / 2) * ROW_PITCH + r * ROW_PITCH, 4)


def empty(r, c):
    return r in (0, ROWS - 1) and c in (0, COLS - 1)      # corner positions: the M3 holes


# ----------------------------------------------------------------------------- layout model
def build_layout():
    """
    LEDs in snake order. Returns (leds, strings):
      leds:    list of dict(ref, r, c, u, v, ltr) - ltr = anode on the left (row runs left -> right)
      strings: list of dict(n, leds=[led, ...] anode->cathode order, res)
    """
    leds, n = [], 0
    for r in range(ROWS):
        ltr = r % 2 == 0                          # rows alternate direction (strings never wrap): the last
        #                                           cathode alternates sides, so the end-of-row resistors next
        #                                           to the corner holes don't all crowd into one band
        cols = range(COLS) if ltr else range(COLS - 1, -1, -1)
        for c in cols:
            if empty(r, c):
                continue
            n += 1
            leds.append(dict(ref=f"D{n}", r=r, c=c, u=col_u(c), v=row_v(r), ltr=ltr))
    assert len(leds) % LEDS_PER_STRING == 0, len(leds)
    strings = []
    for s in range(len(leds) // LEDS_PER_STRING):
        strings.append(dict(n=s + 1, leds=leds[s * LEDS_PER_STRING:(s + 1) * LEDS_PER_STRING], res=f"R{s + 1}"))
    return leds, strings


LEDS, STRINGS = build_layout()


def pads_of(led):
    """(K, A) pad centres in local board coordinates."""
    if led["ltr"]:
        return (led["u"] + 1.27, led["v"]), (led["u"] - 1.27, led["v"])
    return (led["u"] - 1.27, led["v"]), (led["u"] + 1.27, led["v"])


def chamfer(pts, c=CHAMFER):
    """Replace every 90 degree corner of a polyline with a 45 degree cut (c mm along each leg)."""
    out = [pts[0]]
    for a, m, b in zip(pts, pts[1:], pts[2:]):
        d1 = (m[0] - a[0], m[1] - a[1])
        d2 = (b[0] - m[0], b[1] - m[1])
        l1, l2 = (d1[0] ** 2 + d1[1] ** 2) ** 0.5, (d2[0] ** 2 + d2[1] ** 2) ** 0.5
        if l1 < 1e-6 or l2 < 1e-6 or abs(d1[0] * d2[0] + d1[1] * d2[1]) > 1e-6 * l1 * l2:
            out.append(m)                                  # not a right angle: keep
            continue
        cc = min(c, l1 / 2, l2 / 2)
        out.append((round(m[0] - d1[0] / l1 * cc, 4), round(m[1] - d1[1] / l1 * cc, 4)))
        out.append((round(m[0] + d2[0] / l2 * cc, 4), round(m[1] + d2[1] / l2 * cc, 4)))
    out.append(pts[-1])
    return out


def chain_path(la, lb):
    """F.Cu path K(la) -> A(lb). Row-to-row wraps that change column run as an L through the
    empty grid position (corner / J1 gap) instead of cutting diagonally past a pad; the L's
    corner is chamfered (45 degrees) so no trace turns at a right angle."""
    k, _ = pads_of(la)
    _, an = pads_of(lb)
    if abs(k[1] - an[1]) < 0.01 or abs(k[0] - an[0]) < 0.01:
        return [k, an]
    corner = (an[0], k[1]) if abs(k[0]) < abs(an[0]) else (k[0], an[1])
    return chamfer([k, corner, an])


def chain_segments():
    """All F.Cu chain segments (K of LED n -> A of LED n+1 inside each string), local coords."""
    segs = []
    for st in STRINGS:
        for a, b in zip(st["leds"], st["leds"][1:]):
            pts = chain_path(a, b)
            segs += list(zip(pts, pts[1:]))
    return segs


def _seg_dist(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0 if L2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L2))
    return ((ax + t * dx - px) ** 2 + (ay + t * dy - py) ** 2) ** 0.5


def place_resistors():
    """
    Choose a back-side position for every string resistor: pad 1 directly above/below the
    string's last cathode in the neighbouring row gap, pad 2 R_PITCH to the left or right.
    Returns {res: (pad1, pad2)} in local coords.
    """
    segs = [s for s in chain_segments() if abs(s[0][1] - s[1][1]) > 0.1]   # only the row-to-row wraps matter
    hu, hv = BOARD_W / 2, BOARD_H / 2
    used = []                                     # (v, u0, u1) intervals already taken in a band
    out = {}

    def ok(p1, p2):
        for (x, y) in (p1, p2):
            if abs(x) > hu - 3.0 or abs(y) > hv - 3.0:
                return False
            if abs(x) > CORNER_U - 1.2 and abs(y) > CORNER_V - 1.2:
                return False
            if J1_ZONE[0] - 1.6 < x < J1_ZONE[1] + 1.6 and J1_ZONE[2] - 1.0 < y < J1_ZONE[3] + 1.0:
                return False
            for a, b in segs:
                if _seg_dist((x, y), a, b) < 1.8:
                    return False
        v = p1[1]
        u0, u1 = min(p1[0], p2[0]) - 1.6, max(p1[0], p2[0]) + 1.6
        for (vv, a0, a1) in used:
            if abs(vv - v) < 0.1 and not (u1 < a0 or u0 > a1):
                return False
        return True

    for st in STRINGS:
        last = st["leds"][-1]
        k, _ = pads_of(last)
        bands = [k[1] + ROW_PITCH / 2, k[1] - ROW_PITCH / 2]       # (below, above) in front view
        if last["r"] < ROWS - 1:
            bands.reverse()                       # every row but the last tries the gap above it first, the last
            #                                       row the margin below it: each of the 5 rows gets its own band
        done = False
        for vb in bands:
            for sgn in ((-1, 1) if k[0] > 0 else (1, -1)):        # resistor body towards the centre first
                p1 = (k[0], vb)
                p2 = (round(k[0] + sgn * R_PITCH, 4), vb)
                if ok(p1, p2):
                    out[st["res"]] = (p1, p2)
                    used.append((vb, min(p1[0], p2[0]) - 1.6, max(p1[0], p2[0]) + 1.6))
                    done = True
                    break
            if done:
                break
        if not done:
            raise RuntimeError(f"no place for {st['res']} (string {st['n']}, K at {k})")
    return out


# ----------------------------------------------------------------------------- schematic
def make_schematic():
    sch = kigen.Sch(NAME, "Long-range remote LITE - LED head (4 x AA NiMH)", "E",
                    "66 x SFH 4545 950nm, 33 strings of 2, 24R, from the 4 x AA NiMH pack, 99.8 x 44.8 mm", paper="A3")
    sch.text("LITE LED head: 66 x Osram SFH 4545 (950nm, +/-5deg) - 33 strings of 2 on the 4 x AA NiMH pack",
             (6 * G, 7 * G), 2.0, True)
    sch.text("I = (VBAT - 2 x 1.5V) / 24R: 100mA on fresh cells (5.4V), 75-83mA for most of the discharge.  ~3A peak, ~0.9A average "
             "while sending; switched at 38/40kHz by the control board (one low-side switch on LED_RTN; the supply itself is only on while ARM is high).",
             (6 * G, 9 * G))
    sch.text("14 x 5 grid, corners empty (M3 holes): rows of 6 / 7 / 7 / 7 / 6 strings, none crosses rows. Resistors on the back.",
             (6 * G, 10.5 * G))
    col_x = [8 * G, 38 * G, 68 * G, 98 * G]
    y0 = 15 * G
    per_col = 9
    LED = "Device:LED"
    for i, st in enumerate(STRINGS):
        x0, y = col_x[i // per_col], y0 + (i % per_col) * 6 * G
        rows = sorted({l["r"] + 1 for l in st["leds"]})
        sch.text(f"String {st['n']}: row {'/'.join(map(str, rows))}", (x0 - G, y - 2.2 * G), 1.0)
        sch.label("LED_PWR", (x0, y))
        prev = (x0, y)
        c1 = x0 + 3 * G
        for k, led in enumerate(st["leds"]):
            at = (c1 + k * 4 * G, y)                      # (2 LEDs per string)
            a = sch.pin_xy(LED, at, mirror_y=True, pin="2")
            kk = sch.pin_xy(LED, at, mirror_y=True, pin="1")
            sch.symbol(LED, led["ref"], "SFH 4545", at, mirror_y=True, footprint=LED_FP, datasheet=LED_DS,
                       ref_xy=(at[0], y - 3.2), val_xy=(at[0], y + 3.2), hide_val=True)
            sch.wire(prev, a)
            prev = kk
        at = (c1 + LEDS_PER_STRING * 4 * G + 0.5 * G, y)
        r1 = sch.pin_xy("Device:R", at, rot=90, pin="1")
        r2 = sch.pin_xy("Device:R", at, rot=90, pin="2")
        sch.symbol("Device:R", st["res"], R_VAL, at, rot=90, footprint=R_FP,
                   ref_xy=(at[0], y - 2.3), val_xy=(at[0], y + 2.6),
                   props=[("MPN", R_MPN), ("Power", "0.66W")])
        sch.wire(prev, r1)
        e = (r2[0] + 1 * G, y)
        sch.wire(r2, e)
        sch.label("LED_RTN", e)

    jx, jy = 22 * G, 86 * G
    sch.text("J1 - 2x5 0.1in pin header to control board J5 (pin n = pin n)", (jx - 10 * G, jy - 8 * G), 1.5, True)
    sch.text("Odd pins = LED_PWR, even pins = LED_RTN. Fitted on the BACK of the board.", (jx - 10 * G, jy - 6.6 * G), 1.0)
    J = "Connector_Generic:Conn_02x05_Odd_Even"
    sch.symbol(J, "J1", "TO CONTROL J5", (jx, jy), footprint=J1_FP,
               ref_xy=(jx + 1.27, jy - 7.62), val_xy=(jx + 1.27, jy + 8.89),
               props=[("MPN", "Sullins PRPC005DAAN-RC")])
    for p in range(2, 11, 2):
        sch.stub(J, (jx, jy), str(p), "LED_RTN", length=3 * G)
    sch.stub(J, (jx, jy), "9", "LED_PWR", length=3 * G)
    sch.tie(J, (jx, jy), ("1", "3", "5", "7", "9"))
    sch.flag("LED_PWR", (jx - 12 * G, jy + 10 * G))
    sch.flag("LED_RTN", (jx + 6 * G, jy + 10 * G))
    for i in range(4):
        at = (jx + 40 * G + i * 5 * G, jy)
        sch.symbol("Mechanical:MountingHole", f"H{i + 1}", "M3", at, footprint="MountingHole:MountingHole_3.2mm_M3",
                   ref_xy=(at[0], at[1] - 3.81), val_xy=(at[0], at[1] + 3.81))
    sch.write(SCH)


# ----------------------------------------------------------------------------- pcb
def rrect_pts(half, r, n=8):
    """Rounded-square polygon (local coords) for zones."""
    import math
    pts = []
    for (cx, cy, a0) in ((half - r, -half + r, -90), (half - r, half - r, 0), (-half + r, half - r, 90),
                         (-half + r, -half + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((CX + cx + r * math.cos(a), CY + cy + r * math.sin(a)))
    return pts


def make_pcb():
    import pcbnew
    b = kigen.Board(PCB, NET)
    F, B = pcbnew.F_Cu, pcbnew.B_Cu
    hu, hv = BOARD_W / 2, BOARD_H / 2
    b.outline_roundrect(CX - hu, CY - hv, CX + hu, CY + hv, CORNER_R)
    W_CHAIN = 0.6

    def at(u, v):
        return (round(CX + u, 4), round(CY + v, 4))

    for led in LEDS:
        k, a = pads_of(led)
        b.place_pads(led["ref"], at(*k), at(*a), hide_ref=True)
    for st in STRINGS:                                  # chain K(n) -> A(n+1) on F.Cu, wraps included
        for la, lb in zip(st["leds"], st["leds"][1:]):
            b.track([at(*p) for p in chain_path(la, lb)], W_CHAIN, F, b.netof(la["ref"], "1"))
    res = place_resistors()
    for st in STRINGS:
        p1, p2 = res[st["res"]]
        b.place_pads(st["res"], at(*p1), at(*p2), back=True, hide_ref=True)
        k, _ = pads_of(st["leds"][-1])
        b.track([at(*k), at(*p1)], W_CHAIN, B, b.netof(st["leds"][-1]["ref"], "1"))

    # J1 lies along the bottom edge: pin rows at v = J1_V -+ 1.27 (odd pins on the inner row), the five
    # pin pairs along u, centred. Try the four rotations; keep the one that gives that.
    for rot in (0, 90, 180, 270):
        if "J1" in b.fps:
            b.board.Remove(b.fps.pop("J1"))
        b.place_pad("J1", "1", at(0.0, J1_V - 1.27), rot=rot, back=True)
        p1, p2, p3 = (b.pad_xy("J1", n) for n in ("1", "2", "3"))
        if abs(p2[0] - p1[0]) < 0.01 and abs(p2[1] - p1[1] - 2.54) < 0.01 and abs(abs(p3[0] - p1[0]) - 2.54) < 0.01:
            du = 1 if p3[0] > p1[0] else -1                       # pin 1 at the -du end, centred overall
            b.board.Remove(b.fps.pop("J1"))
            b.place_pad("J1", "1", at(-du * 5.08, J1_V - 1.27), rot=rot, back=True)
            break
    else:
        raise RuntimeError("no rotation of J1 puts pin 2 below pin 1")
    b.ref_text("J1", at(-9.6, J1_V), 1.0, layer=pcbnew.B_SilkS)
    for i, (x, y) in enumerate(HOLES):
        b.place(f"H{i + 1}", x, y, hide_ref=True)
        b.keepout(kigen.circle_pts(x, y, 3.9, 24))

    edge = kigen.rrect_pts(CX, CY, hu - 0.6, CORNER_R - 0.6, half_y=hv - 0.6)
    b.zone("/LED_PWR", F, edge, priority=1, full=True, name="LED_PWR")
    b.zone("/LED_RTN", B, edge, priority=1, full=True, name="LED_RTN")

    FS, BS = pcbnew.F_SilkS, pcbnew.B_SilkS
    # labels in the LED-free strip along the top edge (no part values on silk)
    b.text("LONG-RANGE REMOTE LITE - LED HEAD", CX, CY - 20.4, FS, 1.0)
    b.text("LONG-RANGE REMOTE LITE LED HEAD", CX, CY - 20.6, BS, 0.9, mirror=True)
    b.text("J1 odd=PWR even=RTN", CX + 17.0, CY + J1_V, BS, 1.0, mirror=True)
    b.fill_and_save()
    return b


def check_netlist():
    comps, pad_net = kigen.parse_netlist(NET)
    nets = kigen.nets_of(pad_net)
    nd = len([c for c in comps if c.startswith("D")])
    nr = len([c for c in comps if c.startswith("R")])
    ns = len(STRINGS)
    assert nd == 66 and nr == ns == 33, (nd, nr)
    assert len(nets["/LED_PWR"]) == ns + 5, len(nets["/LED_PWR"])
    assert len(nets["/LED_RTN"]) == ns + 5, len(nets["/LED_RTN"])
    internal = [n for n in nets if n not in ("/LED_PWR", "/LED_RTN")]
    assert len(internal) == ns * LEDS_PER_STRING and all(len(nets[n]) == 2 for n in internal)
    print(f"netlist ok: {len(comps)} parts, {len(nets)} nets ({ns} strings x {LEDS_PER_STRING} internal nets)")


def main():
    kigen.write_project_files(DIR, NAME)
    make_schematic()
    kigen.export_netlist(SCH, NET)
    check_netlist()
    make_pcb()
    r = kigen.drc(PCB)
    print("DRC:", {k: v for k, v in r.items() if k != "report"})


if __name__ == "__main__":
    main()
