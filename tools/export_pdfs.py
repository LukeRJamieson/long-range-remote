#!/usr/bin/env python3
"""Export printable PDFs of both boards into pdf/.

  <board>_schematic.pdf   the schematic (KiCad's own PDF export)
  <board>_pcb.pdf         the layout, A4 landscape, one view per page, each scaled to fill the page:
                            1  top: copper + silkscreen + outline
                            2  bottom: copper + silkscreen + outline (mirrored = seen from the back)
                            3  top assembly: part outlines, references and values
                            4  bottom assembly (mirrored)
                            5  top assembly at 1:1 - print at 100 % ("actual size") to check fit

Each view is exported by kicad-cli as an SVG of the board area only (so it can be enlarged; KiCad's
PDF export always prints at 1:1 on the drawing sheet), converted with cairosvg and placed on the
page with a caption.

Needs: kicad-cli (KiCad 7+), python3 with cairosvg, reportlab and pypdf.
Run from anywhere: python3 tools/export_pdfs.py   (tools/build_all.sh calls it)
"""
import io
import re
import subprocess
import tempfile
from pathlib import Path

import cairosvg
from pypdf import PdfReader, PdfWriter, Transformation
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "pdf"
MM = 72 / 25.4                       # points per mm

BOARDS = {
    "led_head_lite": "LED head, 66 x SFH 4545 on 4 x AA (99.8 x 44.8 mm)",
    "control_lite": "Control board, 4 x AA (82 x 52 mm)",
}

# (file suffix, layers, mirrored, caption, 1:1?, black & white?)
VIEWS = [
    ("top",    "F.Cu,F.SilkS,Edge.Cuts", False, "Top: copper + silkscreen", False, False),
    ("bottom", "B.Cu,B.SilkS,Edge.Cuts", True,  "Bottom: copper + silkscreen (mirrored, as seen from the back)", False, False),
    ("topasm", "F.Fab,F.SilkS,Edge.Cuts", False, "Top assembly: part outlines, references, values", False, True),
    ("botasm", "B.Fab,B.SilkS,Edge.Cuts", True,  "Bottom assembly (mirrored, as seen from the back)", False, True),
    ("top11",  "F.Fab,F.SilkS,Edge.Cuts", False, "Top assembly at 1:1 - print at 100 % / actual size", True, True),
]


def svg_view(pcb: Path, layers: str, mirror: bool, bw: bool, tmp: Path, name: str) -> Path:
    out = tmp / f"{name}.svg"
    cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--page-size-mode", "2",
           "--exclude-drawing-sheet", "-o", str(out), str(pcb)]
    if mirror:
        cmd.insert(4, "--mirror")
    if bw:                                   # yellow silkscreen is unreadable on white paper
        cmd.insert(4, "--black-and-white")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def outline_mm(pcb: Path):
    """Board size from its Edge.Cuts outline (what a fab measures), in mm."""
    import pcbnew
    b = pcbnew.LoadBoard(str(pcb))
    e = b.GetBoardEdgesBoundingBox()
    w = 0.0
    for d in b.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts:
            w = max(w, pcbnew.ToMM(d.GetWidth()))
    # the bounding box includes half the outline's line width on each side
    return pcbnew.ToMM(e.GetWidth()) - w, pcbnew.ToMM(e.GetHeight()) - w


def svg_size_mm(svg: Path):
    head = svg.read_text(errors="ignore")[:2000]
    w = float(re.search(r'width="([\d.]+)mm"', head).group(1))
    h = float(re.search(r'height="([\d.]+)mm"', head).group(1))
    return w, h


def caption_page(title: str, sub: str, footer: str) -> PdfReader:
    """A blank A4 landscape page carrying the caption text (the drawing is merged on top)."""
    buf = io.BytesIO()
    pw, ph = landscape(A4)
    c = canvas.Canvas(buf, pagesize=(pw, ph))
    c.setFont("Helvetica-Bold", 14)
    c.drawString(14 * MM, ph - 14 * MM, title)
    c.setFont("Helvetica", 11)
    c.drawString(14 * MM, ph - 20 * MM, sub)
    c.setFont("Helvetica", 8)
    c.drawString(14 * MM, 8 * MM, footer)
    c.showPage()
    c.save()
    buf.seek(0)
    return PdfReader(buf)


def board_pdf(name: str, desc: str):
    pcb = ROOT / name / f"{name}.kicad_pcb"
    ow, oh = outline_mm(pcb)
    writer = PdfWriter()
    pw, ph = landscape(A4)
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for i, (suffix, layers, mirror, cap, actual, bw) in enumerate(VIEWS, 1):
            svg = svg_view(pcb, layers, mirror, bw, tmp, suffix)
            wmm, hmm = svg_size_mm(svg)
            drawing = PdfReader(io.BytesIO(cairosvg.svg2pdf(url=str(svg))))
            dpage = drawing.pages[0]
            dw, dh = float(dpage.mediabox.width), float(dpage.mediabox.height)
            # available area below the caption
            ax, ay, aw, ah = 14 * MM, 14 * MM, pw - 28 * MM, ph - 40 * MM
            s = (wmm * MM / dw) if actual else min(aw / dw, ah / dh)
            ratio = s * dw / (wmm * MM)          # drawing scale relative to real size
            assert not actual or abs(ratio - 1) < 0.002, ratio
            scale_txt = "scale 1:1" if actual else f"scale {ratio:.2f}:1"
            page = caption_page(f"Long-range remote - {desc}", f"{cap}   ({scale_txt}, board outline {ow:.1f} x {oh:.1f} mm)",
                                f"{name}.kicad_pcb  -  layers {layers}  -  page {i} of {len(VIEWS)}"
                                "  -  generated by tools/export_pdfs.py").pages[0]
            tx = ax + (aw - dw * s) / 2
            ty = ay + (ah - dh * s) / 2
            page.merge_transformed_page(dpage, Transformation().scale(s, s).translate(tx, ty))
            writer.add_page(page)
    with open(OUT / f"{name}_pcb.pdf", "wb") as f:
        writer.write(f)


def main():
    OUT.mkdir(exist_ok=True)
    for name, desc in BOARDS.items():
        subprocess.run(["kicad-cli", "sch", "export", "pdf", "-o", str(OUT / f"{name}_schematic.pdf"),
                        str(ROOT / name / f"{name}.kicad_sch")], check=True, stdout=subprocess.DEVNULL)
        board_pdf(name, desc)
        print(f"pdf/{name}_schematic.pdf, pdf/{name}_pcb.pdf")


if __name__ == "__main__":
    main()
