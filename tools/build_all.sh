#!/usr/bin/env bash
# Rebuild everything: LED head + control board (schematic, netlist, PCB, routing, DRC) -> cross-board check ->
# layout-quality check ->
# BOM -> previews -> JLCPCB gerber zips -> PDFs. (3D renders: tools/render_3d.py, needs KiCad's 3D models.)
#
# Needs: KiCad 7+ (pcbnew Python module + kicad-cli), python3 with cairosvg + Pillow (previews) + reportlab + pypdf (PDFs),
#        Java 17+ and tools/bin/freerouting.jar (only to re-autoroute the control board:
#        https://github.com/freerouting/freerouting/releases, v2.1.0 used here).
#
#   tools/build_all.sh            re-uses the committed route (control_lite/build/control_lite.ses)
#   tools/build_all.sh --reroute  runs Freerouting again (only needed after moving parts)
set -euo pipefail
cd "$(dirname "$0")/.."

ROUTE="--reuse"
[[ "${1:-}" == "--reroute" ]] && ROUTE=""

# 66-LED head + control board, 4 x AA
python3 tools/led_head_lite.py
python3 tools/control_lite.py $ROUTE
python3 tools/check_system.py
python3 tools/check_layout.py      # placement / routing quality that DRC doesn't check (fails the build)
python3 tools/bom.py

python3 - <<'EOF'
import sys; sys.path.insert(0, "tools"); import kigen
kigen.render("led_head_lite/led_head_lite.kicad_pcb", "previews", "led_head_lite", bbox=(98, 75, 202, 125), px_per_mm=12)
kigen.render("control_lite/control_lite.kicad_pcb", "previews", "control_lite", bbox=(107, 72, 193, 128), px_per_mm=14)
for n in ("led_head_lite", "control_lite"):
    kigen.render_sch(f"{n}/{n}.kicad_sch", f"previews/{n}_schematic.png")
EOF

mkdir -p gerbers
for n in led_head_lite control_lite; do
  d=$(mktemp -d)
  kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
      --subtract-soldermask --no-x2 -o "$d/" "$n/$n.kicad_pcb" >/dev/null
  kicad-cli pcb export drill --format excellon --drill-origin absolute --excellon-units mm --excellon-separate-th \
      --generate-map --map-format gerberx2 -o "$d/" "$n/$n.kicad_pcb" >/dev/null
  rm -f "gerbers/${n}_jlcpcb.zip"
  (cd "$d" && zip -q -r - .) > "gerbers/${n}_jlcpcb.zip"
  rm -rf "$d"
done
python3 tools/export_pdfs.py
echo "done - see previews/, pdf/, bom/, gerbers/"
