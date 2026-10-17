#!/usr/bin/env python3
"""
3D renders of the two Lite boards, with their components, from KiCad's STEP export.

KiCad 7's kicad-cli can export a board as STEP but can't render it (`pcb render` is KiCad 9+), so this:
  1. writes a scratch copy of each .kicad_pcb with the 3D model paths pointing at a local folder of
     KiCad STEP models (KiCad's packages3D library; only the models these boards use are needed),
  2. runs `kicad-cli pcb export step` on it,
  3. reads the STEP with OpenCascade's XCAF reader (keeps each face's colour), meshes it and renders
     it off-screen with VTK.

Usage:
  tools/render_3d.py MODELS_DIR            MODELS_DIR = a folder laid out like packages3D
                                           (<Lib>.3dshapes/<Model>.step). Fetch the models with
                                           tools/render_3d.py --fetch MODELS_DIR (raw.githubusercontent.com).
Writes previews/<board>_3d_top.png, _3d_bottom.png, _3d_iso.png and _3d_iso_back.png.
"""
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARDS = ["control_lite", "led_head_lite"]
MODEL_RE = re.compile(r'\(model "\$\{KICAD6_3DMODEL_DIR\}/([^"]+)\.wrl"')
RAW = "https://raw.githubusercontent.com/KiCad/kicad-packages3D/master/"


def models_used():
    out = set()
    for b in BOARDS:
        out |= set(MODEL_RE.findall((ROOT / b / f"{b}.kicad_pcb").read_text()))
    return sorted(out)


def fetch(models_dir):
    for m in models_used():
        dst = Path(models_dir) / f"{m}.step"
        if dst.exists():
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        print("fetch", m)
        urllib.request.urlretrieve(RAW + m + ".step", dst)


def export_step(board, models_dir, tmp):
    src = (ROOT / board / f"{board}.kicad_pcb").read_text()
    pcb = Path(tmp) / f"{board}.kicad_pcb"
    pcb.write_text(MODEL_RE.sub(lambda m: f'(model "{Path(models_dir).resolve()}/{m.group(1)}.step"', src))
    step = Path(tmp) / f"{board}.step"
    r = subprocess.run(["kicad-cli", "pcb", "export", "step", "-f", "-o", str(step), str(pcb)],
                       capture_output=True, text=True)
    missing = [l for l in r.stdout.splitlines() if "Could not add" in l]
    if missing:
        sys.exit(f"{board}: models missing:\n" + "\n".join(missing))
    return step


def load_colored_mesh(step):
    """[(vertices Nx3, triangles Mx3, rgb)] per face, colours from the STEP (XCAF)."""
    from OCP.STEPCAFControl import STEPCAFControl_Reader
    from OCP.TDocStd import TDocStd_Document
    from OCP.TCollection import TCollection_ExtendedString
    from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ColorType
    from OCP.TDF import TDF_LabelSequence
    from OCP.Quantity import Quantity_Color
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID
    from OCP.TopoDS import TopoDS
    from OCP.BRep import BRep_Tool
    from OCP.TopLoc import TopLoc_Location
    from OCP.TopAbs import TopAbs_REVERSED

    doc = TDocStd_Document(TCollection_ExtendedString("doc"))
    rd = STEPCAFControl_Reader()
    rd.SetColorMode(True)
    rd.SetNameMode(True)
    rd.ReadFile(str(step))
    rd.Transfer(doc)
    st = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    ct = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())

    def col_of(shape, default):
        c = Quantity_Color()
        for t in (XCAFDoc_ColorType.XCAFDoc_ColorSurf, XCAFDoc_ColorType.XCAFDoc_ColorGen):
            if ct.GetColor(shape, t, c):
                return (c.Red(), c.Green(), c.Blue())
        return default

    out = []

    def walk(label, loc, inherited):
        shape = st.GetShape_s(label)
        if st.IsReference_s(label):
            ref = label.__class__()
            st.GetReferredShape_s(label, ref)
            l2 = loc.Multiplied(st.GetLocation_s(label))
            walk(ref, l2, col_of(shape, inherited))
            return
        if st.IsAssembly_s(label):
            kids = TDF_LabelSequence()
            st.GetComponents_s(label, kids)
            base = col_of(shape, inherited)
            for i in range(1, kids.Length() + 1):
                walk(kids.Value(i), loc, base)
            return
        base = col_of(shape, inherited)
        s = shape.Moved(loc)
        BRepMesh_IncrementalMesh(s, 0.05, False, 0.3, True)
        ex = TopExp_Explorer(s, TopAbs_FACE)
        while ex.More():
            f = TopoDS.Face_s(ex.Current())
            fcol = col_of(ex.Current(), None)
            if fcol is None:                       # colours set on the un-moved sub-shape
                fcol = col_of(f.Located(TopLoc_Location()), base)
            L = TopLoc_Location()
            tri = BRep_Tool.Triangulation_s(f, L)
            if tri is not None:
                tr = L.Transformation()
                v = [tri.Node(i).Transformed(tr) for i in range(1, tri.NbNodes() + 1)]
                verts = [(p.X(), p.Y(), p.Z()) for p in v]
                tris = []
                rev = f.Orientation() == TopAbs_REVERSED
                for i in range(1, tri.NbTriangles() + 1):
                    a, b, c = tri.Triangle(i).Get()
                    tris.append((a - 1, c - 1, b - 1) if rev else (a - 1, b - 1, c - 1))
                out.append((verts, tris, fcol or (0.6, 0.6, 0.6)))
            ex.Next()

    roots = TDF_LabelSequence()
    st.GetFreeShapes(roots)
    for i in range(1, roots.Length() + 1):
        walk(roots.Value(i), TopLoc_Location(), (0.6, 0.6, 0.6))
    return out


def render(parts, png, view, title, size=(1600, 1100)):
    import vtk
    pts, polys, cols = vtk.vtkPoints(), vtk.vtkCellArray(), vtk.vtkUnsignedCharArray()
    cols.SetNumberOfComponents(3)
    for verts, tris, c in parts:
        base = pts.GetNumberOfPoints()
        for p in verts:
            pts.InsertNextPoint(*p)
        rgb = [int(255 * min(1, max(0, x))) for x in c]
        for t in tris:
            polys.InsertNextCell(3, [base + t[0], base + t[1], base + t[2]])
            cols.InsertNextTuple3(*rgb)
    pd = vtk.vtkPolyData()
    pd.SetPoints(pts)
    pd.SetPolys(polys)
    pd.GetCellData().SetScalars(cols)
    nrm = vtk.vtkPolyDataNormals()
    nrm.SetInputData(pd)
    nrm.SetFeatureAngle(35)
    nrm.SplittingOn()
    m = vtk.vtkPolyDataMapper()
    m.SetInputConnection(nrm.GetOutputPort())
    a = vtk.vtkActor()
    a.SetMapper(m)
    a.GetProperty().SetSpecular(0.25)
    a.GetProperty().SetSpecularPower(30)
    ren = vtk.vtkRenderer()
    ren.AddActor(a)
    ren.SetBackground(1, 1, 1)
    ren.SetBackground2(0.88, 0.9, 0.93)
    ren.GradientBackgroundOn()
    b = pd.GetBounds()
    c = ((b[0] + b[1]) / 2, (b[2] + b[3]) / 2, (b[4] + b[5]) / 2)
    d = max(b[1] - b[0], b[3] - b[2]) * 1.6
    cam = ren.GetActiveCamera()
    cam.SetFocalPoint(*c)
    pos = {"top": (c[0], c[1], c[2] + 3 * d), "bottom": (c[0], c[1], c[2] - 3 * d),
           "iso": (c[0] + 0.9 * d, c[1] - 1.3 * d, c[2] + 1.2 * d),
           "iso_back": (c[0] + 0.9 * d, c[1] - 1.3 * d, c[2] - 1.2 * d)}[view]
    cam.SetPosition(*pos)
    cam.SetViewUp(*({"top": (0, 1, 0), "bottom": (0, 1, 0), "iso": (0, 0, 1), "iso_back": (0, 0, -1)}[view]))
    if view == "bottom":                                   # look from below, mirrored like flipping the board over
        cam.SetViewUp(0, 1, 0)
    ren.ResetCamera()
    cam.Zoom(1.25)
    if view in ("top", "bottom"):
        cam.ParallelProjectionOn()
        ren.ResetCamera()
        cam.Zoom(1.3)
    lk = vtk.vtkLightKit()
    lk.AddLightsToRenderer(ren)
    win = vtk.vtkRenderWindow()
    win.SetOffScreenRendering(1)
    win.AddRenderer(ren)
    win.SetSize(*size)
    win.SetMultiSamples(8)
    txt = vtk.vtkTextActor()
    txt.SetInput(title)
    txt.GetTextProperty().SetFontSize(28)
    txt.GetTextProperty().SetColor(0.15, 0.15, 0.18)
    txt.SetDisplayPosition(24, size[1] - 50)
    ren.AddViewProp(txt)
    win.Render()
    w2i = vtk.vtkWindowToImageFilter()
    w2i.SetInput(win)
    w2i.SetInputBufferTypeToRGB()
    w2i.Update()
    wr = vtk.vtkPNGWriter()
    wr.SetFileName(str(png))
    wr.SetInputConnection(w2i.GetOutputPort())
    wr.Write()
    print("wrote", png)


NAMES = {"control_lite": "Control board", "led_head_lite": "LED head"}


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--fetch":
        fetch(sys.argv[2])
        return
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    models = sys.argv[1]
    out = ROOT / "previews"
    with tempfile.TemporaryDirectory() as tmp:
        for b in BOARDS:
            parts = load_colored_mesh(export_step(b, models, tmp))
            n = NAMES[b]
            render(parts, out / f"{b}_3d_iso.png", "iso", f"{n} - 3D view, front")
            render(parts, out / f"{b}_3d_iso_back.png", "iso_back", f"{n} - 3D view, back")
            render(parts, out / f"{b}_3d_top.png", "top", f"{n} - front (component side)")
            render(parts, out / f"{b}_3d_bottom.png", "bottom", f"{n} - back")


if __name__ == "__main__":
    main()
