#!/usr/bin/env python3
"""
Preview renders of the housing (VTK, offscreen):
  previews/lite_assembled.png          rear three-quarter view, as you'd hold it
  previews/lite_keys.png               close-up of the deck: D-pad, SELECT and BACK caps
  previews/lite_front.png              front three-quarter view: the bezel
  previews/lite_bottom.png             underneath: the battery door
  previews/lite_exploded.png           bezel / body (head + tray) / lid pulled apart, boards in place
  previews/lite_section.png            cut at x = 0 (through the battery bay), looking at the cut face
  previews/lite_print_orientation.png  every part as it sits on the Prusa MINI bed
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import housing_lite as L  # noqa: E402
from render import actor, render  # noqa: E402

PCOL = {"bezel": "dark", "body": "housing", "lid": "dark", "door": "dark", "pwr_lever": "cap"}
REFCOL = {"led_head_pcb": "pcb", "control_pcb": "pcb", "aa_holder": "cells",
          "display": "display", "display_18650": "batt"}


def main():
    out = HERE / "previews"
    out.mkdir(exist_ok=True)
    parts = L.make_parts()
    pcaps = list(L.make_caps().values())
    ref = L.reference_solids()
    zc = (0, -10, 92)
    env = L.board_envelopes()

    acts = [actor(p, PCOL[n]) for n, p in parts.items()]
    acts += [actor(ref["display"], "display")] + [actor(c, "cap") for c in pcaps]
    render(acts, out / "lite_assembled.png", cam_pos=(230, 230, 430), focal=zc, zoom=1.15,
           title="Lite assembled: one 107 x 52 mm block - 66-LED head, AA bay, screen + D-pad, SELECT and BACK on the deck")
    deck = parts["lid"].intersect(L.box(-48, 48, -20, 30, 128, 172))          # just the key area of the lid
    render([actor(deck, "dark")] + [actor(c, "cap") for c in pcaps], out / "lite_keys.png",
           cam_pos=(0, 160, 230), focal=(0, 15, 150), view_up=(0, 0, -1), zoom=1.1,
           title="Lite keys: triangular D-pad caps (one model, turned), round SELECT, square BACK")
    leds = [v for k, v in env.items() if k.startswith("led_head_lite.D") and not k.endswith(".leads")]
    front_acts = acts + [actor(ref["led_head_pcb"], "pcb")] + [actor(l, "display") for l in leds]
    render(front_acts, out / "lite_front.png", cam_pos=(-150, 90, -330), focal=(0, -11, 60), zoom=1.3,
           title="Lite front: the bezel is flush with the 66 LED tips")
    render(acts, out / "lite_bottom.png", cam_pos=(260, -330, 330), focal=(0, -20, 100), zoom=1.3,
           title="Lite underneath: battery door (tongue at the front, one M3 screw at the back)")

    # pulled apart along each part's assembly direction: bezel + LED head forward (-z), lid + caps up, AA
    # holder + door down out of the bay
    off = {"bezel": (0, 0, -75), "body": (0, 0, 0), "lid": (0, 70, 0),
           "door": (0, -85, 0), "led": (0, 0, -40), "cells": (0, -55, 0), "pwr_lever": (0, 70, 0)}
    roff = {"led_head_pcb": "led", "control_pcb": "lid", "aa_holder": "cells",
            "display": "lid", "display_18650": "lid"}
    acts = [actor(p, PCOL[n], off[n]) for n, p in parts.items()]
    acts += [actor(ref[n], c, off[roff[n]]) for n, c in REFCOL.items()]
    acts += [actor(c, "cap", (0, 70 + 28, 0)) for c in pcaps]
    render(acts, out / "lite_exploded.png", cam_pos=(420, 170, -60), focal=(0, -5, 75), zoom=1.3, size=(2000, 1500),
           title="Exploded: bezel, LED head, body, AA holder + door, lid (with the display and control board), key caps")

    # display-button access slot: lid + sleeve front wall + display, from above, and a section at x = 0
    ze = L.H.Z_DISP_C - L.H.DISP_H / 2
    zone = L.box(-30, 30, -30, 30, ze - 14, ze + 22)
    render([actor(parts["lid"].intersect(zone), "dark"), actor(ref["display"].intersect(zone), "display")],
           out / "lite_display_buttons.png", cam_pos=(0, 170, ze + 5), focal=(0, 10, ze + 4), view_up=(0, 0, -1),
           zoom=1.4, title="Display buttons: slot in the lid (dark) in front of the display's top edge, centred")
    cutter = L.box(-300, 0, -300, 300, -100, 400)
    acts = [actor(p.cut(cutter), PCOL[n]) for n, p in parts.items()]
    acts += [actor(ref[n].cut(cutter), c) for n, c in REFCOL.items()]
    render(acts, out / "lite_section.png", cam_pos=(-400, 0, 105), focal=(0, -10, 105), zoom=1.45,
           title="Lite section at x = 0: LED head | display over the AA bay | control board")

    acts = []
    for i, (n, s) in enumerate(parts.items()):
        dx = i * 200
        acts.append(actor(L.oriented(n, s), PCOL[n], (dx, 0, 0)))
        acts.append(actor(L.box(-90, 90, -90, 90, -2, 0), "bed", (dx, 0, 0)))
    render(acts, out / "lite_print_orientation.png", cam_pos=(400, -380, 280), focal=(400, 0, 20),
           view_up=(0, 0, 1), size=(2000, 700), zoom=2.3,
           title="Lite, as printed: bezel | body (floor down) | lid (top down) | door (inside down) | PWR lever (on its side)")
    print("previews written to", out)


if __name__ == "__main__":
    main()
