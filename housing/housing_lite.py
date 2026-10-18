#!/usr/bin/env python3
"""
Long-range remote (4 x AA NiMH) - 3D-printable housing (CadQuery, parametric).

An LED head at the front, a body behind it whose lid carries the display and the control board. The
LEDs run from four AA NiMH cells in a bay in the floor, behind a door you open to swap the cells (they
are charged in any external NiMH charger). The display board and ESP32 run from the display board's own
18650, charged through its USB-C.

Side view (front = left), y up, z towards the back, x to the right as seen from behind:

       z 4.0 10.7  29.5                                                        174.5
        ┌──┬───────┬─────────────────────────────────────────────────────────┐ y = 14.6
        │B │ LED   │  LID (top deck): display (front) | D-pad, SELECT, BACK  │
        │E │ cavity│ [ display + its 18650 ]        [ control board ]        │
        │Z │       │  ┌──────────────────┐ ribbon over the bay               │
        │L │  J1 ──┼─►│ 4 x AA (bay)     │                                   │
        └──┴───────┴──┴══ door ══════════┴───────────────────────────────────┘ y = -37.4
          one cross-section: 107.4 wide x 52 tall, 170.5 long; the bezel is flush with the LED tips

  * SHELL    bezel and body are cut from ONE block with every edge rounded (top R1.5, long bottom R4,
             back corners R8, front corners + front bottom edge R3), so the edges run on unbroken
             across the join.
  * BODY     one part: the LED cavity at the front, then the tray. The LED head (99.8 x 44.8, R3
             corners, M3 holes at (+-44.2, +-14.0)) slides into the cavity from the front and is
             clamped by 4 x M3 x 25 countersunk screws: bezel -> 2 mm posts -> LED head -> pilot holes
             in four columns at the back of the cavity, fused into the side walls. J1 lies along the
             board's bottom edge, centred: the 10-way ribbon goes straight back through the opening in
             the cavity's rear wall at floor level and rises over the bay's roof.
             Under the display, a closed BATTERY BAY for a Keystone 2478 4 x AA holder
             (63 x 58 x 16 mm, open side down, taped to the roof), opened from underneath: the DOOR hooks
             in at the front with a tongue and is held at the back by one M3 x 10 countersunk screw into
             a captive nut. Display USB-C (to charge the screen's 18650) and TF card slots in the right
             wall.
  * LID      display window + snap-in sleeve, control board on four bosses (from housing_base.py),
             triangular D-pad holes, a round SELECT hole, a square BACK hole, a slot to reach the
             display's own buttons.
             4 x M3 x 10 countersunk.
  * CAPS     one triangular D-pad cap (printed 4x, turned to point outwards), a plain round SELECT and a
             square BACK, each with a thick round base under the lid: no stem, no engraving, prints
             base-down.
  * BEZEL    front plate (the same cross-section), 6.7 mm thick and flush with the LED tips, with four
             posts that press on the LED head and a relief over J1's pin tails.

Every part fits a Prusa MINI (180 x 180 x 180). Only the battery bay's roof needs support (an enforcer
STL is exported for it); everything else prints without, in its STL orientation.

Run:   python3 housing_lite.py   -> step_lite/, stl_lite/, interference + size + print report
Needs: cadquery, shapely, housing_base.py (shared frame, helpers, lid, key layout),
       ref/board_envelopes.json (tools/extract_envelopes.py reads the KiCad boards).
Dimension sources: [WS] Waveshare drawings, [STEP] Waveshare's 3D model of the display board (see
housing_base.py and tools/waveshare_fit.py), [PCB] the KiCad boards, [KS] Keystone 2478 (63 x 58 x 16 mm,
as listed by Rapid Electronics), [EST] estimated.
"""
import json
from pathlib import Path

import os

import cadquery as cq

# The space behind the LED board for J1, the mated IDC socket and the ribbon's bend: header body 2.5 +
# socket 9 = 11.5 mm mated (the socket slides over the header's pins); 14.0 mm leaves 2.5 mm over the
# socket for the ribbon to turn, + 1.2 mm to the wall. housing_base.py reads it from the environment.
J1_STACK = 14.0
os.environ.setdefault("LRR_HEAD_BACK_SPACE", str(J1_STACK + 1.2))
os.environ.setdefault("LRR_HEAD_REAR_T", "0.0")      # head and tray are ONE part: the wall behind the LED cavity is the tray's front wall
import housing_base as H                  # shared frame, helpers, the lid and the key layout
from housing_base import box, rbox_y, rbox_z, zcyl, ycyl, interference, volume

HERE = Path(__file__).resolve().parent

# ============================================================================ parameters
WALL = H.WALL
# --- body: width and length set by the display and the control board (housing_base.py's frame)
BHI, BHO = H.HI, H.HO                    # 50.7, 53.7
B_R_OUT, B_R_IN = H.R_OUT, H.R_IN        # plan-view corners at the back of the body
Y_BOT = -37.4                            # bottom face
BODY_H = 52.0                            # set by the battery bay + the display's 18650 above it (see check)
Y_TOP = Y_BOT + BODY_H                   # 14.6
LID_T = H.LID_T
Y_LID = Y_TOP - LID_T                    # 11.6
Y_FLOOR = Y_BOT + WALL                   # -34.4
# Outer shell: ONE rounded block (bezel + head + tray are cut from it, so every edge runs on unbroken
# from part to part). Radii per edge of the 107.4 x 52 x 183.5 block:
FRONT_R = 3.0                            # front vertical corners and the front bottom edge (bezel prints
#                                          front-down: small radii there keep the overhang short)
REAR_R = 8.0                             # rear vertical corners (plan view)
BOT_R = 4.0                              # long bottom edges and the rear bottom edge
TOP_R = 1.5                              # every top edge (round the deck rim)
DY = Y_TOP - H.Y_TOP                     # the base frame's lid, display and control board, moved down to this height
Z_BODY1, Z_TRAY_IN0, Z_TRAY_IN1 = H.Z_BODY1, H.Z_TRAY_IN0, H.Z_TRAY_IN1   # 183.5, 41.5, 180.5
LID_COLUMN_R = H.LID_COLUMN_R
# lid screws: placed so each countersink (R3.2) sits fully inside the lid's R6.75 corner, with at least
# 0.8 mm of lid round it.
# Front pair: as far forward as the display sleeve allows; rear pair: clear of the control board.
LID_SCREWS = [(-43.7, H.Z_HEAD1 + 7.0), (43.7, H.Z_HEAD1 + 7.0), (-45.6, H.Z_HEAD1 + 136.6), (45.6, H.Z_HEAD1 + 136.6)]   # z measured from the head's rear face (45.5 / 175.5 at 38.5)

# --- head: the same cross-section as the body (107.4 x 52), so head and body read as one block.
# Lite LED head [PCB]: 99.8 x 44.8, R3 corners, M3 holes at (+-44.2, +-14.0), J1 along its bottom edge.
PCB_W, PCB_H, PCB_R = 99.8, 44.8, 3.0
HX, HY = BHI, BODY_H / 2 - WALL          # cavity half-sizes 50.7 x 23.0 (board + 0.9 / 0.6 per side)
YC = (Y_BOT + Y_TOP) / 2                 # -11.4: head (and LED board) centre height
CAV_R = 2.0                              # cavity's bottom corners R2 (the board's R3 corners clear them)
HOLE_U, HOLE_V = 44.2, 14.0
COLUMN_R = 3.4
POST_D = H.POST_D
# Bezel window: the LED field is a cross shape (the grid's corner positions hold the M3 holes), so the
# window is too - rows 2-4 full width, the end columns cut back - with every corner rounded R2:
#   middle band  u +-47.6 (collars reach 47.1), v +-10.3 (rows +-7 + 2.9 collar + 0.4)
#   full height  u +-40.6 (collars of columns 2..13 reach 40.3), v +-17.4 (collars reach 16.9)
# A post (and its countersink, R3.2) sits 3.6 / 3.7 mm from the nearest window edge.
WIN_WIDE = (47.6, 10.3)
WIN_TALL = (40.6, 17.4)
WIN_R = 2.0
# z stack at the front (Z_LEDPCB 12.7, Z_LEDBACK 14.3, Z_HEAD1 29.5 = the cavity's rear wall). The bezel's front face is flush with the LED tips, so the
# remote can lie face-down on the bezel without resting on the LEDs; its back face stays 2 mm in front of
# the board (short posts), and a 4 mm deep relief over J1 leaves its pin tails 6 mm - no trimming.
Z_LEDPCB, Z_LEDBACK = H.Z_LEDPCB, H.Z_LEDBACK                         # 12.7, 14.3
POST_GAP = 2.0                           # bezel back face to the LED board
Z_HEAD0 = Z_LEDPCB - POST_GAP            # 10.7: bezel back face = head front face
ZF = Z_LEDPCB - H.LED_H                  # 4.0: the front face of the remote = the LED tips
BEZEL_THICK = Z_HEAD0 - ZF               # 6.7
LED_PROUD = ZF - (Z_LEDPCB - H.LED_H)    # 0: flush
J1_RELIEF = 4.0                          # pocket in the bezel's back over J1's pin tails
HEAD_SCREW_L = 25.0                      # M3 x 25 countersunk: bezel -> LED head -> 15 mm into the columns
Z_HEAD_RW, Z_HEAD1 = H.Z_HEAD_RW, H.Z_HEAD1                            # 35.5, 38.5 (same J1 + ribbon space)
BEZEL_T, PCB_T = H.BEZEL_T, H.PCB_T

# ribbon opening through the head's rear wall and the body's front wall, under the head's J1 (bottom
# centre, seen from behind); the ribbon then rises over the battery bay's roof
TRAY_OPEN = (-9.5, 9.5, Y_FLOOR, -23.0)  # x0, x1, y0, y1

# --- battery bay: Keystone 2478, 4 x AA, 63 (x) x 58 (z) x 16 (y) mm [KS], open side down
HOLD_X, HOLD_Z, HOLD_H = 63.0, 58.0, 16.0
BAY_CLEAR = 0.5
BAY_X0, BAY_X1 = -HOLD_X / 2 - BAY_CLEAR, HOLD_X / 2 + BAY_CLEAR
BAY_Z0 = H.Z_DISP_C - HOLD_Z / 2 - BAY_CLEAR      # under the display
BAY_Z1 = BAY_Z0 + HOLD_Z + 2 * BAY_CLEAR
BAY_WALL, ROOF_T = 1.6, 2.0
DOOR_T = 3.0
Y_DOOR_IN = Y_BOT + DOOR_T               # door's inner face: the cells rest just above it
TAPE_T = 1.0                             # double-sided foam tape holding the holder to the roof
Y_ROOF0 = Y_DOOR_IN + HOLD_H + TAPE_T + 0.5   # roof underside: holder + tape + 0.5 mm over the door
Y_ROOF1 = Y_ROOF0 + ROOF_T
DOOR_LIP = 3.0                           # recess margin round the opening (the door's seat)
SCREW_Z = BAY_Z1 + BAY_WALL + 4.5        # door screw, just behind the bay
DOOR_Z0, DOOR_Z1 = BAY_Z0 - DOOR_LIP, SCREW_Z + 4.5
DOOR_X0, DOOR_X1 = BAY_X0 - DOOR_LIP, BAY_X1 + DOOR_LIP
DOOR_GAP = 0.25                          # door to recess, each side
TONGUE_W, TONGUE_L, TONGUE_T = 30.0, 3.0, 1.2      # tongue on the door's inner face
SLOT_Y0 = 1.2                            # lip under the tongue's slot (from the bottom face)
NUT_AF, NUT_T = 5.5, 2.4                 # M3 nut
WIRE_HOLE = (24.0, BAY_Z1 - 6.0, 3.5)    # x, z, radius: the holder's leads up to control J1

KEYS = H.KEYS                            # names and positions (the switch centres on the control board)

# --- key caps. D-pad: ONE triangular cap printed four times, turned to point outwards - the triangular
# lid holes stop it rotating, and the shape says the direction, so nothing is engraved. SELECT: a plain
# round cap. BACK: a rounded square. Every cap has a thick round base under the lid that keeps it in and
# presses the switch, so it prints base-down with no supports and needs no stem.
import math
DPAD_SIDE = 10.0                         # triangle side (centroid on the switch); inradius 2.9, apex 5.8
DPAD_R = 1.0                             # rounded triangle corners
DPAD_ANGLE = {"UP": 0, "DOWN": 180, "LEFT": 90, "RIGHT": -90}   # cap frame: apex = +y = towards the head
DPAD_BASE_R = 6.6                        # neighbouring D-pad bases are 14.1 mm apart: 0.9 mm between them
SELECT_D, SELECT_BASE_R = 10.0, 7.0
BACK_SIDE, BACK_R = 7.5, 1.5             # BACK: a rounded square (the square hole stops it turning), so it
BACK_BASE_R = DPAD_BASE_R                # can't be mistaken for SELECT or a D-pad key by feel; 14.6 mm from
                                         # DOWN and RIGHT, so 1.4 mm between the bases
KEY_GAP = H.KEY_HOLE_CLEAR / 2           # 0.3 mm round each cap in its lid hole
CAP_TOP_H = H.LID_T + H.CAP_PROTRUDE     # 5.5: through the 3 mm lid, 2.5 mm proud
SWITCH_H, SWITCH_GAP = 4.3, 0.2          # B3F-1000 plunger top above the board; free play above it
CAP_BASE_T = H.BTN_GAP - SWITCH_H - SWITCH_GAP   # 4.2: lid underside down to 0.2 mm above the plunger
CAP_TOP_EDGE = 0.8                       # rounded top edge

# --- access to the display board's buttons: KEY (GPIO18), PWR (the power latch), BOOT (GPIO0), at
# xf -10 / 0 / +10, pushed sideways [STEP + Waveshare schematic]. They sit dead centre on the display's top
# (front) edge and are pressed sideways, from the LED-head side [user]. One slot goes down through the lid
# in the gap in front of the display, and through the sleeve's front wall over the display's thickness,
# so a fingertip or a thin tool can go down beside the board's edge and push rearwards onto a button.
# The buttons are at xf -12..+12 [STEP], 3.4-5.6 mm below the glass (6.4-8.6 mm below the lid's top), with
# their plungers flush with the board edge. 38 mm is as wide as the slot can be without cutting the front
# snap hooks' slits (x +-20.5..21). It runs forward right out through the lid's front edge [user]: a notch
# open to the tray's front wall, 11 mm front to back, instead of an 8 mm slot with a 3 mm strip in front.
BTNS_SLOT_W = 38.0                      # along x, centred
# along z, relative to the display's front edge (negative = towards the head). -10.0 is just past the lid's
# front edge (-9.95); the tray's front wall is at -10.2, so the opening's keep-out box still clears it.
BTNS_SLOT_Z0, BTNS_SLOT_Z1 = -10.0, 1.0


# ============================================================================ parts
def corners():
    """LED head holes in housing x, y."""
    return [(sx * HOLE_U, YC + sy * HOLE_V) for sx in (-1, 1) for sy in (-1, 1)]


_SHELL = None


def shell():
    """The whole outer block, every edge rounded (radii per edge, see FRONT_R .. TOP_R). Built once."""
    global _SHELL
    if _SHELL is None:
        from OCP.BRepFilletAPI import BRepFilletAPI_MakeFillet
        blk = box(-BHO, BHO, Y_BOT, Y_TOP, ZF, Z_BODY1).val()
        mf = BRepFilletAPI_MakeFillet(blk.wrapped)
        for e in blk.Edges():
            c, bb = e.Center(), e.BoundingBox()
            if bb.ylen > 1:                                    # vertical corners
                r = FRONT_R if c.z < ZF + 1 else REAR_R
            elif c.y > 0:                                      # top edges
                r = TOP_R
            elif bb.zlen > 1:                                  # long bottom edges
                r = BOT_R
            else:                                              # front / rear bottom edge
                r = FRONT_R if c.z < ZF + 1 else BOT_R
            mf.Add(r, e.wrapped)
        mf.Build()
        _SHELL = cq.Shape.cast(mf.Shape())
    return cq.Workplane("XY").newObject([_SHELL])


def slab(z0, z1):
    """The part of the outer block between z0 and z1."""
    return shell().intersect(box(-BHO - 1, BHO + 1, Y_BOT - 1, Y_TOP + 1, z0, z1))


def pick_edges(wp, face_sel, keep):
    """Edges of the face(s) `face_sel` for which keep(edge) is true, ready to fillet / chamfer."""
    es = [e for e in wp.faces(face_sel).edges().vals() if keep(e)]
    return wp.newObject(es)


def rslot_x(x0, x1, y0, y1, z0, z1, r):
    """Box through a side wall (along x) with its four edges along x rounded: a rounded-rectangle hole."""
    return box(x0, x1, y0, y1, z0, z1).edges("|X").fillet(r)


def section(x0, x1, y0, y1, z0, z1, r_bot, r_top=0.0):
    """Block along z with the head/body cross-section: bottom long edges rounded r_bot, top r_top."""
    b = box(x0, x1, y0, y1, z0, z1)
    if r_bot > 0:
        b = b.edges("|Z").edges("<Y").fillet(r_bot)
    if r_top > 0:
        b = b.edges("|Z").edges(">Y").fillet(r_top)
    return b


def face_sliver_cut(wide, tall, csk_r, depth, min_w):
    """Solid to cut from the bezel's front face: the bits of its outline narrower than min_w between the
    window and the countersinks (2D opening, shapely), within 6 mm of each hole, ZF .. ZF + depth."""
    from shapely.geometry import box as sbox, Point
    from shapely.ops import unary_union
    (wx, wy), (tx, ty) = wide, tall
    rnd = lambda g, r: g.buffer(-r, join_style=1).buffer(r, join_style=1)
    win = unary_union([rnd(sbox(-wx, YC - wy, wx, YC + wy), WIN_R), rnd(sbox(-tx, YC - ty, tx, YC + ty), WIN_R)])
    win = win.buffer(WIN_R, join_style=1).buffer(-WIN_R, join_style=1)         # R2 in the concave corners too
    holes = [Point(x, y).buffer(csk_r, 128) for x, y in corners()]
    mat = sbox(-BHO, YC - BODY_H, BHO, YC + BODY_H).difference(win).difference(unary_union(holes))
    keep = mat.buffer(-min_w / 2, join_style=1).buffer(min_w / 2, join_style=1)
    # keep only the main frame: small separate crescents left between a hole and the window would be
    # loose specks on the face
    keep = max(getattr(keep, "geoms", [keep]), key=lambda g: g.area)
    near = unary_union([Point(x, y).buffer(6.0, 64) for x, y in corners()])
    # the countersinks themselves go in the cut too, as a short straight counterbore at the face diameter:
    # the cone narrows with depth, so a straight cut beside it would leave a thin ring below the face
    drop = mat.difference(keep).intersection(near).union(unary_union(holes))
    # grow 0.1 mm so the cut overlaps the true circles / arcs (the 2D outline is a polygon approximation:
    # cut exactly to it, it would leave microscopic lens slivers along every curved edge)
    drop = drop.buffer(0.1, join_style=1).simplify(0.02)    # fewer facets: keeps the STEP small
    cut = None
    for g in getattr(drop, "geoms", [drop]):
        if g.is_empty or g.area < 1e-4:
            continue
        sol = cq.Workplane("XY").workplane(offset=ZF - 1).polyline(list(g.exterior.coords)[:-1]).close() \
            .extrude(depth + 1)
        for h in g.interiors:                                           # (none expected)
            sol = sol.cut(cq.Workplane("XY").workplane(offset=ZF - 2).polyline(list(h.coords)[:-1]).close()
                          .extrude(depth + 3))
        cut = sol if cut is None else cut.union(sol)
    return cut if cut is not None else box(0, 0.01, 0, 0.01, ZF - 3, ZF - 2)


def make_bezel():
    b = slab(ZF, Z_HEAD0)
    (wx, wy), (tx, ty) = WIN_WIDE, WIN_TALL
    win = box(-wx, wx, YC - wy, YC + wy, ZF - 1, Z_HEAD0 + 1).union(box(-tx, tx, YC - ty, YC + ty, ZF - 1, Z_HEAD0 + 1))
    b = b.cut(win.edges("|Z").fillet(WIN_R))
    for x, y in corners():
        b = b.union(zcyl(x, y, POST_D / 2, Z_HEAD0 - 0.5, Z_LEDPCB))          # short posts onto the board
        b = b.cut(zcyl(x, y, H.CLEAR_M3 / 2, ZF - 1, Z_LEDPCB + 1))
        b = b.cut(cq.Workplane("XY").workplane(offset=ZF).center(x, y).circle(3.2)
                  .workplane(offset=1.7).circle(H.CLEAR_M3 / 2).loft())
    # The M3 countersinks (6.4 at the face) come within ~0.4 mm of the window's cut-back corners, which
    # would print as feather-thin slivers on the first layers (the bezel prints face-down). On the front
    # face's outline (bezel minus window minus countersinks), a morphological opening (shrink, then grow,
    # by 0.4 mm) keeps only material at least 0.8 mm wide - two perimeters; whatever it drops near a
    # hole is cut away for the first 0.45 mm, below which the countersink cone has narrowed enough.
    b = b.cut(face_sliver_cut((wx, wy), (tx, ty), 3.2, depth=0.45, min_w=0.8))
    # relief over J1's soldered pin stubs on the LED side of the board (bottom centre)
    # (runs on into the window, which is open anyway: stopping 0.8 mm past its edge left slivers at the corners)
    b = b.cut(rbox_z(-7.5, 7.5, YC - 21.9, YC - 14.0, Z_HEAD0 - J1_RELIEF, Z_HEAD0 + 0.1, 1.0))
    return b


def opening(x0, x1, y0, y1, z0, z1, r=2.0):
    return rbox_z(x0, x1, y0, y1, z0, z1, r)


def door_outline(grow=0.0, below=0.0):
    """The door plate (grow > 0: the recess it sits in, reaching `below` mm under the bottom face)."""
    return rbox_y(DOOR_X0 - grow, DOOR_X1 + grow, Y_BOT - below, Y_BOT + DOOR_T,
                  DOOR_Z0 - grow, DOOR_Z1 + grow, 3.0 + grow)


def make_body():
    """Head and tray in one part: the LED cavity at the front (open to the bezel), then the tray behind it,
    separated by the tray's 3 mm front wall (the ribbon passes through it)."""
    t = slab(Z_HEAD0, Z_BODY1)
    t = t.cut(section(-HX, HX, YC - HY, YC + HY, Z_HEAD0 - 1, Z_HEAD_RW, CAV_R))
    for x, y in corners():                                     # columns behind the LED head's holes
        sx, sy = (1 if x > 0 else -1), (1 if y > YC else -1)
        ex, ey = sx * (HX + 0.5), YC + sy * (HY + 0.5)         # fused into the side wall and top / floor
        z0, z1 = Z_LEDBACK, Z_HEAD_RW + 0.5
        col = zcyl(x, y, COLUMN_R, z0, z1)
        col = col.union(box(min(x, ex), max(x, ex), y - COLUMN_R, y + COLUMN_R, z0, z1))
        col = col.union(box(x - COLUMN_R, x + COLUMN_R, min(y, ey), max(y, ey), z0, z1))
        t = t.union(col)
        t = t.cut(zcyl(x, y, H.PILOT_M3 / 2, Z_LEDBACK - 1, ZF + HEAD_SCREW_L + 0.5))   # ends inside the column
    # inside: same outline as the lid's (R7 corners, 0.25 mm round the R6.75 lid)
    inner = box(-BHI, BHI, Y_FLOOR, Y_TOP + 1, Z_TRAY_IN0, Z_TRAY_IN1).edges("|Y").fillet(B_R_IN)
    t = t.cut(inner)
    x0, x1, y0, y1 = TRAY_OPEN                                 # ribbon opening, front wall
    t = t.cut(opening(x0, x1, y0 - 3, y1, Z_HEAD_RW - 1, Z_TRAY_IN0 + 1).cut(
        box(x0 - 1, x1 + 1, y0 - 4, y0, Z_HEAD_RW - 2, Z_TRAY_IN0 + 2)))
    for (x, z) in LID_SCREWS:                                  # lid columns, fused to the side walls
        sx = 1 if x > 0 else -1
        col = ycyl(x, z, LID_COLUMN_R, Y_BOT + 1, Y_LID)
        col = col.union(box(min(x, sx * (BHI + 0.5)), max(x, sx * (BHI + 0.5)), Y_BOT + 1, Y_LID,
                            z - LID_COLUMN_R, z + LID_COLUMN_R))
        t = t.union(col)
        t = t.cut(ycyl(x, z, H.PILOT_M3 / 2, Y_LID - 14, Y_LID + 1))
    # display USB-C (charges the screen's 18650) and TF card slots (right wall)
    for yf, depth, along, tall in ((H.USB_YF, H.USB_DEPTH, H.USB_ALONG, H.USB_TALL),
                                   (H.TF_YF, H.TF_DEPTH, H.TF_ALONG, H.TF_TALL)):
        _, z = H.disp_xz(0, yf)
        y = Y_LID - depth
        t = t.cut(rslot_x(BHI - 1, BHO + 1, y - tall / 2, y + tall / 2, z - along / 2, z + along / 2,
                          min(1.5, tall / 2 - 0.05)))

    # --- battery bay: walls + roof, rising from the floor
    w = BAY_WALL
    bay = box(BAY_X0 - w, BAY_X1 + w, Y_BOT, Y_ROOF1, BAY_Z0 - w, BAY_Z1 + w).edges("|Y").fillet(2.0)
    t = t.union(bay)
    # floor thickened round the door (its recess is as deep as the floor), and the screw boss behind it
    t = t.union(box(DOOR_X0 - DOOR_GAP - 2.5, DOOR_X1 + DOOR_GAP + 2.5, Y_BOT + 1, Y_BOT + DOOR_T + 2.0,
                    DOOR_Z0 - TONGUE_L - 3.0, DOOR_Z1 + DOOR_GAP + 2.5).edges("|Y").fillet(3.0))
    t = t.union(ycyl(0, SCREW_Z, 4.6, Y_BOT, Y_BOT + 10.0))
    t = t.union(box(-4.6, 4.6, Y_BOT, Y_BOT + 10.0, BAY_Z1, SCREW_Z))
    # inside of the bay, open at the bottom
    t = t.cut(box(BAY_X0, BAY_X1, Y_BOT - 1, Y_ROOF0, BAY_Z0, BAY_Z1))
    # the door's recess, flush with the bottom face, and the slot its tongue slides into
    t = t.cut(door_outline(DOOR_GAP, below=1.0))
    # (taller than the tongue so the door can go in tilted a few degrees, then swing up flat)
    t = t.cut(box(-TONGUE_W / 2 - 0.3, TONGUE_W / 2 + 0.3, Y_BOT + SLOT_Y0, Y_BOT + DOOR_T + 0.5,
                  DOOR_Z0 - TONGUE_L - 0.4, DOOR_Z0 + 0.1))
    # door screw: M3 clearance up the boss, hex pocket for the nut opening into the tray (drop it in
    # from above before the lid goes on)
    t = t.cut(ycyl(0, SCREW_Z, H.CLEAR_M3 / 2, Y_BOT - 1, Y_BOT + 11))
    hexr = NUT_AF / 2 / 0.8660 + 0.15
    t = t.cut(cq.Workplane("XZ").workplane(offset=-(Y_BOT + 11.0)).center(0, SCREW_Z)
              .polygon(6, 2 * hexr).extrude(11.0 - (DOOR_T + 2.6)))     # nut seat 2.6 mm above the door
    # finger notch behind the door's back edge, to lift the door out once the screw is undone
    # (beside the screw, not behind it, so the two don't run into each other)
    t = t.cut(ycyl(18.0, DOOR_Z1 + 0.5, 3.5, Y_BOT - 1, Y_BOT + 2.0).edges().fillet(0.6))
    # the holder's leads up through the roof
    wx, wz, wr = WIRE_HOLE
    t = t.cut(ycyl(wx, wz, wr, Y_ROOF0 - 1, Y_ROOF1 + 1))
    return t


def make_door():
    d = door_outline(0.0)
    # tongue at the front edge, on the door's inner half, slides into the slot in front of the recess
    d = d.union(box(-TONGUE_W / 2, TONGUE_W / 2, Y_BOT + DOOR_T - TONGUE_T, Y_BOT + DOOR_T,
                    DOOR_Z0 - TONGUE_L, DOOR_Z0 + 0.5))
    # countersunk M3 hole at the back (head flush with the outside)
    d = d.cut(ycyl(0, SCREW_Z, H.CLEAR_M3 / 2, Y_BOT - 1, Y_BOT + DOOR_T + 1))
    d = d.cut(cq.Workplane("XZ").workplane(offset=-Y_BOT).center(0, SCREW_Z).circle(3.2)
              .workplane(offset=-1.7).circle(H.CLEAR_M3 / 2).loft())
    # soften the outside edge (it prints on top): 0.6 mm chamfer round the outline, not the countersink
    d = pick_edges(d, "<Y", lambda e: e.Center().z < SCREW_Z - 4 or abs(e.Center().x) > 5).chamfer(0.6)
    return d


def tri_prism(grow, z0, z1, r):
    """Equilateral triangle (side DPAD_SIDE, grown by `grow` all round), centroid at the origin, apex on
    +y, corners rounded r, extruded z0..z1 (cap frame: z up through the lid)."""
    h = DPAD_SIDE * math.sqrt(3) / 2
    k = (h / 3 + grow) / (h / 3)                       # scaling about the centroid = parallel offset
    pts = [(0, 2 * h / 3 * k), (-DPAD_SIDE / 2 * k, -h / 3 * k), (DPAD_SIDE / 2 * k, -h / 3 * k)]
    return (cq.Workplane("XY").workplane(offset=z0).polyline(pts).close().extrude(z1 - z0)
            .edges("|Z").fillet(r))


def make_dpad_cap():
    """The D-pad cap (one model for all four): triangle through the lid, round base under it."""
    body = tri_prism(0.0, -0.5, CAP_TOP_H, DPAD_R).faces(">Z").edges().fillet(CAP_TOP_EDGE)
    return body.union(zcyl(0, 0, DPAD_BASE_R, -CAP_BASE_T, 0))


def make_select_cap():
    body = zcyl(0, 0, SELECT_D / 2, -0.5, CAP_TOP_H).faces(">Z").edges().fillet(CAP_TOP_EDGE)
    return body.union(zcyl(0, 0, SELECT_BASE_R, -CAP_BASE_T, 0))


def make_back_cap():
    body = rrect(BACK_SIDE, BACK_SIDE, -0.5, CAP_TOP_H, BACK_R).faces(">Z").edges().fillet(CAP_TOP_EDGE)
    return body.union(zcyl(0, 0, BACK_BASE_R, -CAP_BASE_T, 0))


def key_outline(name, grow, z0, z1):
    """A key's outline through the lid (cap frame), grown by `grow` all round: hole cutters, travel paths."""
    if name == "SELECT":
        return zcyl(0, 0, SELECT_D / 2 + grow, z0, z1)
    if name == "BACK":
        return rrect(BACK_SIDE + 2 * grow, BACK_SIDE + 2 * grow, z0, z1, BACK_R + grow)
    return tri_prism(grow, z0, z1, DPAD_R + grow)


def place_key(shape, name):
    """A cap (or hole cutter) from its own frame into the housing at key `name`, D-pad ones turned."""
    if name in DPAD_ANGLE:
        shape = shape.rotate((0, 0, 0), (0, 0, 1), DPAD_ANGLE[name])
    return H.place_cap(shape, name).translate((0, DY, 0))


def make_caps():
    """{name: placed cap} for all six keys."""
    models = {"SELECT": make_select_cap(), "BACK": make_back_cap()}
    dpad = make_dpad_cap()
    return {name: place_key(models.get(name, dpad), name) for name, *_ in KEYS}


def rrect(w, l, z0, z1, r):
    """Rounded rectangle w (x) by l (y), centred, extruded z0..z1 (cap / knob frame)."""
    return box(-w / 2, w / 2, -l / 2, l / 2, z0, z1).edges("|Z").fillet(r)


def btns_access_box(y0):
    """The opening to the display board's buttons, from y0 (Lite frame: add DY) up through the lid's top face."""
    ze = H.Z_DISP_C - H.DISP_H / 2                       # display front edge (image 'up' = forward = -z)
    return box(-BTNS_SLOT_W / 2, BTNS_SLOT_W / 2, y0 + DY, Y_TOP + 1.0, ze + BTNS_SLOT_Z0, ze + BTNS_SLOT_Z1)


def make_lid():
    """The base lid (display sleeve, control board bosses, lid screws) moved to this housing's lid height,
    with triangular D-pad holes, the round SELECT hole, the square BACK hole and the display-button slot."""
    lid = H.make_lid(lid_screws=LID_SCREWS, key_holes=False).translate((0, DY, 0))
    for name, *_ in KEYS:
        lid = lid.cut(place_key(key_outline(name, KEY_GAP, -1, H.LID_T + 1), name))
    # display buttons: slot through the lid and the sleeve's whole front wall. It must go right down past the
    # wall's bottom edge (1.2 mm below the display's back plate, where the hooks catch): stopping at the back
    # plate left a loose 38 x 1.6 x 1.2 mm strip there, cut free by the hook slits either side.
    lid = lid.cut(btns_access_box(H.Y_DISP_BACK - 2.5))
    # 0.3 mm chamfer round the top outline (a neat shadow line against the tray's R1.5 rim) ...
    x1, z0, z1 = BHI - H.LID_CLEAR, Z_TRAY_IN0 + H.LID_CLEAR, Z_TRAY_IN1 - H.LID_CLEAR
    outer = lambda e: (abs(abs(e.Center().x) - x1) < 0.05 or abs(e.Center().z - z0) < 0.05
                       or abs(e.Center().z - z1) < 0.05
                       or (e.geomType() == "CIRCLE" and e.radius() > 6.0))      # the R6.75 corners
    lid = pick_edges(lid, ">Y", outer).chamfer(0.3)   # 0.3: the countersinks are 0.5 mm in from the edge
    # ... and 0.4 mm round each key hole
    centres = [H.key_xz(u, v) for _, u, v, _, _ in KEYS]
    near_key = lambda e: any((e.Center().x - x) ** 2 + (e.Center().z - z) ** 2 < 8.0 ** 2 for x, z in centres)
    lid = pick_edges(lid, ">Y", near_key).chamfer(0.4)
    return lid


# ============================================================================ reference geometry / checks
def head_xy(u, v):
    """Lite LED head board (u right, v down, as in KiCad, front view) -> housing x, y. Seen from behind,
    x = -u; its J1 (v = +19.3) ends up at the bottom."""
    return -u, YC - v


def reference_solids():
    ref = {}
    ref["led_head_pcb"] = rbox_z(-PCB_W / 2, PCB_W / 2, YC - PCB_H / 2, YC + PCB_H / 2, Z_LEDPCB,
                                 Z_LEDPCB + PCB_T, PCB_R)
    for name, s in H.reference_solids().items():
        if name.startswith("display") or name == "control_pcb":     # (incl. the cell and the ribbon socket)
            ref[name] = s.translate((0, DY, 0))
    zc = (BAY_Z0 + BAY_Z1) / 2
    ref["aa_holder"] = box(-HOLD_X / 2, HOLD_X / 2, Y_ROOF0 - TAPE_T - HOLD_H, Y_ROOF0 - TAPE_T,
                           zc - HOLD_Z / 2, zc + HOLD_Z / 2)
    return ref


def access_solids():
    a = {}
    for name, s in H.access_solids().items():
        if name == "usb_c_plug":
            a[name] = s.translate((0, DY, 0))
    # each key's travel: the cap's own outline (triangle / circle / square) from the lid underside to 3 mm above it
    for name, *_ in KEYS:
        a[f"key_path_{name}"] = place_key(key_outline(name, 0.0, -1, H.LID_T + 3), name)
    # a thin tool reaching the display's side buttons: down the slot, then rearwards onto the button
    a["display_buttons_tool"] = btns_access_box(H.Y_DISP_BACK + 2.0)
    # cells out through the opening once the door is off
    a["aa_cells_out"] = box(BAY_X0 + 0.3, BAY_X1 - 0.3, Y_BOT - 30, Y_DOOR_IN, BAY_Z0 + 0.3, BAY_Z1 - 0.3)
    # 10-way ribbon (12.7 mm wide): from the LED head's J1 socket (bottom centre) straight back at floor
    # level, up in front of the battery bay, then over the bay's roof on the LEFT half (towards control J5 at
    # x = -19.6), under the display's 18650 holder.
    a["ribbon_path"] = box(-7.0, 7.0, Y_FLOOR + 0.3, YC - 15.0, Z_LEDBACK + J1_STACK, Z_TRAY_IN0 + 3.0).union(
        box(-7.0, 7.0, Y_ROOF1 + 0.1, Y_ROOF1 + 1.5, BAY_Z0, BAY_Z0 + 10.0)).union(
        box(-25.0, -11.5, Y_ROOF1 + 0.1, Y_ROOF1 + 1.5, BAY_Z0 + 10.0, BAY_Z1))
    # 16-way ribbon (20.3 mm wide), both ends in the lid: out of the display header's socket stack towards
    # the back, down under the 18650 holder onto the RIGHT half of the bay roof (beside the 10-way), then up
    # to control J2 (x = 7.1). Made ~90 mm long between the sockets (see the README: the 12" cable is cut).
    z_sock = H.Z_DISP_C + 4.6                                   # back edge of the ribbon socket on the header [STEP]
    z_cell = H.Z_DISP_C + 29.45 + 0.5                           # just behind the 18650 cradle [STEP]
    y_sock = H.Y_GLASS + DY - H.HDR_STACK                       # the ribbon leaves the socket's far face here
    a["ribbon16_path"] = box(-11.1, 9.2, Y_ROOF1 + 0.1, y_sock, z_sock, z_sock + 2.4).union(  # down to the roof
        box(-3.5, 17.5, Y_ROOF1 + 0.1, Y_ROOF1 + 1.5, z_sock, z_cell)).union(                 # under the 18650
        box(-3.5, 17.5, Y_ROOF1 + 0.1, H.Y_BTN + DY - PCB_T - 13.0, z_cell, z_cell + 6.0))  # up to J2's socket
    # M3 x 10 door screw and the nut
    a["door_screw"] = ycyl(0, SCREW_Z, 1.5, Y_BOT + DOOR_T, Y_BOT + 10.0)      # M3 x 10 countersunk
    return a


LED_BODY_R, LED_COLLAR_R, LED_COLLAR_H = 2.5, 2.9, 1.0     # SFH 4545: 5 mm body, 5.8 mm collar [LED datasheet, as in the envelopes]


def led_solid(cx, cy):
    """One 5 mm SFH 4545 for the STEP assembly: collar on the board, round body, domed tip. The tip is at
    z = ZF (4.0, flush with the bezel); the board's front face is Z_LEDPCB. Axis along the housing's z."""
    z_tip = Z_LEDPCB - H.LED_H
    z_dome = z_tip + LED_BODY_R                                 # dome centre: a hemisphere of the body's radius
    body = zcyl(cx, cy, LED_BODY_R, z_dome, Z_LEDPCB - LED_COLLAR_H + 0.1)
    collar = zcyl(cx, cy, LED_COLLAR_R, Z_LEDPCB - LED_COLLAR_H, Z_LEDPCB)
    dome = cq.Workplane("XY").sphere(LED_BODY_R).translate((cx, cy, z_dome)).intersect(
        box(cx - 3, cx + 3, cy - 3, cy + 3, z_tip - 0.1, z_dome))
    return body.union(collar).union(dome)


def led_reference_solids():
    """{ 'D12': solid, ... } the 66 LEDs, at the positions in board_envelopes.json (from the KiCad board)."""
    data = json.loads((HERE / "ref" / "board_envelopes.json").read_text())
    leds = {}
    for it in data.get("led_head_lite", []):
        if not (it["fp"].startswith("LED_THT") and it["side"] == "front"):
            continue
        (u0, u1), (v0, v1) = it["u"], it["v"]
        pts = [head_xy(u, v) for u in (u0, u1) for v in (v0, v1)]
        cx = (min(q[0] for q in pts) + max(q[0] for q in pts)) / 2
        cy = (min(q[1] for q in pts) + max(q[1] for q in pts)) / 2
        leds[it["ref"]] = led_solid(cx, cy)
    return leds


def board_envelopes():
    data = json.loads((HERE / "ref" / "board_envelopes.json").read_text())
    env = {}
    for it in data.get("led_head_lite", []):
        (u0, u1), (v0, v1), h, lead = it["u"], it["v"], it["h"], it["lead"]
        pts = [head_xy(u, v) for u in (u0, u1) for v in (v0, v1)]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        zf, zb = Z_LEDPCB, Z_LEDPCB + PCB_T
        front = it["side"] == "front"
        body = (zf - h, zf) if front else (zb, zb + h)
        if it["ref"] == "J1":
            h = J1_STACK                                       # header + mated socket + the ribbon's turn
            lead = 4.0                                         # untrimmed header tails + solder on the LED side
        legs = (zb, zb + lead) if front else (zf - lead, zf)
        if it["fp"].startswith("LED_THT") and front:
            # the LEDs are round and reach through the bezel window: model them as they are - a 5.8 mm
            # collar 1 mm tall on the board, then the 5.0 mm body (a box would clip the window corners)
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            env[f"led_head_lite.{it['ref']}"] = zcyl(cx, cy, 2.9, zf - 1.0, zf).union(
                zcyl(cx, cy, 2.5, zf - h, zf - 0.9))
        else:
            env[f"led_head_lite.{it['ref']}"] = box(x0, x1, y0, y1, *body)
        if it["fp"].startswith(("LED_THT", "Connector")):       # through-hole parts only
            env[f"led_head_lite.{it['ref']}.leads"] = box(x0 + 1, x1 - 1, y0 + 1, y1 - 1, *legs)
    yt = H.Y_BTN + DY
    yb = yt - PCB_T
    for it in data.get("control_lite", []):
        (u0, u1), (v0, v1), h, lead = it["u"], it["v"], it["h"], it["lead"]
        x0, x1, z0, z1 = u0, u1, H.BTN_ZC + v0, H.BTN_ZC + v1
        front = it["side"] == "front"
        body = (yt, yt + h) if front else (yb - h, yb)
        legs = (yb - lead, yb) if front else (yt, yt + lead)
        env[f"control_lite.{it['ref']}"] = box(x0, x1, body[0], body[1], z0, z1)
        env[f"control_lite.{it['ref']}.leads"] = box(x0 + 1, x1 - 1, legs[0], legs[1], z0 + 1, z1 - 1)
    return env


ORIENT = {   # print orientation: flat face on the bed, no supports
    "bezel": lambda s: s,
    "body": lambda s: s.rotate((0, 0, 0), (1, 0, 0), 90),
    "lid": lambda s: s.rotate((0, 0, 0), (1, 0, 0), -90),
    "door": lambda s: s.rotate((0, 0, 0), (1, 0, 0), -90),         # inner face (tongue) down
}


def oriented(name, s):
    o = ORIENT[name](s)
    bb = o.val().BoundingBox()
    return o.translate((-bb.center.x, -bb.center.y, -bb.zmin))


def make_parts():
    return {"bezel": make_bezel(), "body": make_body(), "lid": make_lid(),
            "door": make_door()}


def main():
    out_step, out_stl = HERE / "step_lite", HERE / "stl_lite"
    for d in (out_step, out_stl):
        d.mkdir(exist_ok=True)
        for f in d.glob("*"):
            f.unlink()
    parts = make_parts()
    placed_caps = make_caps()
    caps = {"dpad": make_dpad_cap(), "select": make_select_cap(), "back": make_back_cap()}   # the cap models to print
    ref, acc, env = reference_solids(), access_solids(), board_envelopes()

    keep = {**ref, **acc, **env}
    key_paths = {k for k in acc if k.startswith("key_path_")}
    # the door closes the bay, so it is left out of the "cells out" check (that's with the door off)
    lines = interference({k: v for k, v in parts.items() if k != "door"},
                         {k: v for k, v in keep.items() if k != "door_screw"})
    lines += interference({"door": parts["door"]},
                          {k: v for k, v in keep.items() if k not in ("door_screw", "aa_cells_out")})
    lines += interference({k: v for k, v in parts.items() if k != "body"}, {"door_screw": acc["door_screw"]})
    lines += interference({f"cap_{n}": c for n, c in placed_caps.items()},
                          {k: v for k, v in keep.items() if k not in key_paths})
    print(f"Lite housing vs {len(keep)} keep-out volumes ({len(env)} from the KiCad boards):")
    print("\n".join(lines) if lines else "  no clashes")
    names = list(parts)
    pl = []
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            v = volume(parts[a].intersect(parts[b]))
            if v > 0.01:
                pl.append(f"  OVERLAP {a} x {b}: {v:.2f} mm^3")
    for n, c in placed_caps.items():                                  # caps in their lid holes
        v = volume(c.intersect(parts["lid"]))
        if v > 0.01:
            pl.append(f"  OVERLAP cap {n} x lid: {v:.2f} mm^3")
    names_c = list(placed_caps)
    for i, a in enumerate(names_c):                                     # caps' bases against each other
        for b in names_c[i + 1:]:
            v = volume(placed_caps[a].intersect(placed_caps[b]))
            if v > 0.01:
                pl.append(f"  OVERLAP cap {a} x cap {b}: {v:.2f} mm^3")
    print("housing parts and key caps vs each other:", "no overlaps" if not pl else "\n" + "\n".join(pl))
    gap = (Y_LID - H.CELL_BOTTOM) - Y_ROOF1
    print(f"bay roof to the display's 18650 cell: {gap:.2f} mm (both ribbons pass here, side by side)")
    gap = (Y_LID - H.HDR_STACK) - Y_ROOF1
    print(f"bay roof to the 16-way ribbon's socket on the display header: {gap:.2f} mm")
    print("print sizes as oriented (Prusa MINI 180 x 180 x 180):")
    for name, p in parts.items():
        bb = oriented(name, p).val().BoundingBox()
        print(f"  {name:9s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:6.1f} (h)  "
              f"{'OK' if max(bb.xlen, bb.ylen, bb.zlen) <= 180 else 'TOO BIG'}")
    print(f"overall: {2 * BHO:.1f} wide x {BODY_H:.0f} tall x {Z_BODY1 - ZF:.1f} long (head and body are one block), "
          f"bezel {BEZEL_THICK:.1f} mm thick, front flush with the LED tips")

    asm = cq.Assembly(name="long_range_remote_lite_housing")
    col = {"bezel": (0.2, 0.2, 0.22), "body": (0.32, 0.34, 0.37),
           "lid": (0.2, 0.2, 0.22), "door": (0.2, 0.2, 0.22)}
    for name, p in parts.items():
        cq.exporters.export(p, str(out_step / f"{name}.step"))
        asm.add(p, name=name, color=cq.Color(*col[name], 1))
    for name, k in caps.items():
        cq.exporters.export(k, str(out_step / f"keycap_{name}.step"))
    for name, c in placed_caps.items():
        asm.add(c, name=f"keycap_{name.lower()}",
                color=cq.Color(0.85, 0.4, 0.15, 1))
    refasm = cq.Assembly(name="reference_components")
    for name, r in ref.items():
        refasm.add(r, name=name, color=cq.Color(0.1, 0.5, 0.2, 1) if "pcb" in name
                   else cq.Color(0.6, 0.6, 0.65, 1))
    leds = led_reference_solids()                                      # the 66 SFH 4545, each its own body
    ledasm = cq.Assembly(name="leds")
    for ref_name in sorted(leds, key=lambda r: int(r[1:])):
        ledasm.add(leds[ref_name], name=f"led_{ref_name}", color=cq.Color(0.75, 0.75, 0.8, 1))
    refasm.add(ledasm, name="led_head_leds")
    print(f"reference LEDs in the assembly: {len(leds)}")
    asm.add(refasm, name="reference_components")
    asm.export(str(out_step / "long_range_remote_lite_housing_assembly.step"))
    for name, p in parts.items():
        cq.exporters.export(oriented(name, p), str(out_stl / f"{name}.stl"), tolerance=0.02, angularTolerance=0.1)
    for name, k in caps.items():                                       # base down, top up
        cq.exporters.export(k.translate((0, 0, CAP_BASE_T)), str(out_stl / f"keycap_{name}.stl"),
                            tolerance=0.01, angularTolerance=0.1)
    # Support enforcer for the battery bay: the bay roof is a 59 mm span 20 mm above the bed, too long to
    # bridge cleanly. Load this in the slicer as a support enforcer on body.stl (same coordinates as the
    # body's STL) and set supports to "enforcers only": the bay opens onto the bed, so the supports pull out
    # through the door opening.
    bo = ORIENT["body"](parts["body"]).val().BoundingBox()
    shift = (-bo.center.x, -bo.center.y, -bo.zmin)
    enf = box(BAY_X0 + 1.0, BAY_X1 - 1.0, Y_BOT, Y_ROOF0 - 0.1, BAY_Z0 + 1.0, BAY_Z1 - 1.0)
    cq.exporters.export(ORIENT["body"](enf).translate(shift), str(out_stl / "body_bay_support_enforcer.stl"),
                        tolerance=0.05)
    print("exported STEP ->", out_step, " STL ->", out_stl)
    return parts, placed_caps, ref


if __name__ == "__main__":
    main()
