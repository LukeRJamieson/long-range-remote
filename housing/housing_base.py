#!/usr/bin/env python3
"""
Long-range remote - shared base for the housing (CadQuery): the coordinate frame, the lid with its display
sleeve and snap hooks, the display and control-board positions, the key layout and small geometry helpers.
housing_lite.py builds the actual housing (bezel, body, lid, door, key caps) on top of this; run that file,
not this one.

Frame: x to the right as seen from behind, y up, z from the front (the LED end) towards the user.
Dimension sources: [WS] Waveshare drawings, [STEP] Waveshare's 3D model of the ESP32-S3-RLCD-4.2
(ESP32-S3-rlcd-4_2_20251208.stp), [PCB] the KiCad boards, [EST] estimated.
Display front view ("xf right, yf up", the coordinates disp_xz() takes) = the STEP model seen from the glass
side, origin at the centre of its 92.5 x 70.1 back case: xf = model z - 1.6, yf = model x + 0.5. Depths are
measured down from the glass top (model y = +0.75).
"""
import os
from pathlib import Path

import cadquery as cq

HERE = Path(__file__).resolve().parent

# ============================================================================ parameters
WALL = 3.0
INNER = 101.4                  # inside width of the body
OUTER = INNER + 2 * WALL       # 107.4
HI, HO = INNER / 2, OUTER / 2  # 50.7, 53.7
R_OUT = 10.0                   # outside corner radius of the body (the lid's corners follow R_IN)
R_IN = R_OUT - WALL
PILOT_M3 = 2.7                 # self-tapping M3 into PETG/PLA
CLEAR_M3 = 3.4

# --- z stations (head)
BEZEL_T = 3.0
LED_CLEAR = 1.0
LED_H = 8.7                    # SFH 4545 height above the PCB, seated flat [EST]
PCB_T = 1.6
Z_HEAD0 = BEZEL_T
Z_LEDPCB = Z_HEAD0 + LED_CLEAR + LED_H          # 12.7
Z_LEDBACK = Z_LEDPCB + PCB_T                    # 14.3: back face of the LED head
# Space behind the LED head: J1 + the mated ribbon socket + room to turn the ribbon. housing_lite.py sets
# LRR_HEAD_BACK_SPACE (= its J1_STACK + 1.2) before importing this module, so everything behind the head
# moves with J1_STACK. There is no separate head part, so no rear wall of its own (HEAD_REAR_T = 0).
HEAD_BACK_SPACE = float(os.environ.get("LRR_HEAD_BACK_SPACE", "15.2"))
HEAD_REAR_T = float(os.environ.get("LRR_HEAD_REAR_T", "0.0"))
Z_HEAD_RW = Z_LEDBACK + HEAD_BACK_SPACE         # rear face of the space behind the LED head
Z_HEAD1 = Z_HEAD_RW + HEAD_REAR_T               # front face of the tray part (no separate head: = Z_HEAD_RW)

POST_D = 6.4                                    # bezel posts (board hole keep-out is Ø7.8)
# --- body frame. BODY_H / BODY_L place the lid (housing_lite.py moves it to its own height with DY); the
# Lite body itself is built in housing_lite.py.
BODY_H = 70.0
BODY_L = 145.0
Y_BOT = -HO                                     # -53.7
Y_TOP = Y_BOT + BODY_H                          # lid top in this frame (housing_lite.py: + DY)
LID_T = 3.0
Y_LID = Y_TOP - LID_T                           # lid underside
Z_BODY1 = Z_HEAD1 + BODY_L                      
Z_TRAY_IN0 = Z_HEAD1 + WALL                     # tray front wall inner face
Z_TRAY_IN1 = Z_BODY1 - WALL                     
LID_CLEAR = 0.25
LID_COLUMN_R = 4.0
LID_SCREWS = [(sx * 46.0, z) for z in (Z_TRAY_IN0 + LID_COLUMN_R, Z_TRAY_IN1 - 3.0) for sx in (-1, 1)]  # (x, z)

# --- display [WS], glass up under the lid window, image "up" = forward (-z)
# Waveshare ship the V2 board from 10 Oct 2026 (QC label says which): a thicker acrylic spacer puts the
# board, and everything on it and behind it, 0.6 mm further from the screen: 3.75 -> 4.35 mm from the
# LCD to the board, 13.5 -> 14.1 mm overall [Waveshare docs, "Version Compatibility Notice"]. Their 3D
# model is V1, so for V2 every depth measured from it below grows by DISP_DROP (the screen itself, against
# the lid, doesn't move). False = a V1 board.
DISP_V2 = True
DISP_DROP = 0.6 if DISP_V2 else 0.0
# Measured on the user's V2 board (calipers): 14.35 mm overall (Waveshare say 14.1), board back face
# 4.35 mm below the glass (as Waveshare say). The board depth matches DISP_DROP; the overall thickness is
# taken as measured, so the back case sits BACK_EXTRA deeper again.
DISP_W, DISP_H, DISP_T = 92.5, 70.1, 14.35 if DISP_V2 else 13.5
BACK_EXTRA = DISP_T - (13.5 + DISP_DROP)        # back case beyond the model + DISP_DROP (0.25)
GLASS_H = 69.1
ACTIVE_W, ACTIVE_H = 84.8, 63.6
ACTIVE_DX = 1.5 + ACTIVE_W / 2 - DISP_W / 2     # -2.35 (front view)
ACTIVE_DY = 2.0 + ACTIVE_H / 2 - GLASS_H / 2    # -0.75
SLEEVE_CLEAR, SLEEVE_T = 0.3, 1.6
SLEEVE_HALF = DISP_H / 2 + SLEEVE_CLEAR + SLEEVE_T             # 36.95: display centre to sleeve outside
Z_DISP_C = Z_TRAY_IN0 + 2 * LID_COLUMN_R + 0.3 + SLEEVE_HALF   # just behind the front lid columns
Y_GLASS = Y_LID                                 # glass top against the lid underside
Y_DISP_BACK = Y_GLASS - DISP_T                  # back-plate face, the snap hooks catch here
# Snap hooks: four, on the long walls, catching the back case's flat back face [STEP: square edge, full
# width]. The catch stands 1.5 mm out from the wall, so it overlaps the display's back plate by 1.2 mm
# (1.05 mm if the display sits hard against the opposite wall's crush ribs). It used to stand 0.6 mm
# out: less the 0.3 mm sleeve clearance, that was a 0.3 mm overlap - 0 if the display sat over - and the
# display popped out. The lead-in ramp is 1.7 mm tall (~40 deg from the wall).
# Strength: one hook snapped off where the sleeve meets the lid [user]. The lid prints top-down, so the
# sleeve bends across its layer lines there, at a sharp inside corner. Now: a 2 mm 45 deg chamfer along
# the outside of that joint on both long walls (SLEEVE_CHAMFER; the display side has no room for one);
# the slits stop 3.5 mm below the lid, so the arms flex below the chamfered joint, not at it; slits with
# fully rounded ends; a 0.25 mm chamfer between catch and arm; and 1.3 mm catches (1.0 mm overlap with
# the back plate) so the arms flex less on the way in (strain ~1.6 %).
HOOK_X = (-28.0, 28.0)
HOOK_W, HOOK_DEPTH = 15.0, 1.3
HOOK_ROOT_BELOW_LID = 3.5                       # top of the slits (the arms' root) below the lid underside
HOOK_GUSSET = 0.25
SLEEVE_CHAMFER = 2.0
HOOK_DROP, HOOK_RAMP = 2.0, 1.7                 # catch bottom below its top face; lead-in height
# The catches' top face sits this far below the back plate, to make sure the hooks get over it (at
# 13.5 and 14.0 mm below the glass the user's display wouldn't clip in; it measures 14.35). Catches at
# 14.5 mm. Raise it if the hooks still won't snap over; lower it if the display rattles up and down.
HOOK_SLACK = 0.15
# Crush ribs on the long walls, either side of the hooks: they take up the 0.3 mm clearance so the
# display sits centred and every hook keeps its full catch.
RIB_X, RIB_W, RIB_H = (-40.0, 40.0), 1.0, 0.15
WINDOW_CHAMFER = 1.2
# right-edge openings: (yf of the centre, depth of the centre below the glass, length along the edge, height)
USB_YF, USB_DEPTH, USB_ALONG, USB_TALL = 0.49, 4.92 + DISP_DROP, 14.0, 9.0   # USB-C receptacle 9.6 x 4.2 [STEP]
TF_YF, TF_DEPTH, TF_ALONG, TF_TALL = 19.65, 4.4 + DISP_DROP, 13.0, 5.0       # TF card slot (socket 16.1 wide) [STEP]
# Depths below the glass top of the display board's parts [STEP, + DISP_DROP for V2]
DISP_PCB_BACK = 3.76 + DISP_DROP     # the board's component face
CELL_BOTTOM = 23.55 + DISP_DROP      # the 18650's underside: it hangs in an open cradle, through the back case
HOLDER_BOTTOM = 18.95 + DISP_DROP    # the cradle's rails and end contacts
HDR_FACE = 12.45 + DISP_DROP         # the 2x8 female header's open face
HDR_STACK = 24.0 + DISP_DROP         # + the 2.5 mm spacer of the double-sided header + a ~9 mm IDC socket

# --- control board [PCB]: 82 x 52, front (key) side up under the lid, behind the display
BTN_W, BTN_H = 82.0, 52.0
BTN_HOLES = [(su * 37.5, sv * 22.5) for su in (-1, 1) for sv in (-1, 1)]      # (u, v) [PCB]
BTN_ZC = Z_DISP_C + SLEEVE_HALF + 1.0 + BTN_H / 2                              # control board centre
# Board top face to lid underside, set by the four bosses. 8.7 left every key pressed on the user's
# first build (the caps were designed for 8.7: base 0.2 mm above the plunger), so the bosses are 1 mm
# longer; the caps stay as designed (CAP_DESIGN_GAP), so they now have ~1.2 mm of free play.
CAP_DESIGN_GAP = 8.7
BTN_GAP = CAP_DESIGN_GAP + 1.0
Y_BTN = Y_LID - BTN_GAP                         # control board top face
# (name, u, v, cap Ø, switch actuator height) - u, v = switch centres on the control board [PCB]
# All six are Omron B3F-1000 6 mm switches (4.3 mm tall); SELECT gets a larger cap so it's easy to find,
# BACK (lower right of the D-pad) a square one.
KEYS = [("UP", -22.0, -10.0, 8.0, 4.3), ("DOWN", -22.0, 10.0, 8.0, 4.3), ("LEFT", -32.0, 0.0, 8.0, 4.3),
        ("RIGHT", -12.0, 0.0, 8.0, 4.3), ("SELECT", 24.0, 0.0, 10.0, 4.3), ("BACK", -8.0, 14.0, 8.0, 4.3)]
KEY_HOLE_CLEAR, CAP_FLANGE, CAP_PROTRUDE, CAP_PRELOAD = 0.6, 1.2, 2.5, 0.2


# ============================================================================ helpers
def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def rbox_z(x0, x1, y0, y1, z0, z1, r):
    """Box with its four edges parallel to z rounded."""
    return box(x0, x1, y0, y1, z0, z1).edges("|Z").fillet(r)


def rbox_y(x0, x1, y0, y1, z0, z1, r):
    """Box with its four edges parallel to y rounded."""
    return box(x0, x1, y0, y1, z0, z1).edges("|Y").fillet(r)


def zcyl(x, y, r, z0, z1):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(r).extrude(z1 - z0)


def ycyl(x, z, r, y0, y1):
    """Cylinder along y from y0 to y1 (y0 < y1)."""
    return cq.Workplane("XZ").workplane(offset=-y1).center(x, z).circle(r).extrude(y1 - y0)


def disp_xz(xf, yf):
    """Display front-view coordinates (centre origin, x right, y up) -> housing x, z."""
    return xf, Z_DISP_C - yf


def key_xz(u, v):
    return u, BTN_ZC + v


# ============================================================================ parts
def make_lid(lid_screws=None, key_holes=True):
    """The lid: top deck with the display window, the display sleeve and snap hooks, the control board's
    bosses and the countersunk lid screws, in the base frame (housing_lite.py moves it by DY).
    lid_screws: (x, z) of the four countersunk lid screws (default LID_SCREWS).
    key_holes: cut round key holes (False: the caller cuts its own)."""
    lid_screws = LID_SCREWS if lid_screws is None else lid_screws
    x0, x1 = -HI + LID_CLEAR, HI - LID_CLEAR
    z0, z1 = Z_TRAY_IN0 + LID_CLEAR, Z_TRAY_IN1 - LID_CLEAR
    l = rbox_y(x0, x1, Y_LID, Y_TOP, z0, z1, R_IN - LID_CLEAR)
    # display window (active area), chamfered on top
    wx, wz = disp_xz(ACTIVE_DX, ACTIVE_DY)
    l = l.cut(box(wx - ACTIVE_W / 2 - 0.2, wx + ACTIVE_W / 2 + 0.2, Y_LID - 1, Y_TOP + 1,
                  wz - ACTIVE_H / 2 - 0.2, wz + ACTIVE_H / 2 + 0.2))
    l = l.faces(">Y").edges(cq.selectors.BoxSelector((wx - 44, Y_TOP - 0.1, wz - 34), (wx + 44, Y_TOP + 0.1, wz + 34))) \
        .chamfer(WINDOW_CHAMFER)
    # sleeve around the display, hanging from the lid, with snap hooks on the front / back edges
    iw, ih = DISP_W + 2 * SLEEVE_CLEAR, DISP_H + 2 * SLEEVE_CLEAR       # along x, along z
    y_catch = Y_DISP_BACK - HOOK_SLACK              # the catches' top face
    y_hook0 = y_catch - HOOK_DROP
    hook_len = Y_LID - HOOK_ROOT_BELOW_LID - y_hook0
    zc = Z_DISP_C
    sleeve = (box(-iw / 2 - SLEEVE_T, iw / 2 + SLEEVE_T, y_hook0, Y_LID + 0.5, zc - ih / 2 - SLEEVE_T, zc + ih / 2 + SLEEVE_T)
              .cut(box(-iw / 2, iw / 2, y_hook0 - 1, Y_LID + 1, zc - ih / 2, zc + ih / 2)))
    for sz in (-1, 1):
        z_in = zc + sz * ih / 2
        for hx in HOOK_X:
            for sx in (-1, 1):
                xs = hx + sx * (HOOK_W / 2 + 0.5)
                zz0, zz1 = (z_in - 0.1, z_in + SLEEVE_T + 0.1) if sz > 0 else (z_in - SLEEVE_T - 0.1, z_in + 0.1)
                y_end = y_hook0 + hook_len - 0.5            # slit: 1 mm wide, its top end fully rounded
                sleeve = sleeve.cut(box(xs - 0.5, xs + 0.5, y_hook0 - 1, y_end, zz0, zz1))
                sleeve = sleeve.cut(cq.Workplane("XY").workplane(offset=zz0).center(xs, y_end)
                                    .circle(0.5).extrude(zz1 - zz0))
            # catch: lead-in ramp facing the insertion direction (-y), flat face at the back plate, and a
            # small chamfer where that face meets the arm (the back plate's edge sits clear of it)
            zt = z_in - sz * HOOK_DEPTH
            g = HOOK_GUSSET
            prof_yz = [(y_hook0, z_in), (y_hook0 + HOOK_RAMP, zt), (y_catch, zt), (y_catch, z_in - sz * g),
                       (y_catch + g, z_in)]
            catch = (cq.Workplane("YZ").workplane(offset=hx - HOOK_W / 2)
                     .polyline(prof_yz).close().extrude(HOOK_W))
            sleeve = sleeve.union(catch)
        for rx in RIB_X:                         # crush ribs, from the back plate up to the glass
            zr0, zr1 = sorted((z_in, z_in - sz * RIB_H))
            sleeve = sleeve.union(box(rx - RIB_W / 2, rx + RIB_W / 2, Y_DISP_BACK, Y_LID, zr0 - 0.01, zr1 + 0.01))
    # 45 deg chamfer along the outside of the long walls' joint with the lid, either side of the middle:
    # x 6.5..39.5 each side on the rear wall, 19..39.5 on the front; both cover the hooks (x 19.5..36.5)
    # and clear the lid-screw columns at the ends
    c = SLEEVE_CHAMFER
    for sz in (-1, 1):
        zo = zc + sz * (ih / 2 + SLEEVE_T)                # the wall's outer face
        tri = [(Y_LID + 0.01, zo - sz * 0.01), (Y_LID + 0.01, zo + sz * c), (Y_LID - c, zo - sz * 0.01)]
        # front wall (sz < 0): only behind the hooks, so the PWR lever's pin has a clear straight run of
        # 13.5 mm sideways from either cheek (the chamfer at x 6.5 blocked it [user])
        segs = ((-39.5, -19.0), (19.0, 39.5)) if sz < 0 else ((-39.5, -6.5), (6.5, 39.5))
        for xa, xb in segs:
            sleeve = sleeve.union(cq.Workplane("YZ").workplane(offset=xa).polyline(tri).close()
                                  .extrude(xb - xa))
    l = l.union(sleeve)
    # openings in the sleeve's +x wall for the display's USB-C and TF card
    for yf, depth, along, tall in ((USB_YF, USB_DEPTH, USB_ALONG, USB_TALL), (TF_YF, TF_DEPTH, TF_ALONG, TF_TALL)):
        _, z = disp_xz(0, yf)
        y = Y_GLASS - depth
        l = l.cut(box(iw / 2 - 1, iw / 2 + SLEEVE_T + 1, y - tall / 2, y + tall / 2, z - along / 2, z + along / 2))
    # button-board bosses
    for u, v in BTN_HOLES:
        x, z = key_xz(u, v)
        l = l.union(ycyl(x, z, 3.5, Y_BTN, Y_LID + 0.5))
        l = l.cut(ycyl(x, z, PILOT_M3 / 2, Y_BTN - 1, Y_BTN + 8))
    # key holes
    for name, u, v, d, _ in (KEYS if key_holes else []):
        x, z = key_xz(u, v)
        l = l.cut(ycyl(x, z, (d + KEY_HOLE_CLEAR) / 2, Y_LID - 1, Y_TOP + 1))
    # countersunk screw holes into the tray columns
    for (x, z) in lid_screws:
        l = l.cut(ycyl(x, z, CLEAR_M3 / 2, Y_LID - 1, Y_TOP + 1))
        l = l.cut(cq.Workplane("XZ").workplane(offset=-Y_TOP).center(x, z).circle(3.2)
                  .workplane(offset=1.7).circle(CLEAR_M3 / 2).loft())
    return l


def place_cap(k, name):
    """Key cap from its own frame (top = +z, arrow 'up' = +y) into the housing (top = +y, 'up' = -z)."""
    _, u, v, _, _ = next(x for x in KEYS if x[0] == name)
    x, z = key_xz(u, v)
    return k.rotate((0, 0, 0), (1, 0, 0), -90).translate((x, Y_LID, z))


# ============================================================================ reference geometry / checks
def reference_solids():
    """The control board and the display (with its 18650 holder and header socket) as keep-out boxes."""
    ref = {}
    ref["control_pcb"] = box(-BTN_W / 2, BTN_W / 2, Y_BTN - PCB_T, Y_BTN, BTN_ZC - BTN_H / 2, BTN_ZC + BTN_H / 2)
    ref["display"] = box(-DISP_W / 2, DISP_W / 2, Y_DISP_BACK, Y_GLASS, Z_DISP_C - DISP_H / 2, Z_DISP_C + DISP_H / 2)
    ypb = Y_GLASS - DISP_PCB_BACK                                   # display PCB back face [STEP]
    # 18650 cradle (xf -43.5..43.5, yf -29.45..-8.35) and the cell in it (19 mm across, to its underside) [STEP]
    ax0, az0 = disp_xz(-43.5, -8.35)
    ax1, az1 = disp_xz(43.5, -29.45)
    ref["display_18650"] = box(ax0, ax1, Y_GLASS - HOLDER_BOTTOM, ypb, min(az0, az1), max(az0, az1))
    cx0, cz0 = disp_xz(-43.5, -9.4)
    cx1, cz1 = disp_xz(43.5, -28.4)
    ref["display_18650_cell"] = box(cx0, cx1, Y_GLASS - CELL_BOTTOM - 0.2, ypb, min(cz0, cz1), max(cz0, cz1))
    # 2x8 female header (xf -10.6..10.4, yf -3.5..3.1) [STEP], then the plugged-in ribbon socket below it
    hx0, hz0 = disp_xz(-10.6, 3.1)
    hx1, hz1 = disp_xz(10.4, -3.5)
    ref["display_header_socket"] = box(hx0, hx1, Y_GLASS - HDR_FACE, ypb, min(hz0, hz1), max(hz0, hz1))
    sx0, sz0 = disp_xz(-12.9, 4.2)
    sx1, sz1 = disp_xz(12.7, -4.6)
    ref["display_header_ribbon_socket"] = box(sx0, sx1, Y_GLASS - HDR_STACK, Y_GLASS - HDR_FACE,
                                              min(sz0, sz1), max(sz0, sz1))
    return ref


def access_solids():
    """Room for the display's USB-C plug (the key paths are added by housing_lite.py)."""
    a = {}
    _, uz = disp_xz(0, USB_YF)
    uy = Y_GLASS - USB_DEPTH
    a["usb_c_plug"] = box(DISP_W / 2 - 1, HO + 20, uy - 3.0, uy + 3.0, uz - 5.5, uz + 5.5)
    return a


def volume(shape):
    try:
        return shape.val().Volume()
    except Exception:
        return 0.0


def interference(parts, keepouts):
    lines = []
    for pname, p in parts.items():
        bb = p.val().BoundingBox()
        for kname, k in keepouts.items():
            kb = k.val().BoundingBox()
            if (kb.xmin > bb.xmax or kb.xmax < bb.xmin or kb.ymin > bb.ymax or kb.ymax < bb.ymin
                    or kb.zmin > bb.zmax or kb.zmax < bb.zmin):
                continue
            v = volume(p.intersect(k))
            if v > 0.01:
                lines.append(f"  CLASH {pname:10s} x {kname:30s} {v:8.2f} mm^3")
    return lines
