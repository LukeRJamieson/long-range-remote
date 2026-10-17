#!/usr/bin/env python3
"""
Layout-quality check for both boards: the things KiCad's DRC doesn't look at, because they are legal,
just bad. DRC only checks clearances and connectivity, so a pull-down 40 mm from the pin it pulls down,
or a resistor 0.6 mm from the board edge, passes it. This script fails the build on them instead.

  1. Pads near the board edge: every pad's copper must be >= EDGE_MIN from the outline, except parts that
     belong at an edge (listed in EDGE_OK with the reason).
  2. Parts far from what they connect to: for each pad of a 2-pin part (resistors, capacitors), the
     nearest OTHER pad on the same net must be within NEAR_MAX - a series / pull part sits beside one of
     the things it joins. Power nets with deliberate long runs are listed in FAR_OK.
  3. Detours: for every routed net, track length vs. the shortest tree through its pads (a minimum
     spanning tree). Fails when the track is both > RATIO_MAX times that and > EXCESS_MAX mm longer.
     Nets whose length is set by the placement on purpose are listed in DETOUR_OK.

Run after the boards are built (tools/build_all.sh runs it). Prints one line per finding and exits 1 if any.
"""
import math
import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
EDGE_MIN = 1.5            # mm, pad copper to board outline
NEAR_MAX = 12.0           # mm, a 2-pin part's pad to the nearest other pad on its net
RATIO_MAX, EXCESS_MAX = 1.5, 8.0

# (board, ref) -> why it may sit at the edge
EDGE_OK = {
    ("led_head_lite", "J1"): "ribbon header on the bottom edge: the ribbon leaves the head straight back there",
}
# (board, ref, net) -> why that pad of that part may be far from the rest of its net (per part, so a
# misplaced part on the same net is still caught)
FAR_OK = {
    ("control_lite", "R16", "/ARM"): "R16 is Q6's base resistor and sits at Q6: the long run is ARM (low impedance, "
                                     "from J2) rather than the base node",
    ("control_lite", "J1", "/BATT"): "the pack plug sits where the AA holder's leads come up through the bay roof",
    ("control_lite", "R12", "/BATT"): "divider top beside J2 pin 7, so the high-impedance VBAT_SENSE node stays short",
    ("control_lite", "R14", "/BATT"): "Q5's gate-source resistor, in the gate-drive group with R15 / R16 above Q6",
    ("control_lite", "C7", "/VBAT"): "C7's drain side is the end node of the hand-drawn VBAT trunk",
    ("led_head_lite", "*", "/LED_RTN"): "LED_RTN is the head's back copper plane, not tracks",
    ("led_head_lite", "*", "/LED_PWR"): "LED_PWR is the head's front copper plane, not tracks",
}
DETOUR_OK = {
    ("control_lite", "/VBAT"): "the 1.5 mm VBAT trunk from J5 round the keys to C7 (drawn by hand)",
    ("control_lite", "/KEY_TXD"): "J2 pin 10 is in the header's outer row and the gap between the inner-row pins is "
                                  "too narrow for a track, so it leaves round the header's end (20 mm, shortest legal)",
}


def mst(pts):
    if len(pts) < 2:
        return 0.0
    used, rest, total = [pts[0]], list(pts[1:]), 0.0
    while rest:
        d, i = min((min(math.dist(r, u) for u in used), i) for i, r in enumerate(rest))
        total += d
        used.append(rest.pop(i))
    return total


def check(name):
    b = pcbnew.LoadBoard(str(ROOT / name / f"{name}.kicad_pcb"))
    edge = b.GetBoardEdgesBoundingBox()
    x0, y0, x1, y1 = (pcbnew.ToMM(v) for v in (edge.GetLeft(), edge.GetTop(), edge.GetRight(), edge.GetBottom()))
    lw = 0.05                                    # half the outline's line width
    x0, y0, x1, y1 = x0 + lw, y0 + lw, x1 - lw, y1 - lw
    out = []
    pads_by_net = {}
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        for p in fp.Pads():
            pos = (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
            if p.GetNetname():
                pads_by_net.setdefault(p.GetNetname(), []).append((ref, p.GetNumber(), pos))
            if not p.IsOnCopperLayer() or (name, ref) in EDGE_OK or ref.startswith("H"):
                continue
            bb = p.GetBoundingBox()
            d = min(pcbnew.ToMM(bb.GetLeft()) - x0, x1 - pcbnew.ToMM(bb.GetRight()),
                    pcbnew.ToMM(bb.GetTop()) - y0, y1 - pcbnew.ToMM(bb.GetBottom()))
            if d < EDGE_MIN:
                out.append(f"{name}: {ref} pad {p.GetNumber()} is {d:.2f} mm from the board edge (min {EDGE_MIN})")
    # 2-pin parts far from their nets
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        pads = [p for p in fp.Pads() if p.GetNetname()]
        if len(fp.Pads()) != 2 or len(pads) != 2:
            continue
        for p in pads:
            net = p.GetNetname()
            if net == "GND" or (name, ref, net) in FAR_OK or (name, "*", net) in FAR_OK:
                continue
            pos = (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
            others = [q for q in pads_by_net[net] if q[0] != ref]
            if not others:
                continue
            d, oref, onum = min((math.dist(pos, q[2]), q[0], q[1]) for q in others)
            if d > NEAR_MAX:
                out.append(f"{name}: {ref}.{p.GetNumber()} ({net}) is {d:.1f} mm from the nearest other pad on its "
                           f"net ({oref}.{onum}; max {NEAR_MAX})")
    # detours
    length = {}
    for t in b.GetTracks():
        if t.GetClass() != "PCB_VIA":
            length[t.GetNetname()] = length.get(t.GetNetname(), 0.0) + pcbnew.ToMM(t.GetLength())
    for net, pads in pads_by_net.items():
        if net == "GND" or (name, net) in DETOUR_OK or (name, "*", net) in FAR_OK or net not in length:
            continue
        direct = mst([q[2] for q in pads])
        routed = length[net]
        if direct > 0 and routed > RATIO_MAX * direct and routed - direct > EXCESS_MAX:
            out.append(f"{name}: net {net} is routed {routed:.1f} mm for a {direct:.1f} mm spread of pads "
                       f"(x{routed / direct:.1f}) - a detour")
    return out


def main():
    findings = []
    for n in ("led_head_lite", "control_lite"):
        findings += check(n)
    for f in findings:
        print("LAYOUT:", f)
    print(f"LAYOUT CHECK: {'PASS' if not findings else f'{len(findings)} finding(s)'}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
