# Long-range remote: 3D-printed housing (Prusa MINI)

One **107.4 × 52 mm block**, 170.5 mm long, that you hold in both hands. The LED head (99.8 ×
44.8 mm) fills its front, the screen sits at the front of the top deck, and the D-pad and BACK (left
thumb) and SELECT (right thumb) sit behind it, so your hands never cover the screen. A **battery bay
for four AA NiMH cells** sits under the display, opened from underneath.

The model is `housing_lite.py` (CadQuery). It builds on `housing_base.py`, which holds the shared
frame, the lid with its display sleeve, and the key layout; run `housing_lite.py`, not the base.
(The `_lite` names are historical: this is the only version.)


The head end has exactly the body's cross-section, with the LED head (99.8 × 44.8 mm) filling its
front, so the whole remote reads as a single piece.

- **Size:** **107.4 mm wide × 52 mm tall × 170.5 mm long**, head and body alike.
- **Bezel flush with the LEDs:** the bezel is 6.7 mm thick and its front face is level with the LED
  tips, so the remote can lie face-down on it without resting on the LEDs. Its back is 2 mm in front
  of the LED board, and the LEDs reach through its window with at least 0.7 mm to every edge.
- **Shape:** the bezel and the body (head and tray in one part) are cut from **one rounded block**, so every edge runs on
  unbroken across the joins: top edges R1.5, long bottom edges R4, back corners R8, front corners
  and front bottom edge R3 (kept small because the bezel prints front-down). The lid's top edge has
  a 0.3 mm chamfer that shows as a fine line against the tray's rounded rim, and its key holes a
  0.4 mm chamfer. The side slots and the bezel window have rounded ends / corners.
- **Parts:** five printed parts (bezel, body, lid, battery door, PWR lever) plus the key caps (**one
  D-pad model printed four times**, a SELECT cap and a BACK cap), and an 11 mm piece of 1.75 mm
  filament as the lever's pin. There is no power switch: the display board's own PWR button turns the
  remote on and off (through the PWR lever), and the ESP32 switches the AA pack for the LEDs.
- **Power:** the AA cells run the LEDs only. The display board and ESP32 run from the display
  board's own 18650, charged through its USB-C, which is reachable through the right wall.

```
 side view, front = left                                                 y up, z towards you
       z 4.0 10.7 29.5                                                      174.5
        ┌──┬───────┬─────────────────────────────────────────────────────────┐ y 14.6
        │B │ LED   │  LID (top deck): display window | D-pad, SELECT, BACK   │
        │E │ cavity│  [ display + its 18650 ]     [ control board 82 × 52 ]  │
        │Z │       │  ┌─────────────────┐ ribbon over the bay               │
        │L │  J1 ──┼─►│ 4 × AA holder   │                                   │
        └──┴───────┴──┴═══ door ═══════╧┴───────────────────────────────────┘ y −37.4
          107.4 wide × 52 tall × 170.5 long, one cross-section; bezel flush with the LED tips
```

- **LED cavity (the head end of the body).** There is no separate head: the cavity at the front of the body
  is open to the bezel and closed behind by a single 3 mm wall. The LED head (99.8 × 44.8 mm, R3 corners, M3 holes at (±44.2, ±14.0)) slides in
  from the front and is clamped with 4 × **M3 × 25** countersunk screws through
  the bezel and its short (2 mm) posts into columns at the back of the cavity, fused into the side
  walls. A 4 mm deep pocket in the bezel's back over J1 leaves its pin tails 6 mm of room, so they
  don't need trimming. J1 lies
  along the board's bottom edge, centred: the 10-way ribbon goes straight back through the opening
  at floor level, then rises over the battery bay's roof and runs under the display to the control
  board. There are no vents: the LEDs average about 4 W only while sending.
- **Rest of the body (the tray).** It carries:
  - the **battery bay**: a closed box under the display for a Keystone 2478 4 × AA holder
    (63 × 58 × 16 mm), open side down. The holder's leads go up through a hole in the bay roof
    to control J1;
  - the **door** recess in the bottom face, the slot for the door's front tongue, and a boss behind
    the bay with a pocket for the door screw's M3 nut;
  - USB-C and TF-card slots for the display in the right wall.

- **Door.** Flush with the bottom. Its tongue hooks into the slot at the front; one **M3 × 10
  countersunk** screw holds the back. To swap cells: undo the screw, lift the door out by the
  finger notch beside the screw, swap the four cells in the holder, and refit (tongue in first, at a slight
  angle, then swing the back up).
- **Lid** (the top deck). It carries the display window with a snap-in sleeve underneath, and the
  control board on four bosses behind it, keys up through the lid. 4 × M3 × 10 countersunk screws fix
  it (front pair at x ±43.7, back pair at x ±45.6, 136.6 mm behind the cavity's rear wall), each
  countersink fully inside the lid's rounded corner, with at least 0.8 mm of lid round it.
  - **Display retention:** four snap hooks (x ±28, on the front and rear walls) catch the display's
    flat back plate by 1.0 mm, on 13 mm flexing arms, 15 mm wide. The sleeve's long walls have a 2 mm
    45° chamfer along the outside of their joint with the lid, where a hook first snapped off (the lid
    prints top-down, so that joint bends across the layer lines: PETG is much tougher here than PLA).
    The slits beside each arm stop 3.5 mm below the lid, so the arms flex below that joint, and their
    ends are fully rounded; the catch has a small chamfer where it meets the arm. Two 0.15 mm crush ribs
    on each of those walls (x ±40) take up the sleeve's clearance, so the display sits centred and no
    hook loses its catch.
  - **Display version:** Waveshare ship a V2 board from 10 Oct 2026 (the QC label says which). Its
    thicker acrylic spacer puts the board and everything on it 0.6 mm further from the screen. Their 3D
    model is V1, so `DISP_V2 = True` in `housing_base.py` adds the 0.6 mm to every depth taken from it
    (PWR lever nub, USB-C and TF openings, 18650 and header clearances). The overall thickness is the
    one measured on a V2 board, 14.35 mm (Waveshare say 14.1), and the catches sit `HOOK_SLACK`
    (0.15 mm) beyond it. For a V1 board set `DISP_V2 = False`.
  - **To take the display out:** lid off, then push each hook arm outwards with a thumbnail or a small
    screwdriver while you press the back of the display; it comes out towards you.
- **PWR lever.** The display's PWR button sits on its front edge and is pressed sideways, so a small
  lever does it from the face of the lid. Its tab stands 3 mm proud of the lid just in front of the
  screen, in the middle. **Flick the tab forwards** (towards the LED head): the lever pivots on a
  1.75 mm filament pin in two cheeks under the lid, and a nub on its lower end pushes PWR's plunger.
  A short flick turns the screen on; hold it forwards to turn it off. The button's own spring
  returns it, and the slot in the lid stops the tab at 11°, so the nub can't push harder than the
  switch needs.
  - Checked against Waveshare's 3D model: at rest it touches nothing; at its stop the nub reaches
    0.45 mm past the plunger's tip (the switch travels ~0.25 mm), below the board edge and well short
    of the switch body.
  - **If a flick doesn't click PWR,** lower `NUB_GAP` (0.3 mm) in `housing_lite.py` and reprint only
    the lever (a 5-minute print). If PWR is pressed with the lever at rest, raise it.
  - KEY and BOOT, either side of PWR, can't be reached with the lever fitted. They don't need to be:
    KEY is the same GPIO as UP, and BOOT the same as SELECT (hold SELECT at power-up for the
    bootloader).
  - For the open notch instead (fingertip access to all three buttons, no lever), set
    `PWR_LEVER = False` in `housing_lite.py`.
- **Key caps.** The D-pad caps are **triangles** pointing outwards, in triangular lid holes, so they
  can't turn and the shape shows the direction: nothing is engraved. All four are the same model,
  turned. SELECT is a plain round cap and BACK a rounded square, so you can tell the three apart by
  feel. Each cap has a thick round base under the lid (Ø13.2 for the D-pad and BACK, Ø14 for SELECT) that keeps it in and presses the switch directly, so there's no stem and it
  prints base-down without supports. At rest the base sits 0.2 mm above the switch plunger.
- **Bezel window.** Shaped like the LED field: full width across the middle three rows, cut back at
  the four corners where the holes are, every corner R2. Each post has 3.6 mm of bezel round it.
  Each screw's countersink (Ø6.4 at the face) comes within 0.4 mm of the window there, so for the first
  0.45 mm the countersink opens into the window's corner. Otherwise the face, which prints first, would
  have a feather-thin ring round each screw.

## Files

| Path | What it is |
|---|---|
| `step_lite/long_range_remote_lite_housing_assembly.step` | **Open this in Fusion 360.** Every part in place, plus reference bodies for the boards, display and AA holder, and all 66 LEDs (each its own body, tips flush with the bezel). |
| `step_lite/<part>.step`, `stl_lite/<part>.stl` | Each part; the STLs are in print orientation |
| `housing_lite.py` | The parametric model (CadQuery). Run this one. |
| `housing_base.py` | Shared frame, helpers, the lid with its display sleeve, the key layout (imported by `housing_lite.py`; keep the two together) |
| `tools/extract_envelopes.py` | Reads the two KiCad boards and writes `ref/board_envelopes.json` (component keep-out boxes) |
| `stl_lite/body_bay_support_enforcer.stl` | Support enforcer for the battery bay roof (see printing) |
| `tools/render_lite.py`, `tools/render.py` | Make `previews/lite_*.png` (render.py holds the VTK helpers) |

## Opening it in Fusion 360

- **Opening it:** use **File → Open → From my computer**, pick the assembly STEP, and Fusion
  converts it.
- **Editing:** parts arrive as solid bodies. Edit them with direct modelling (Press Pull, Offset
  Face, Combine), or turn on **Capture Design History** before adding parametric features.
- **Big changes:** wall thickness, body length or the deck layout are quicker to change through the
  parameters in `housing_lite.py`. Run `pip install cadquery shapely`, then `python3 housing_lite.py`.

## Printing (Prusa MINI)

| Part | Orientation (as in the STLs) | Size (mm) |
|---|---|---|
| `bezel` | front face down, posts up | 107.4 × 52 × 8.7 |
| `body` | floor down | 107.4 × 163.8 × 52 |
| `lid` | top face down | 100.9 × 138.5 × 19.5 |
| `door` | inside face (tongue) down; the countersink opens upward | 70 × 75.6 × 3 |
| `pwr_lever` | on its side (pin hole vertical); 100 % infill | 4.9 × 12.4 × 6 |
| `keycap_dpad` | base down — **print 4** | 13.2 Ø × 9.7 |
| `keycap_select` | base down | 14 Ø × 9.7 |
| `keycap_back` | base down | 13.2 Ø × 9.7 |

**Material:** PETG is recommended (it stays stiff in a hot car and handles the snap hooks well); PLA
works for indoor use. **Settings:** 0.2 mm layers, 3 perimeters, 15–20 % infill. One exception to
"no supports": **the body needs support under
the battery bay roof.** The roof spans 59 mm, 20 mm above the bed, which is too long to bridge cleanly.
Load `body_bay_support_enforcer.stl` onto `body.stl` as a *support enforcer* (PrusaSlicer: right-click
the part → Add support enforcer → Load; it lines up by itself) and set supports to *For support
enforcers only*. The bay is open to the bed, so the supports pull out through the door opening.
Everything else prints without support: the LED cavity roof is a 15 mm bridge, and the tongue slot is
a short bridge (30 mm) just above the bed. The tray's R4 bottom edges and the bezel's
R3 front edges start on the bed, so their first millimetre is a slight overhang; PETG handles it
without support (use a brim only if your bed adhesion is marginal).

## Hardware

| Qty | Item | Used for |
|---|---|---|
| 4 | **M3 × 25** countersunk (DIN 7991 / ISO 10642) | bezel → LED head → body columns (15 mm of thread) |
| 4 | M3 × 10 countersunk | lid → tray columns |
| 4 | M3 × 8 pan head | control board → lid bosses |
| 1 | **M3 × 10 countersunk + M3 nut** | battery door (the nut drops into the hex pocket from inside the tray) |
| 1 | Keystone 2478 4 × AA holder, 6" leads | the AA pack (in the BOM) |
| — | JST XH 2-pin plug (XHP-2 + 2 crimps), from stock | holder leads → control J1. Or solder the leads straight into J1's holes |
| — | double-sided foam tape | holds the holder to the bay roof |
| 4 | AA NiMH cells, ~2000 mAh, low self-discharge | not alkalines (see the main README) |

## Assembly

1. **Battery holder.**
   1. Fit the XH plug to the holder's leads: red (+) to pin 1 (BATT), black to pin 2 (GND).
   2. Feed the leads up through the hole in the bay roof from underneath, then stick the holder to
      the roof with foam tape (about 1 mm thick; the bay allows for it), open side down.
   3. Drop the M3 nut into the hex pocket behind the bay (from inside the tray).
2. **LED head.** Feed the 10-way ribbon through the opening in the cavity's rear wall from the tray side,
   plug it into J1 (pin 1 marked) with the board held in front of the cavity, J1 at the **bottom**, then slide the board in, then the bezel and 4 × M3 × 25. J1's pin
   tails can stay as they are: the bezel has a pocket for them.
3. **Lid.** Drop the caps into their holes from underneath: the four triangles each point outwards
   (UP towards the LED head), SELECT on the right, the square BACK at the D-pad's lower right.
   **PWR lever:** with the lid upside down, push the lever's tab up through its slot from underneath,
   nub towards the display window, and line its hole up with the cheeks'. Cut an 11 mm piece of
   1.75 mm filament, melt one end into a small head with a soldering iron (or a lighter), and push it
   in sideways through one cheek, the lever and the other cheek. The holes are a loose fit (2.2 mm in
   the cheeks, 2.4 mm in the lever), so it slides straight in; a dab of glue on the head, against the
   cheek and away from the lever, keeps it there. The lever should swing freely. Then screw on the control board and push the display
   into its sleeve from underneath until the four hooks click over its back plate (glass to the
   window, USB-C on the **right**, image top towards the LED head). It takes a firm push: the crush ribs
   are meant to be tight.
4. **Cables:** 16-way ribbon (cut to ~70 mm, see the main README) from the display header to
   control J2, both in the lid; holder plug → control J1; 10-way ribbon over the bay roof → control
   J5. Under the display's 18650 cell the two ribbons lie side by side, flat, 10-way on the left half
   of the roof and 16-way on the right half (there is about 3 mm there).
   Close with 4 × M3 × 10.
5. **Cells:** charge four NiMH cells, put them in, fit the door.

## How it was checked

The display board's dimensions come from **Waveshare's 3D model** (`ESP32-S3-rlcd-4_2_20251208.stp`, in
Waveshare's "3D file" download for the board): the 18650 cradle and cell, the 2 × 8 header, the USB-C
and TF positions on the right edge, the side buttons and the back case. `tools/waveshare_fit.py
<that .stp>` places the whole model (868 solids, the fold-out stand left out) in the housing. It
intersects every solid with the printed parts and checks that each snap hook lands on the back case.
Result: no clashes, all four hooks fully on the case, and the USB-C receptacle and TF socket inside their
wall openings. It also swings the PWR lever to its stop: the nub meets PWR's plunger and nothing else.

`housing_lite.py` checks every printed part and key cap against **241 keep-out volumes**:

- **221 from the two KiCad boards**: each footprint as a box, its KiCad outline extruded by a height for
  that part type (mated cables included), plus 2 mm lead tails on the far side (the
  DPAK P-FET counts as 2.5 mm tall, the 2N3904 as 5.5 mm);
- the boards themselves, the display with its 18650 cradle, the cell, the header socket and the
  ribbon socket plugged into it (all from Waveshare's 3D model), and the AA holder;
- access volumes: the display's USB-C plug, a thin tool's path to the display's buttons, each key's travel, both ribbons' paths (the 10-way from the LED head over the left half of the bay roof,
  the 16-way from the display's header over the right half to J2, side by side under the 18650 holder),
  the cells coming out through the bay opening, and the door screw.

It also checks every cap against the lid (each sits in its hole with 0.3 mm all round) and the caps' bases against each other. The result is **no clashes**, no part overlaps another, and every part
fits the Prusa MINI. Also:

- the bay roof sits 17.5 mm above the door: the 16 mm holder, ~1 mm of foam tape and 0.5 mm spare. It
  clears the display's 18650 cell by 2.95 mm and the 16-way ribbon's socket on the display header by
  2.5 mm: the two ~1 mm ribbons pass there side by side;
- every printed part is one solid, with no walls under 0.8 mm (two perimeters) except the snap hooks'
  tapered lead-ins, and nothing starts in mid-air except the supported bay roof and short bridges;
- the head's rear cavity (behind the LED board) is 15.2 mm deep: J1 plus the
  mated IDC socket stack to about 11.5 mm (header body 2.5 + socket 9, the socket slides over the pins),
  and the housing allows 14.0 mm so the ribbon can turn over the socket. **Check this against your actual
  Adafruit 370 cable** before printing; if the ribbon is tight, raise `J1_STACK` in `housing_lite.py`
  (everything behind the cavity moves with it);
- head and tray are **one part** (rear wall 3 mm instead of two); it prints floor-down, 163.8 mm long. The
  roof over the LED cavity spans only its 15 mm depth, so it bridges; the upper corner columns are
  short shelves off the side wall, which print without support but may need slower first layers;
- the LED head sits in a 101.4 × 46 mm cavity: 0.8 mm each side, 0.6 mm top and bottom;
- the nearest LED clears each bezel post by 0.7 mm (the grid's corner positions are left empty
  for the holes), and the window's edges by 0.7–0.9 mm. In this check the
  LEDs are modelled as they are, a 5.8 mm collar and a 5 mm body, not as boxes;
- J1's pin tails are modelled 4 mm long on the LED side and clear the bezel's pocket.

## Dimensions to confirm before printing

| Parameter | Value | Source |
|---|---|---|
| `HOLD_X`, `HOLD_Z`, `HOLD_H` | 63 × 58 × 16 mm | Keystone 2478 as listed by Rapid Electronics. Measure yours; the bay has 0.5 mm clearance each side. |
| `LED_H` | 8.7 mm | typical 5 mm (T-1¾) LED height. Measure an SFH 4545 seated flat; it sets the bezel's depth. |
| `HDR_STACK` | 24.0 mm | glass top to the underside of the 16-way ribbon socket plugged into the display header: 12.45 mm to the header's face [STEP] + 2.5 mm Pololu spacer + ~9 mm IDC socket [EST]. Measure it with the plug fitted; up to ~25 mm still leaves room for the ribbons. |
| `CELL_BOTTOM` | 23.55 mm | glass top to the underside of the display's 18650 [STEP, 19 mm cell]. Measure with your cell in; up to ~24.5 mm is fine. |
| `DISP_T` | 13.5 mm | glass to back plate [STEP]. The snap hooks catch here. |

`USB_YF`, `TF_YF` and the 18650 cradle come from Waveshare's 3D model, and the header's pin 1
(bottom-left, seen from the back with the 18650 at the bottom) was checked on a real board.
