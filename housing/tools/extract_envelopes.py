#!/usr/bin/env python3
"""
Export component keep-out boxes for the long-range remote PCBs (the Lite LED head and control board) to ref/board_envelopes.json.

Each footprint becomes an axis-aligned box: its KiCad bounding box (pads + graphics) in the board
plane, extruded away from the board on its own side by a height for that part type (mated cables
included for connectors), plus a 2 mm lead-protrusion box on the far side for through-hole parts.
Board coordinates are relative to the board centre (150, 100) in KiCad, u right / v down, front
side view. housing_lite.py maps them into the housing.

Needs KiCad's pcbnew Python module. Run from anywhere:  python3 housing/tools/extract_envelopes.py
"""
import json
import re
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parents[2]        # long_range_remote/
OUT = ROOT / "housing" / "ref" / "board_envelopes.json"
CX, CY = 150.0, 100.0

# (regex on "lib:footprint", height above the board on the part's own side in mm)
HEIGHTS = [
    (r"LED_THT:LED_D5\.0mm", 8.7),                     # SFH 4545 seated flat
    (r"Resistor_THT:R_Axial", 3.0),
    (r"Resistor_SMD", 0.8),                            # LED head string resistors (1206)
    (r"Capacitor_THT:CP_Radial_D10", 15.0),            # 470 uF 10x14 + standoff
    (r"Capacitor_THT:C_(Rect|Disc)", 7.0),
    (r"TO-220-3_Vertical", 20.0),
    (r"Package_DIP", 9.5),                             # DIP + optional socket
    (r"TO-92", 5.5),                                   # 2N3904
    (r"Connector_IDC", 18.0),                          # box header 9 + mated IDC socket 9
    (r"JST_XH", 20.0),                                 # XH header 8 + plug and wire bend
    (r"PinHeader_2x05", 20.0),                         # LED head J1: header 8.5 + IDC socket 9 + cable bend
    (r"SW_PUSH_6mm", 4.3),                             # Omron B3F-1000
    (r"TO-252", 2.5),                                  # DPAK P-FET (Lite LED power gate)
]
LEAD = 2.0                                             # THT lead protrusion on the far side


def height_for(fpid):
    for pat, h in HEIGHTS:
        if re.search(pat, fpid):
            return h
    return None


def main():
    out = {}
    for name in ("led_head_lite", "control_lite"):
        b = pcbnew.LoadBoard(str(ROOT / name / f"{name}.kicad_pcb"))
        items = []
        for fp in b.GetFootprints():
            fpid = f"{fp.GetFPID().GetLibNickname()}:{fp.GetFPID().GetLibItemName()}"
            h = height_for(fpid)
            if h is None:                  # mounting holes etc.
                continue
            bb = fp.GetBoundingBox(False, False)
            u0, u1 = pcbnew.ToMM(bb.GetLeft()) - CX, pcbnew.ToMM(bb.GetRight()) - CX
            v0, v1 = pcbnew.ToMM(bb.GetTop()) - CY, pcbnew.ToMM(bb.GetBottom()) - CY
            items.append(dict(ref=fp.GetReference(), fp=fpid, side="back" if fp.IsFlipped() else "front",
                              u=[round(u0, 2), round(u1, 2)], v=[round(v0, 2), round(v1, 2)],
                              h=h, lead=LEAD))
        out[name] = items
        print(f"{name}: {len(items)} parts")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(out, indent=1))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
