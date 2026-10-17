#!/usr/bin/env python3
"""
Fit check against Waveshare's own 3D model of the ESP32-S3-RLCD-4.2.

    python3 tools/waveshare_fit.py path/to/ESP32-S3-rlcd-4_2_20251208.stp

(The STEP comes in Waveshare's "3D file" download for the board; it is not redistributed here.)

It places every solid of the model in the housing (glass top against the lid's underside, image 'up'
towards the LED head, USB-C and TF in the right wall) and:
  1. intersects each one with the printed parts (lid, body, bezel, door): any overlap is a clash;
  2. checks that the lid's four snap hooks land on the display's back case;
  3. prints where the USB-C receptacle and TF socket sit, to compare with the wall openings.
The model's fold-out stand is left out (folded, it lies flush in the back case).

Model frame (from the file): y up through the glass (glass top at y = +0.75), the 92.5 mm side along z,
the 70.1 mm side along x. Housing front view: xf = model z - 1.6, yf = model x + 0.5 (origin = the back
case's centre); housing x = xf, z = Z_DISP_C - yf, y = glass top - depth below it.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))

import cadquery as cq                                      # noqa: E402
import housing_base as H                                   # noqa: E402
import housing_lite as L                                   # noqa: E402
from OCP.STEPCAFControl import STEPCAFControl_Reader       # noqa: E402
from OCP.TDocStd import TDocStd_Document                   # noqa: E402
from OCP.TCollection import TCollection_ExtendedString     # noqa: E402
from OCP.XCAFDoc import XCAFDoc_DocumentTool               # noqa: E402
from OCP.TDF import TDF_LabelSequence, TDF_Label           # noqa: E402
from OCP.TDataStd import TDataStd_Name                     # noqa: E402
from OCP.TopLoc import TopLoc_Location                     # noqa: E402
from OCP.Bnd import Bnd_Box                                # noqa: E402
from OCP.BRepBndLib import BRepBndLib                      # noqa: E402


def read_named_solids(path):
    """Every leaf shape of the STEP assembly as (path of names, shape) in model coordinates."""
    doc = TDocStd_Document(TCollection_ExtendedString("doc"))
    r = STEPCAFControl_Reader()
    r.SetNameMode(True)
    if r.ReadFile(str(path)) != 1:
        sys.exit(f"cannot read {path}")
    r.Transfer(doc)
    st = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())

    def name(lab):
        n = TDataStd_Name()
        return n.Get().ToExtString() if lab.FindAttribute(TDataStd_Name.GetID_s(), n) else "?"

    out = []

    def walk(lab, loc, path):
        if st.IsReference_s(lab):
            ref = TDF_Label()
            st.GetReferredShape_s(lab, ref)
            walk(ref, loc.Multiplied(st.GetLocation_s(lab)), path + [name(lab)])
        elif st.IsAssembly_s(lab):
            kids = TDF_LabelSequence()
            st.GetComponents_s(lab, kids)
            for i in range(1, kids.Length() + 1):
                walk(kids.Value(i), loc, path + [name(lab)])
        else:
            shp = st.GetShape_s(lab).Moved(loc)
            b = Bnd_Box()
            BRepBndLib.Add_s(shp, b, True)
            if not b.IsVoid():
                out.append(("/".join(path + [name(lab)]), cq.Shape.cast(shp)))

    roots = TDF_LabelSequence()
    st.GetFreeShapes(roots)
    for i in range(1, roots.Length() + 1):
        walk(roots.Value(i), TopLoc_Location(), [])
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    yg, zc = H.Y_GLASS + L.DY, H.Z_DISP_C

    def place(s):   # model (x, y, z) -> housing (z - 1.6, y - 0.75 + glass, Z_DISP_C - x - 0.5)
        return s.rotate((0, 0, 0), (0, 1, 0), 90).translate((-1.6, yg - 0.75, zc - 0.5))

    ws = [(n, place(s)) for n, s in read_named_solids(sys.argv[1]) if "BRACKET" not in n]
    print(f"{len(ws)} solids from the Waveshare model")
    parts = {"lid": L.make_lid(), "body": L.make_body(), "bezel": L.make_bezel(), "door": L.make_door()}
    hits = []
    for pn, p in parts.items():
        pb = p.val().BoundingBox()
        for n, s in ws:
            b = s.BoundingBox()
            if (b.xmax < pb.xmin or b.xmin > pb.xmax or b.ymax < pb.ymin or b.ymin > pb.ymax
                    or b.zmax < pb.zmin or b.zmin > pb.zmax):
                continue
            try:
                v = p.val().intersect(s).Volume()
            except Exception:
                v = -1.0
            if v > 0.001 or v < 0:
                hits.append(f"  CLASH {pn} x {n.split('/')[-1]}: {v:.3f} mm^3")
    print("Waveshare model vs the printed parts:", "no clashes" if not hits else "")
    for h in hits:
        print(h)

    base = next(s for n, s in ws if n.endswith("ESP32-S3-RLCD-4_2-BASE"))
    yb = H.Y_DISP_BACK + L.DY
    for hx in H.HOOK_X:
        for sz in (1, -1):
            z_in = zc + sz * H.DISP_H / 2
            z0 = z_in - H.HOOK_DEPTH if sz > 0 else z_in
            probe = cq.Solid.makeBox(H.HOOK_W, 0.6, H.HOOK_DEPTH, pnt=cq.Vector(hx - H.HOOK_W / 2, yb, z0))
            pct = 100 * probe.intersect(base).Volume() / (H.HOOK_W * 0.6 * H.HOOK_DEPTH)
            print(f"snap hook x {hx:+.0f}, {'rear' if sz > 0 else 'front'} edge: back case under the catch {pct:.0f} %")

    for key, what in (("SOLID-610343", "USB-C receptacle"), ("SOLID-296080", "TF socket")):
        bb = next(s for n, s in ws if n.endswith(key)).BoundingBox()
        print(f"{what:17s} y {bb.ymin:6.2f}..{bb.ymax:6.2f}  z {bb.zmin:6.1f}..{bb.zmax:6.1f}  (right wall at x {bb.xmax:.1f})")
    for yf, depth, along, tall, what in ((H.USB_YF, H.USB_DEPTH, H.USB_ALONG, H.USB_TALL, "USB-C opening"),
                                         (H.TF_YF, H.TF_DEPTH, H.TF_ALONG, H.TF_TALL, "TF opening")):
        y, z = yg - depth, zc - yf
        print(f"{what:17s} y {y - tall / 2:6.2f}..{y + tall / 2:6.2f}  z {z - along / 2:6.1f}..{z + along / 2:6.1f}")


if __name__ == "__main__":
    main()
