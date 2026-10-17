#!/usr/bin/env python3
"""
Long-range remote LITE - CONTROL board, 4 x AA NiMH. 82 x 52 mm, 2 layers, through-hole except the two
DPAK MOSFETs Q2 and Q5 (soldered on the back). It sits under the lid: keys up through the lid, everything else on the back.

FRONT (keys): SW1-SW4 D-pad + SW5 SELECT + SW6 BACK, all Omron B3F-1000 6 mm. Each key shorts its GPIO to GND;
  the ESP32's internal pull-ups (~45k) hold them high, so there are no key resistors or expander:
    UP     -> J2 pin 15 GPIO18 (also the Waveshare board's own KEY button, active low)
    DOWN   -> J2 pin 11 GPIO3
    LEFT   -> J2 pin 12 GPIO44 (RXD)   RIGHT  -> R11 1k -> J2 pin 10 GPIO43 (TXD)
    SELECT -> J2 pin 5  GPIO0 (also the Waveshare board's BOOT button: hold SELECT at a reset = bootloader)
    BACK   -> J2 pin 16 GPIO14 (the display board's I2C SCL: its own pull-up holds it high. Nothing in the
              firmware uses that bus, and SCL held low with SDA idle is never an I2C start, so the display
              board's chips ignore it. The only other free header pins, GPIO19/20, are its USB-C data lines.)
  R11 protects GPIO43: the ESP32's boot ROM drives it as UART TX for a moment at power-up, so a key held
  then would short an output to GND without it (1k = 3.3 mA).
  (UP on pin 15 at the D-pad's end of J2, SELECT on pin 5 at its own end: no key track crosses another.
  GPIO1, an ADC pin, reads the pack.)

FRONT: the six keys only. Everything else is on the BACK.

BACK (LED power gate, behind SELECT's right-hand side): Q5 + R14 / R15 / C7 / C8 + Q6 + R16. No switch: the ESP32 turns the
  pack on only while it is sending (ARM, GPIO2), so the pack drains nothing the rest of the time.
    J1 pack + (BATT) -> Q5 IPD80P03P4L-07 P-MOSFET (DPAK, source = BATT, drain = VBAT, 12 mOhm max
    at -4.5 V) -> VBAT. R14 100k holds the gate at the source: Q5 off, the pack is disconnected from
    everything and the LEDs cannot light. ARM high -> R16 10k -> Q6 2N3904 -> pulls the gate to GND through
    R15 10k: Vgs = -0.91 x pack (-4.0 V at the 4.4 V cutoff, Vgs(th) max -2.0 V) and Q5 is on. C7 100nF
    from gate to drain is a soft start: the drain rises ~2 V/ms, so C1 / C2 charge at ~2 A instead of a
    spark. C8 1uF from gate to source keeps Q5 OFF when cells go in: without it, C7 would hold the gate
    near the (empty) drain as BATT jumps to ~5 V, turning Q5 fully on and jump-charging C1 / C2 at 10-20 A
    (C8 / C7 = 10: the gate follows the source to within ~0.5 V). The cost is a slower turn-on - C8
    discharges through R15 first (~10-20 ms) - so the firmware waits LED_ARM_SETTLE_MS = 30 ms.
    (A P-FET because a low-side N-FET would let the pack float when off, leaving its gate unreferenced.)

BACK:
  Power: J1 = the 4 x AA NiMH pack (Keystone 2478 holder, 4.4-5.4 V under load) -> Q5 -> VBAT. It feeds
    the LEDs only: the display board and ESP32 run from the display board's own 18650 (J2 pin 2 VBUS is
    not connected), so the AA cells can be taken out and charged in any NiMH charger.
    VBAT -> J5 odd pins (LED_PWR of the Lite LED head: 33 strings of 2 x SFH 4545 + 24R).
    C1, C2 470 uF + C3 100 nF on VBAT supply the ~3 A carrier pulses; the pack sees ~0.9 A average.
    R12 / R13 470k divider + C6 100 nF -> VBAT_SENSE (J2 pin 7, GPIO1 ADC): the PACK (BATT, before Q5)
    voltage / 2, so the firmware can read it while the LEDs are off (~0 V = no cells: it shows "AA out").
    Drain from the pack when idle: 5 uA divider (a NiMH cell loses ~30 uA to self-discharge anyway).
    Armed, while sending: ~50 uA gate divider + the LED current.
  LED switching - one low-side logic-level MOSFET on LED_RTN, driven straight from the ESP32:
    IR_DRV (GPIO17, 38 kHz carrier, drive strength 2) -> R6 47R -> Q2 AOD424 (LED_RTN -> GND)
    Q2 is a 20 V DPAK MOSFET specified at 2.5 V gate drive (5.7 mOhm max, Vgs(th) 0.5-1.6 V), so the 3.3 V
    GPIO turns it fully on with no gate driver. Its gate drive no longer depends on the AA cells.
    R6 47R slows the switching edge: with a fast edge the ribbon's inductance rings against the LEDs'
    capacitance and reverse-biases the strings (up to ~5.5 V per LED with 10R and a gate driver; the SFH 4545
    is rated 5 V). With 47R and the GPIO's own ~40R there is no reverse bias even with 120 nH of ribbon, the GPIO
    peaks at ~36 mA, and Q2 dissipates < 0.1 W up to 58 kHz carriers (sim/control_lite_sim.py).
    The LEDs only light while ARM *and* IR_DRV are high: ARM powers the LED side through Q5, IR_DRV switches
    Q2. Two GPIOs, two separate switches. R4 100k holds ARM low and R7 100k holds Q2's gate low while the
    ESP32 boots.

J2 = Waveshare ESP32-S3-RLCD-4.2 2x8 header (pin n = pin n via 16-way IDC ribbon).
J5 = Lite LED head J1 (10-way ribbon): odd pins = LED_PWR (VBAT), even pins = LED_RTN.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kigen  # noqa: E402
from kigen import G  # noqa: E402

NAME = "control_lite"
DIR = kigen.ROOT / NAME
SCH, PCB, NET = DIR / f"{NAME}.kicad_sch", DIR / f"{NAME}.kicad_pcb", DIR / "build" / f"{NAME}.net"
CX, CY, W, H = 150.0, 100.0, 82.0, 52.0
HOLE_U, HOLE_V = 37.5, 22.5
HOLES = [(CX + sx * HOLE_U, CY + sy * HOLE_V) for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1))]

FP = {
    "SW6": "Button_Switch_THT:SW_PUSH_6mm",          # the six tact buttons (name kept from the old footprint key)
    "DIP8": "Package_DIP:DIP-8_W7.62mm",
    "TO220": "Package_TO_SOT_THT:TO-220-3_Vertical",
    "TO92": "Package_TO_SOT_THT:TO-92_Inline_Wide",
    "CP10": "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm",
    "C2.5": "Capacitor_THT:C_Rect_L4.0mm_W2.5mm_P2.50mm",
    "C5": "Capacitor_THT:C_Disc_D5.1mm_W3.2mm_P5.00mm",
    "R": "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal",
    "DO201": "Diode_THT:D_DO-201AD_P12.70mm_Horizontal",
    "XH2": "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
    "HDR3": "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
    "IDC16": "Connector_IDC:IDC-Header_2x08_P2.54mm_Vertical",
    "IDC10": "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical",
    "M3": "MountingHole:MountingHole_3.2mm_M3",
    "DPAK": "Package_TO_SOT_SMD:TO-252-2",
}
# (ref, label, key net, centre u, centre v, switch size)
BUTTONS = [
    ("SW1", "UP", "BTN_UP", -22.0, -10.0, 6),
    ("SW2", "DOWN", "BTN_DOWN", -22.0, 10.0, 6),
    ("SW3", "LEFT", "BTN_LEFT", -32.0, 0.0, 6),
    ("SW4", "RIGHT", "BTN_RIGHT", -12.0, 0.0, 6),
    ("SW5", "SELECT", "BTN_SEL", 24.0, 0.0, 6),
    ("SW6", "BACK", "BTN_BACK", -8.0, 14.0, 6),      # lower right of the D-pad (where a Flipper's BACK is)
]
J2_MAP = {"1": None, "2": None, "3": "GND", "4": "GND", "5": "BTN_SEL", "6": None, "7": "VBAT_SENSE", "8": None,
          "9": "ARM", "10": "KEY_TXD", "11": "BTN_DOWN", "12": "BTN_LEFT", "13": "IR_DRV", "14": None,
          "15": "BTN_UP", "16": "BTN_BACK"}

# Q2 (DPAK, on the back) right under J5; R6 / R7 beside its gate lead
Q2_U, Q2_V, Q2_ROT = -13.5, -9.8, 90
R6_UV = ((-8.4, -13.5), (-8.4, -5.88))      # top IR_DRV, bottom Q2_G (next to Q2's gate lead)
R7_UV = ((-4.0, -13.5), (-4.0, -5.88))      # top IR_DRV, bottom GND (the VBAT trunk runs between R6 and R7)

# ----------------------------------------------------------------------------- schematic
def make_schematic():
    """A3 sheet in four blocks. Inside a block the parts are wired; net labels only where a net leaves a block
    (BATT/VBAT/ARM/IR_DRV/VBAT_SENSE/keys), plus a label on every named net so the board keeps its net names.
    Every T-joint is a wire end with a junction dot."""
    s = kigen.Sch(NAME, "Long-range remote LITE - control board (4 x AA NiMH)", "G",
                  "AA pack -> power gate -> LEDs; LED switch; pack sense; six keys; connectors",
                  paper="A3", date="2026-10-11")
    R, C, CP = "Device:R", "Device:C", "Device:C_Polarized"
    QP, QN, QB = "Device:Q_PMOS_GDS", "Device:Q_NMOS_GDS", "Transistor_BJT:2N3904"
    MPN_100K, MPN_100N = "Yageo MFR-25FBF52-100K", "TDK FG18X7R1H104KNT06"

    def pin(lib, at, p, rot=0, mir=False):
        return s.pin_xy(lib, at, rot, mir, p)

    def path(*pts):                                   # polyline of wires
        for a_, b_ in zip(pts, pts[1:]):
            s.wire(a_, b_)

    def gnd(p, drop=2 * G):                          # wire down to a GND symbol
        e = (p[0], round(p[1] + drop, 4))
        s.wire(p, e)
        s.power("GND", e)

    def vpart(lib, ref, val, x, y, mpn, fp, side="right"):
        """Vertical two-pin part centred at (x, y): pin 1 at the top. Fields beside it."""
        dx, just = (2.54, "left") if side == "right" else (-2.54, "right")
        s.symbol(lib, ref, val, (x, y), footprint=fp, ref_xy=(x + dx, y - 1.27), val_xy=(x + dx, y + 1.27),
                 justify=just, props=[("MPN", mpn)])
        return pin(lib, (x, y), "1"), pin(lib, (x, y), "2")

    def hpart(lib, ref, val, x, y, mpn, fp):
        """Horizontal resistor centred at (x, y): pin 1 on the left. Ref above, value below."""
        s.symbol(lib, ref, val, (x, y), rot=90, footprint=fp, ref_xy=(x, y - 2.54), val_xy=(x, y + 2.54),
                 justify=None, props=[("MPN", mpn)])
        return pin(lib, (x, y), "1", 90), pin(lib, (x, y), "2", 90)

    s.text("LITE control board - LEDs from a 4 x AA NiMH pack; keys straight to ESP32 GPIOs", (15.24, 15.24), 2.0, True)
    s.text("The LEDs light only while ARM (GPIO2) powers them through Q5 AND the carrier on IR_DRV (GPIO17) switches Q2. "
           "LED head: 33 strings of 2 x SFH 4545 + 24R.", (15.24, 20.32), 1.27)

    # ============ A. battery, pack sense, power gate, reservoir =================================================
    s.text("A. Battery, pack sense and LED power gate", (17.78, 43.18), 1.5, True)
    yb, yg, yv = 55.88, 71.12, 83.82                 # BATT bus, Q5 gate node, VBAT line
    s.symbol("Connector_Generic:Conn_01x02", "J1", "4xAA (JST-XH)", (22.86, yb), mirror_y=True, footprint=FP["XH2"],
             ref_xy=(19.05, yb - 3.81), val_xy=(19.05, yb + 6.35), justify="right",   # mirrored: reads left-aligned
             props=[("MPN", "JST B2B-XH-A")])
    j1a, j1b = pin("Connector_Generic:Conn_01x02", (22.86, yb), "1", 0, True), \
        pin("Connector_Generic:Conn_01x02", (22.86, yb), "2", 0, True)
    path(j1b, (36.83, j1b[1]))
    gnd((36.83, j1b[1]), 5.08)
    s.flag("GND", (22.86, 86.36))
    # pack sense (R12 / R13 / C6) hanging from BATT
    xs = 45.72
    r12 = vpart(R, "R12", "470k", xs, 63.5, "Yageo MFR-25FBF52-470K", FP["R"])
    r13 = vpart(R, "R13", "470k", xs, 78.74, "Yageo MFR-25FBF52-470K", FP["R"])
    c6 = vpart(C, "C6", "100nF 50V", 58.42, 78.74, MPN_100N, FP["C2.5"])
    path((xs, yb), r12[0])
    path(r12[1], (xs, yg))
    path((xs, yg), r13[0])
    path((xs, yg), (58.42, yg), c6[0])
    path((xs, yg), (38.1, yg))
    s.junction((xs, yg))
    s.label("VBAT_SENSE", (38.1, yg), (-1, 0))
    gnd(r13[1])
    gnd(c6[1])
    # Q5 power gate: source on BATT (top), drain = VBAT (below), gate node to its left
    q5 = (109.22, yg)
    s.symbol(QP, "Q5", "IPD80P03P4L-07", q5, rot=180, mirror_y=True, footprint=FP["DPAK"],
             ref_xy=(114.3, yg - 1.27), val_xy=(114.3, yg + 1.27), justify="left",
             props=[("MPN", "Infineon IPD80P03P4L07ATMA2")])
    q5g, q5d, q5s = (pin(QP, q5, n, 180, True) for n in ("1", "2", "3"))
    r14 = vpart(R, "R14", "100k", 81.28, 63.5, MPN_100K, FP["R"])
    c8 = vpart(C, "C8", "1uF 50V", 91.44, 63.5, "TDK FG24X7R1H105KRT06", FP["C5"])
    c7 = vpart(C, "C7", "100nF 50V", 99.06, 77.47, MPN_100N, FP["C2.5"], side="left")
    r15 = vpart(R, "R15", "10k", 73.66, 78.74, "Yageo MFR-25FBF52-10K", FP["R"])
    # BATT bus: J1 -> R12 -> R14 -> C8 -> Q5 source
    path(j1a, (xs, yb), (81.28, yb), (91.44, yb), (q5s[0], yb), q5s)
    for x in (xs, 81.28, 91.44):
        s.junction((x, yb))
    s.label("BATT", (j1a[0] + 1.27, yb), (1, 0))
    path((81.28, yb), r14[0])
    path((91.44, yb), c8[0])
    # gate node: R15 corner -> R14 -> C8 -> C7 -> Q5 gate
    path((73.66, yg), (81.28, yg), (91.44, yg), (99.06, yg), q5g)
    for x in (81.28, 91.44, 99.06):
        s.junction((x, yg))
    path(r14[1], (81.28, yg))
    path(c8[1], (91.44, yg))
    path((99.06, yg), c7[0])
    path((73.66, yg), r15[0])
    s.label("PWR_G", (73.66, yg), (-1, 0))
    # VBAT line: C7 -> Q5 drain -> C1 / C2 / C3 -> to J5 (block B)
    xc = (127.0, 142.24, 157.48)
    path(c7[1], (99.06, yv), (q5d[0], yv), (xc[0], yv), (xc[1], yv), (xc[2], yv), (170.18, yv))
    path(q5d, (q5d[0], yv))
    for x in (q5d[0],) + xc:
        s.junction((x, yv))
    s.label("VBAT", (170.18, yv), (1, 0))
    for ref, x in (("C1", xc[0]), ("C2", xc[1])):
        c = vpart(CP, ref, "470uF 25V", x, 90.17, "Panasonic EEU-FR1E471", FP["CP10"])
        path((x, yv), c[0])
        gnd(c[1])
    c3 = vpart(C, "C3", "100nF 50V", xc[2], 90.17, MPN_100N, FP["C2.5"])
    path((xc[2], yv), c3[0])
    gnd(c3[1])
    # Q6 pulls the gate down through R15 while ARM is high
    q6 = (71.12, 95.25)
    s.symbol(QB, "Q6", "2N3904", q6, footprint=FP["TO92"], ref_xy=(76.2, 93.98), val_xy=(76.2, 96.52),
             justify="left", props=[("MPN", "onsemi 2N3904BU")])
    q6e, q6b, q6c = (pin(QB, q6, n) for n in ("1", "2", "3"))
    path(r15[1], q6c)
    s.label("PWR_SW", ((r15[1][0]), round((r15[1][1] + q6c[1]) / 2, 4)), (1, 0))
    gnd(q6e)
    r16 = hpart(R, "R16", "10k", 53.34, q6b[1], "Yageo MFR-25FBF52-10K", FP["R"])
    path(r16[1], q6b)
    s.label("Q6_B", (r16[1][0] + 1.27, q6b[1]), (1, 0))
    r4 = vpart(R, "R4", "100k", 44.45, 104.14, MPN_100K, FP["R"], side="left")
    path(r16[0], (44.45, q6b[1]), (38.1, q6b[1]))
    path((44.45, q6b[1]), r4[0])
    s.junction((44.45, q6b[1]))
    s.label("ARM", (38.1, q6b[1]), (-1, 0))
    gnd(r4[1])
    for i, t in enumerate((
            "Q5 (P-FET) connects the pack to the LEDs only while ARM is high. R14 holds it off; Q6 pulls its gate",
            "down through R15 (Vgs = -0.91 x pack). C7 (gate-drain) is a soft start, C8 (gate-source) keeps Q5 off",
            "when cells go in. R4 holds ARM low while the ESP32 boots. Pack sense: BATT / 2 (470k + 470k, 5 uA) on GPIO1.")):
        s.text(t, (17.78, 119.38 + i * 3.81), 1.27)

    # ============ B. LED switch and LED head connector ==========================================================
    s.text("B. LED switch (Q2) and LED head connector (J5)", (195.58, 43.18), 1.5, True)
    J10 = "Connector_Generic:Conn_02x05_Odd_Even"
    j5 = (231.14, 69.85)
    s.symbol(J10, "J5", "TO LED HEAD", j5, footprint=FP["IDC10"], ref_xy=(232.41, 60.96), val_xy=(232.41, 80.01),
             props=[("MPN", "Wurth 61201021621")])
    s.tie(J10, j5, ("1", "3", "5", "7", "9"))
    s.tie(J10, j5, ("2", "4", "6", "8", "10"))
    p5, p6 = pin(J10, j5, "5"), pin(J10, j5, "6")
    path(p5, (213.36, p5[1]))
    s.junction(p5)
    s.label("VBAT", (213.36, p5[1]), (-1, 0))
    q2 = (248.92, 87.63)
    s.symbol(QN, "Q2", "AOD424", q2, footprint=FP["DPAK"], ref_xy=(254.0, 86.36), val_xy=(254.0, 88.9),
             justify="left", props=[("MPN", "Alpha & Omega AOD424")])
    q2g, q2d, q2s = (pin(QN, q2, n) for n in ("1", "2", "3"))
    path(p6, (q2d[0], p6[1]), q2d)
    s.junction(p6)
    s.label("LED_RTN", (q2d[0], 76.2), (1, 0))
    gnd(q2s)
    r6 = hpart(R, "R6", "47R", 231.14, q2g[1], "Yageo MFR-25FBF52-47R", FP["R"])
    path(r6[1], q2g)
    s.label("Q2_G", (r6[1][0] + 1.27, q2g[1]), (1, 0))
    r7 = vpart(R, "R7", "100k", 220.98, 95.25, MPN_100K, FP["R"], side="left")
    path(r6[0], (220.98, q2g[1]), (213.36, q2g[1]))
    path((220.98, q2g[1]), r7[0])
    s.junction((220.98, q2g[1]))
    s.label("IR_DRV", (213.36, q2g[1]), (-1, 0))
    gnd(r7[1])
    for i, t in enumerate((
            "J5 -> LED head J1 over the 10-way ribbon: odd pins VBAT (LED_PWR), even pins LED_RTN.",
            "Q2 is logic-level (5.7 mOhm max at Vgs 2.5 V): GPIO17 drives it directly. R6 47R slows its edge so",
            "the ribbon can't ring the LEDs into reverse bias (sim/). R7 holds Q2 off while the ESP32 boots.")):
        s.text(t, (195.58, 116.84 + i * 3.81), 1.27)

    # ============ C. display connector ==========================================================================
    s.text("C. J2 - Waveshare ESP32-S3-RLCD-4.2 2x8 header (16-way ribbon, pin n = pin n)", (17.78, 144.78), 1.5, True)
    J16 = "Connector_Generic:Conn_02x08_Odd_Even"
    j2 = (76.2, 177.8)
    s.symbol(J16, "J2", "TO WAVESHARE", j2, footprint=FP["IDC16"], ref_xy=(77.47, 166.37), val_xy=(77.47, 191.77),
             props=[("MPN", "Wurth 61201621621")])
    for p_, n in J2_MAP.items():
        if n == "GND":
            continue
        s.stub(J16, j2, p_, n, length=3 * G)
    p3, p4 = pin(J16, j2, "3"), pin(J16, j2, "4")
    path(p3, (48.26, p3[1]), (48.26, 193.04))          # GND pins taken round the label columns
    s.power("GND", (48.26, 193.04))
    path(p4, (104.14, p4[1]), (104.14, 193.04))
    s.power("GND", (104.14, 193.04))
    for i, t in enumerate((
            "Used: 3/4 GND, 5 GPIO0 SELECT (also BOOT), 7 GPIO1 pack / 2, 9 GPIO2 ARM, 10 GPIO43 RIGHT (via R11), 11 GPIO3 DOWN,",
            "12 GPIO44 LEFT, 13 GPIO17 IR_DRV, 15 GPIO18 UP (also KEY), 16 GPIO14 BACK (I2C SCL, 2.2k pull-up on the display board).",
            "Not connected: 1 3V3, 2 VBUS, 6/8 USB D-/D+, 14 SDA. The display board and ESP32 run from their own 18650.")):
        s.text(t, (17.78, 203.2 + i * 3.81), 1.27)
    s.text("Mounting holes", (127.0, 166.37), 1.27, True)
    for i in range(4):
        at_ = (129.54 + i * 10.16, 175.26)
        s.symbol("Mechanical:MountingHole", f"H{i + 1}", "M3", at_, footprint=FP["M3"],
                 ref_xy=(at_[0], at_[1] - 3.81), val_xy=(at_[0], at_[1] + 3.81))

    # ============ D. keys ======================================================================================
    s.text("D. Keys - each shorts its GPIO to GND; ESP32 internal pull-ups", (187.96, 140.97), 1.5, True)
    SW = "Switch:SW_Push"
    for i, (ref, lbl, net, _, _, _) in enumerate(BUTTONS):
        y = 152.4 + i * 10.16
        at_ = (228.6, y)
        s.symbol(SW, ref, lbl, at_, footprint=FP["SW6"], ref_xy=(228.6, y - 3.81), val_xy=(228.6, y + 2.54),
                 props=[("MPN", "Omron B3F-1000 (6mm)")])
        a_, b_ = pin(SW, at_, "1"), pin(SW, at_, "2")
        path(b_, (238.76, y))
        gnd((238.76, y))
        if net == "BTN_RIGHT":                       # GPIO43 (UART TX at boot) through R11
            r11 = hpart(R, "R11", "1k", 203.2, y, "Yageo MFR-25FBF52-1K", FP["R"])
            path(r11[1], a_)
            s.label("BTN_RIGHT", (r11[1][0] + 1.27, y), (1, 0))
            path(r11[0], (195.58, y))
            s.label("KEY_TXD", (195.58, y), (-1, 0))
        else:
            path((213.36, y), a_)
            s.label(net, (213.36, y), (-1, 0))
    s.text("R11: GPIO43 is UART TX during the boot ROM; 1k limits the current if RIGHT is held at power-up.",
           (187.96, 215.9), 1.27)
    s.write(SCH)


def check_netlist():
    comps, pad_net = kigen.parse_netlist(NET)
    nets = kigen.nets_of(pad_net)
    exp = {
        "/BATT": {"J1.1", "Q5.3", "R14.1", "R12.1", "C8.1"},
        "/PWR_G": {"Q5.1", "R14.2", "R15.1", "C7.1", "C8.2"},
        "/PWR_SW": {"R15.2", "Q6.3"},
        "/Q6_B": {"R16.2", "Q6.2"},
        "/VBAT": {"Q5.2", "C1.1", "C2.1", "C3.1", "C7.2", "J5.1", "J5.3", "J5.5", "J5.7", "J5.9"},
        "/LED_RTN": {"Q2.2", "J5.2", "J5.4", "J5.6", "J5.8", "J5.10"},
        "/ARM": {"J2.9", "R4.1", "R16.1"},
        "/IR_DRV": {"J2.13", "R6.1", "R7.1"},
        "/Q2_G": {"R6.2", "Q2.1"},
        "/VBAT_SENSE": {"R12.2", "R13.1", "C6.1", "J2.7"},
        "/BTN_UP": {"J2.15", "SW1.1"}, "/BTN_DOWN": {"J2.11", "SW2.1"}, "/BTN_LEFT": {"J2.12", "SW3.1"},
        "/BTN_RIGHT": {"R11.2", "SW4.1"}, "/KEY_TXD": {"J2.10", "R11.1"}, "/BTN_SEL": {"J2.5", "SW5.1"},
        "/BTN_BACK": {"J2.16", "SW6.1"},
    }
    bad = {k: sorted(nets.get(k, set()) ^ v) for k, v in exp.items() if nets.get(k) != v}
    assert not bad, bad
    gnd = nets["GND"]
    for p in ["J1.2", "C1.2", "C2.2", "C3.2", "R13.2", "C6.2", "R4.2", "R7.2", "Q2.3", "J2.3", "J2.4", "Q6.1",
              "SW1.2", "SW2.2", "SW3.2", "SW4.2", "SW5.2", "SW6.2"]:
        assert p in gnd, p
    print(f"netlist ok: {len(comps)} parts, {len(nets)} nets")


# ----------------------------------------------------------------------------- pcb
def make_pcb():
    import pcbnew
    b = kigen.Board(PCB, NET)
    b.outline_roundrect(CX - W / 2, CY - H / 2, CX + W / 2, CY + H / 2, 3.0)

    def at(u, v):
        return (round(CX + u, 4), round(CY + v, 4))

    FS, BS = pcbnew.F_SilkS, pcbnew.B_SilkS
    # ---- front: keys
    for ref, lbl, _, u, v, size in BUTTONS:
        half = (3.25, 2.25)                                          # footprint origin = top-left pad
        b.place(ref, *at(u - half[0], v - half[1]))
        b.ref_text(ref, at(u - 6.0, v + 3.0) if ref == "SW2" else at(u, v + 4.9), 1.0)
        b.text(lbl, *at(u, v - 4.9), FS, 1.0)
    # ---- back, front half (towards the display / LED head): ribbons, the LED switch
    b.place_pad("J5", "1", at(-14.5, -19.23), rot=90, back=True)     # LED head, 10-way (pins run to -u)
    b.place_pad("J2", "1", at(16.0, -19.23), rot=90, back=True)      # Waveshare, 16-way
    # Q2 AOD424 DPAK right under J5: tab (drain = LED_RTN) towards J5, gate / source leads towards +v
    b.place("Q2", *at(Q2_U, Q2_V), rot=Q2_ROT, back=True)
    # pack divider: both VBAT_SENSE ends at u 18.3, so that node is one short link; its filter cap C6 right
    # under J2 pin 7 (the ADC input)
    b.place_pads("R13", at(18.3, -12.0), at(25.92, -12.0), back=True)  # VBAT_SENSE -> GND
    b.place_pads("R12", at(25.92, -9.0), at(18.3, -9.0), back=True)    # BATT -> VBAT_SENSE
    b.place_pads("C6", at(9.6, -14.5), at(9.6, -12.0), back=True)      # ADC filter
    b.place_pads("R6", at(*R6_UV[0]), at(*R6_UV[1]), back=True)    # 47R GPIO17 -> Q2 gate (slow edge, see top)
    # J2's two series / pull parts straight under the header, each a few mm from its own J2 pin:
    b.place_pads("R4", at(5.84, -14.5), at(5.84, -6.88), back=True)    # ARM pull-down, under J2 pin 9
    b.place_pads("R11", at(1.5, -14.5), at(1.5, -6.88), back=True)     # 1k GPIO43 (J2 pin 10) -> RIGHT
    b.place_pads("R7", at(*R7_UV[0]), at(*R7_UV[1]), back=True)    # 100k IR_DRV pull-down (holds Q2 off through R6)
    # ---- back, rear half: switches, arm circuit, reservoir, battery / 5 V inputs, key logic
    b.place_pads("C1", at(12.0, 13.0), at(17.0, 13.0), back=True)    # 470uF on VBAT
    b.place_pads("C2", at(24.0, 13.0), at(29.0, 13.0), back=True)
    b.place_pads("J1", at(18.0, 23.0), at(20.5, 23.0), back=True)    # 4 x AA pack
    b.place_pads("C3", at(26.5, 21.0), at(29.0, 21.0), back=True)      # (between C2 and J1, clear of Q5)
    # ---- back, behind SELECT's right-hand side: the LED power gate. The front carries the six keys
    # and nothing else; the gate's lead tails come through on the front clear of SELECT's cap base (u > 31).
    b.place_pad("Q6", "1", at(35.8, 1.08), rot=90, back=True)        # 2N3904 E B C along -v: C (top) meets R15,
    #                                                                  B meets R16 beside it, E into the pour
    b.place_pads("C7", at(31.0, -4.0), at(31.0, -1.5), back=True)    # soft-start 100nF, gate-drain
    b.place_pads("R16", at(38.4, -16.0), at(38.4, -8.38), back=True)  # 10k ARM -> Q6 base
    b.place_pads("C8", at(38.4, 4.2), at(33.4, 4.2), back=True)      # 1uF gate-source, right beside Q5 S / G
    b.place_pad("Q5", "1", at(33.72, 7.96), rot=270, back=True)      # DPAK on the back (mirrored: gate at u 33.7,
    #                                                                  source at u 38.3), leads towards -v,
    #                                                                  tab (drain = VBAT) at u 36.0, v 14.3
    b.place_pads("R14", at(32.0, -16.0), at(32.0, -8.38), back=True)  # 100k gate -> source (off)
    b.place_pads("R15", at(35.0, -16.0), at(35.0, -8.38), back=True)  # 10k gate -> Q6
    for i, (x, y) in enumerate(HOLES):
        b.place(f"H{i + 1}", x, y, hide_ref=True)
        b.keepout(kigen.circle_pts(x, y, 3.8, 24), tracks=True, vias=True)
    # no tracks within 0.9 mm of the board edge (the autorouter otherwise squeezes wide power
    # tracks up to the edge, under KiCad's 0.4 mm copper-to-edge rule)
    x0, x1, y0, y1 = CX - W / 2, CX + W / 2, CY - H / 2, CY + H / 2
    e = 0.9
    for (a0, a1, b0, b1) in ((x0, x1, y0, y0 + e), (x0, x1, y1 - e, y1), (x0, x0 + e, y0, y1), (x1 - e, x1, y0, y1)):
        b.keepout([(a0, b0), (a1, b0), (a1, b1), (a0, b1)], tracks=True, vias=True)

    # J5's five LED_RTN pins (and five VBAT pins) tied in a row on the back before autorouting, so the
    # router can't leave one of the parallel pins out
    for row, net in (("2", "/LED_RTN"), ("1", "/VBAT")):
        pts = [b.pad_xy("J5", str(int(row) + 2 * i)) for i in range(5)]
        b.track(pts, 1.5, pcbnew.B_Cu, net)
    # Power paths drawn before autorouting (the router left one or the other unconnected before):
    # LED_RTN: J5's return pins -> front copper round the end of J5 -> two 1.0 mm vias -> Q2's tab (drain).
    # All of the LED current (up to ~3.3 A peak) flows here.
    vu = (-12.0, -14.0)
    b.track([b.pad_xy("J5", "2"), at(-12.0, -21.77), at(-12.0, -15.6), at(-14.0, -15.6)], 1.5, pcbnew.F_Cu, "/LED_RTN")
    for u in vu:
        b.via(at(u, -15.6), "/LED_RTN", size=1.0, drill=0.5)
        b.track([at(u, -15.6), at(u, -12.5)], 1.5, pcbnew.B_Cu, "/LED_RTN")
    # Q2's source: stitched to both ground pours right beside the pad (the LED current returns to the pack's -)
    b.track([b.pad_xy("Q2", "3"), at(-17.6, -4.76)], 1.2, pcbnew.B_Cu, "GND")
    for v in (-4.0, -5.5):
        b.via(at(-17.6, v), "GND", size=1.0, drill=0.5)
    b.track([at(-17.6, -4.0), at(-17.6, -5.5)], 1.0, pcbnew.B_Cu, "GND")
    # The pack's current path, drawn by hand: J1 (+) -> up and along the front, clear of C1-C3 -> up over Q5
    # (SMD, on the back) -> C8's BATT pad, which is right beside Q5's source lead -> back copper to the
    # source. Only this leg carries the LED current; R12 / R14 (microamps) are left to the router at
    # normal width. (Routed automatically, the 1.5 mm BATT net went round C1 and the left edge: 102 mm.)
    b.track([b.pad_xy("J1", "1"), at(18.0, 18.5), at(33.0, 18.5), at(36.0, 15.5), at(36.0, 7.5),
             at(38.4, 5.1), b.pad_xy("C8", "1")], 1.5, pcbnew.F_Cu, "/BATT")
    b.track([b.pad_xy("C8", "1"), b.pad_xy("Q5", "3")], 1.5, pcbnew.B_Cu, "/BATT")
    # UP (J2 pin 15) by hand: down the corridor between R7 and R11, then straight along the front under J5's
    # LED_RTN loop and through R6 / R7's bodies to the key (~25 mm).
    b.track([b.pad_xy("J2", "15"), at(-2.6, -18.41), at(-2.6, -11.0), at(-17.6, -11.0), at(-18.75, -12.15),
             at(-18.75, -12.25)], 0.3, pcbnew.F_Cu, "/BTN_UP")
    # SELECT (J2 pin 5) down the front, past the pack divider's pads, to the key: the router took it round
    # R12 / R13 on the back (55 mm); this is 22 mm.
    b.track([b.pad_xy("J2", "5"), at(10.92, -13.0), at(17.92, -6.0), at(19.5, -6.0), at(20.75, -4.75),
             at(20.75, -2.25)], 0.4, pcbnew.F_Cu, "/BTN_SEL")
    # The key traces fence off a patch of the front ground pour between UP/RIGHT and J2; without a via of its
    # own it is an island and gets removed. This one ties it to the back pour (found by searching for the spot
    # that fills the most front pour while clearing everything else by 0.3 mm).
    b.via(at(-11.0, -1.0), "GND", size=0.8, drill=0.4)
    # VBAT trunk on the back: J5's supply pins -> past Q2's right-hand side (between R6 and R7) -> along the
    # board between the keys' leads -> C7.
    b.track([b.pad_xy("J5", "1"), at(-9.0, -19.23), at(-6.3, -16.53), at(-6.3, -1.04), at(18.31, -1.04),
             at(19.15, -0.2), at(29.7, -0.2), b.pad_xy("C7", "2")], 1.5, pcbnew.B_Cu, "/VBAT")
    # ... and on from C7 down to C2 (and so to Q5's drain and C1) on the front copper, clear of SELECT's leads
    b.track([b.pad_xy("C7", "2"), at(31.0, 4.95), at(24.0, 11.95), b.pad_xy("C2", "1")], 1.5, pcbnew.F_Cu, "/VBAT")
    for ref, (x1, y1) in ((r, b.pad_xy(r, "1")) for r in ("R4", "R6", "R7", "R11", "R12", "R13",
                                                          "R14", "R15", "R16")):
        x2, y2 = b.pad_xy(ref, "2")                                       # refs inside the resistor bodies,
        along = 90 if abs(x1 - x2) < 0.1 else 0                           # along them (the angle is relative
        b.ref_text(ref, ((x1 + x2) / 2, (y1 + y2) / 2), 1.0,              # to the footprint: see below)
                   rot=(b.fps[ref].GetOrientationDegrees() + along) % 360, layer=BS)
    b.ref_text("C7", at(31.0, -6.4), 1.0, layer=BS)
    for ref, uv in (("C1", (14.5, 7.0)), ("C2", (26.5, 7.0)),
                    ("C3", (27.75, 23.2)), ("C8", (35.9, 6.85))):
        b.ref_text(ref, at(*uv), 1.0, layer=BS)
    b.ref_text("C6", at(12.7, -13.25), 1.0, rot=b.fps["C6"].GetOrientationDegrees(), layer=BS)  # beside its body
    for ref in ("J1", "J2", "J5"):                                       # labelled by the texts below instead
        b.fps[ref].Reference().SetVisible(False)
    # the rotated transistors: KiCad 7 measures a footprint text's angle from the footprint, so add the
    # footprint's own rotation to read them horizontally
    for ref, uv in (("Q2", (-12.9, -3.0)), ("Q5", (36.0, 18.6)), ("Q6", (39.4, -1.5))):
        b.ref_text(ref, at(*uv), 1.0, rot=b.fps[ref].GetOrientationDegrees(), layer=BS)
        b.fps[ref].Reference().SetVisible(True)
    for ref in ("SW2", "SW3", "SW4", "SW6", "Q6", "R4", "R7", "R13", "C6", "Q2"):     # GND pads straight into the pour
        for pad in b.fps[ref].Pads():
            if pad.GetNetname() == "GND":
                pad.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
    b.text("LONG-RANGE REMOTE LITE CONTROL", *at(-21.0, -24.0), FS, 1.0)
    b.text("LONG-RANGE REMOTE LITE CONTROL", *at(-24.0, 4.5), BS, 1.0, mirror=True)
    b.text("J5 LED", *at(-8.15, -19.8), BS, 1.0, rot=90, mirror=True)     # connector labels on the back,
    b.text("J2 WAVESHARE", *at(27.2, -21.0), BS, 1.0, mirror=True)            # beside each connector
    b.text("pin 1 = 3V3", *at(27.2, -19.5), BS, 1.0, mirror=True)
    b.text("4xAA J1", *at(11.0, 22.7), BS, 1.0, mirror=True)                  # beside the plug
    b.fill_and_save()
    return b


CLASSES = {
    "POWER": (1.5, 0.3, ["/VBAT", "/LED_RTN"]),       # BATT's current leg (J1 -> Q5) is drawn by hand
    "GNDC": (1.0, 0.3, ["GND"]),
    "MID": (0.8, 0.3, ["/Q2_G"]),
    # signals (microamps; IR_DRV ~36 mA peaks): 0.3 mm fits between J2's pins (0.84 mm gap) with 0.25 mm
    # clearance, so the outer-row pins needn't go round the header's ends
    "SIG": (0.3, 0.25, ["/BTN_UP", "/BTN_DOWN", "/BTN_LEFT", "/BTN_RIGHT", "/BTN_SEL", "/BTN_BACK", "/KEY_TXD",
                        "/IR_DRV", "/ARM", "/VBAT_SENSE", "/BATT"]),
}


def finish_pcb():
    import pcbnew
    x0, y0, x1, y1 = CX - W / 2 + 0.5, CY - H / 2 + 0.5, CX + W / 2 - 0.5, CY + H / 2 - 0.5
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    kigen.finish(PCB, zones=[("GND", pcbnew.B_Cu, pts, dict(priority=0, name="GND_B")),
                             ("GND", pcbnew.F_Cu, pts, dict(priority=0, name="GND_F"))])
    print("removed dangling track stubs:", kigen.remove_dangling(PCB))
    print("right-angle track corners chamfered:", kigen.chamfer_corners(PCB))
    print("tiny track jogs absorbed:", kigen.absorb_short_legs(PCB))


def report_positions():
    import pcbnew
    brd = pcbnew.LoadBoard(str(PCB))
    for fp in sorted(brd.GetFootprints(), key=lambda f: f.GetReference()):
        bb = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
        pads = {p.GetNumber(): (round(pcbnew.ToMM(p.GetPosition().x) - CX, 2), round(pcbnew.ToMM(p.GetPosition().y) - CY, 2))
                for p in fp.Pads() if p.GetNumber()}
        cy = (round(pcbnew.ToMM(bb.GetLeft()) - CX, 1), round(pcbnew.ToMM(bb.GetRight()) - CX, 1),
              round(pcbnew.ToMM(bb.GetTop()) - CY, 1), round(pcbnew.ToMM(bb.GetBottom()) - CY, 1))
        print(fp.GetReference(), "B" if fp.IsFlipped() else "F", "crtyd u,v:", cy, dict(list(pads.items())[:3]))


def main():
    kigen.write_project_files(DIR, NAME)
    make_schematic()
    kigen.export_netlist(SCH, NET)
    check_netlist()
    make_pcb()
    if "--pos" in sys.argv:
        report_positions()
    ses = DIR / "build" / f"{NAME}.ses"
    if "--reuse" in sys.argv and ses.exists():
        import pcbnew
        brd = pcbnew.LoadBoard(str(PCB))
        print("re-applied", kigen.import_ses(brd, ses), "tracks/vias from", ses.name)
        pcbnew.SaveBoard(str(PCB), brd)
        finish_pcb()
    elif "--noroute" not in sys.argv:
        print(kigen.route(PCB, CLASSES, passes=100)[-400:])
        finish_pcb()
    r = kigen.drc(PCB)
    print("DRC:", {k: v for k, v in r.items() if k != "report"})


if __name__ == "__main__":
    main()
