#!/usr/bin/env python3
"""
kigen - small helpers shared by the three long-range remote board generators.

  Sch      writes a KiCad 7 schematic (.kicad_sch). Symbols are copied verbatim from KiCad's
           stock libraries and pin positions are parsed from
           the library text, so every pin can be stubbed out to a net label / power symbol.
  Board    places footprints from the exported netlist (UUID-linked to the schematic), adds
           tracks, vias, zones, keepouts, text and a board outline.
  route()  round-trips the board through Freerouting (Specctra DSN -> SES) for the
           irregular boards. Net classes (track widths) are written into the DSN.
  drc()    runs KiCad's DRC and returns the violation / unconnected counts.
  render() front/back SVG -> PNG previews.

Deterministic UUIDs (uuid5 of project + counter) keep schematic <-> PCB links stable when
the generators are re-run.
"""
import json
import math
import re
import subprocess
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STOCK_SYM = Path("/usr/share/kicad/symbols")
STOCK_FP = Path("/usr/share/kicad/footprints")
FREEROUTING = ROOT / "tools" / "bin" / "freerouting.jar"   # Freerouting v2.1.0 (github.com/freerouting/freerouting releases)

G = 2.54


def fmt(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


# =============================================================================== schematic
def _block(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise ValueError("unbalanced")


_SYM_CACHE = {}


def lib_symbol(lib_id):
    """(symbol "lib:name" ...) block, renamed to the full lib_id, plus parsed pins."""
    if lib_id in _SYM_CACHE:
        return _SYM_CACHE[lib_id]
    lib, name = lib_id.split(":")
    path = STOCK_SYM / f"{lib}.kicad_sym"
    text = path.read_text()
    start = text.find(f'(symbol "{name}"')
    if start < 0:
        raise KeyError(lib_id)
    blk = _block(text, start)
    if "(extends" in blk[:400]:
        raise ValueError(f"{lib_id} is derived; use its parent")
    pins = {}
    for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)[\s\S]*?\(number "([^"]*)"', blk):
        pins.setdefault(m.group(4), (float(m.group(1)), float(m.group(2)), int(m.group(3))))
    blk = blk.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)
    _SYM_CACHE[lib_id] = (blk, pins)
    return _SYM_CACHE[lib_id]


def _xf(rot, mirror_y, px, py):
    if mirror_y:
        px = -px
    a = math.radians(rot)
    return (px * math.cos(a) - py * math.sin(a), px * math.sin(a) + py * math.cos(a))


POWER_NETS = {"+8V", "+5V", "+3V3", "+BATT", "GND"}


class Sch:
    def __init__(self, project, title, rev, comment="", paper="A3", date="2026-10-03"):
        self.project, self.title, self.rev, self.comment, self.paper = project, title, rev, comment, paper
        self.date = date
        self.root = str(uuid.uuid5(uuid.NAMESPACE_URL, f"{project}/root"))
        self.n = 0
        self.items = []
        self.used = []
        self.pwr_n = 0

    def uid(self, tag=""):
        self.n += 1
        return str(uuid.uuid5(uuid.NAMESPACE_URL, f"{self.project}/revD/{tag}/{self.n}"))

    # -- geometry -------------------------------------------------------------
    def pin_xy(self, lib_id, at, rot=0, mirror_y=False, pin="1"):
        px, py, _ = lib_symbol(lib_id)[1][pin]
        rx, ry = _xf(rot, mirror_y, px, py)
        return (round(at[0] + rx, 4), round(at[1] - ry, 4))

    def pin_dir(self, lib_id, rot=0, mirror_y=False, pin="1"):
        """Unit vector (world, Y down) pointing away from the body at this pin."""
        ang = lib_symbol(lib_id)[1][pin][2]
        dx, dy = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        rx, ry = _xf(rot, mirror_y, dx, dy)
        return (round(-rx), round(ry))

    # -- primitives -----------------------------------------------------------
    @staticmethod
    def _prop(name, value, xy, rot=0, hide=False, justify=None, size=1.27):
        eff = f"(effects (font (size {size} {size}))"
        if justify:
            eff += f" (justify {justify})"
        eff += " hide)" if hide else ")"
        value = value.replace('"', '\\"')
        return f'    (property "{name}" "{value}" (at {fmt(xy[0])} {fmt(xy[1])} {rot}) {eff})'

    def symbol(self, lib_id, ref, value, at, rot=0, mirror_y=False, footprint="", datasheet="",
               ref_xy=None, val_xy=None, hide_ref=False, hide_val=False, in_bom=True, on_board=True,
               props=(), justify=None):
        if lib_id not in self.used:
            self.used.append(lib_id)
        sid = self.uid("sym")
        ref_xy = ref_xy or (at[0], at[1] - 3.81)
        val_xy = val_xy or (at[0], at[1] + 3.81)
        mirror = " (mirror y)" if mirror_y else ""
        L = [f'  (symbol (lib_id "{lib_id}") (at {fmt(at[0])} {fmt(at[1])} {rot}){mirror} (unit 1)',
             f'    (in_bom {"yes" if in_bom else "no"}) (on_board {"yes" if on_board else "no"}) (dnp no)',
             f'    (uuid {sid})',
             # field angles are stored relative to the symbol: matching the symbol rotation keeps the
             # reference / value text horizontal on rotated parts
             self._prop("Reference", ref, ref_xy, rot=rot % 180, hide=hide_ref, justify=justify),
             self._prop("Value", value, val_xy, rot=rot % 180, hide=hide_val, justify=justify),
             self._prop("Footprint", footprint, at, hide=True),
             self._prop("Datasheet", datasheet or "~", at, hide=True)]
        for k, v in props:
            L.append(self._prop(k, v, at, hide=True))
        for pnum in lib_symbol(lib_id)[1]:
            L.append(f'    (pin "{pnum}" (uuid {self.uid("pin")}))')
        L.append(f'    (instances (project "{self.project}" (path "/{self.root}" (reference "{ref}") (unit 1))))')
        L.append("  )")
        self.items.append("\n".join(L))
        return sid

    def wire(self, a, b):
        self.items.append(f"  (wire (pts (xy {fmt(a[0])} {fmt(a[1])}) (xy {fmt(b[0])} {fmt(b[1])}))"
                          f" (stroke (width 0) (type default)) (uuid {self.uid('w')}))")

    def junction(self, p):
        self.items.append(f"  (junction (at {fmt(p[0])} {fmt(p[1])}) (diameter 0) (color 0 0 0 0) (uuid {self.uid('j')}))")

    def no_connect(self, p):
        self.items.append(f"  (no_connect (at {fmt(p[0])} {fmt(p[1])}) (uuid {self.uid('nc')}))")

    def label(self, name, p, d=(1, 0)):
        rot, just = {(1, 0): (0, "left bottom"), (-1, 0): (180, "right bottom"),
                     (0, -1): (90, "left bottom"), (0, 1): (270, "right bottom")}[d]
        self.items.append(f'  (label "{name}" (at {fmt(p[0])} {fmt(p[1])} {rot}) (fields_autoplaced)'
                          f" (effects (font (size 1.27 1.27)) (justify {just})) (uuid {self.uid('l')}))")

    def text(self, s, p, size=1.27, bold=False):
        s = s.replace('"', '\\"')
        b = " bold" if bold else ""
        self.items.append(f'  (text "{s}" (at {fmt(p[0])} {fmt(p[1])} 0)'
                          f" (effects (font (size {size} {size}){b}) (justify left bottom)) (uuid {self.uid('t')}))")

    def power(self, kind, p):
        self.pwr_n += 1
        if kind == "FLAG":
            self.symbol("power:PWR_FLAG", f"#FLG0{self.pwr_n:02d}", "PWR_FLAG", p, ref_xy=p,
                        val_xy=(p[0], p[1] - 3.81), hide_ref=True, in_bom=False, on_board=False)
            return
        vxy = (p[0], p[1] + 3.81) if kind == "GND" else (p[0], p[1] - 3.556)
        self.symbol(f"power:{kind}", f"#PWR0{self.pwr_n:02d}", kind, p, ref_xy=p, val_xy=vxy,
                    hide_ref=True, in_bom=False, on_board=False)

    def flag(self, net, p):
        """PWR_FLAG on a net (power symbol or label) so ERC knows it is driven."""
        self.power("FLAG", p)
        e = (p[0], p[1] + 2 * G)
        self.wire(p, e)
        if net in POWER_NETS:
            self.power(net, e)
        else:
            self.label(net, e, (1, 0))

    def stub(self, lib_id, at, pin, net, rot=0, mirror_y=False, length=2 * G):
        p = self.pin_xy(lib_id, at, rot, mirror_y, pin)
        if net is None:
            self.no_connect(p)
            return
        d = self.pin_dir(lib_id, rot, mirror_y, pin)
        e = (round(p[0] + d[0] * length, 4), round(p[1] + d[1] * length, 4))
        self.wire(p, e)
        if net in POWER_NETS:
            self.power(net, e)
        else:
            self.label(net, e, d if d != (0, 0) else (1, 0))

    def part(self, lib_id, ref, value, at, nets, rot=0, mirror_y=False, footprint="", datasheet="",
             props=(), field_dx=3.81, stub_len=2 * G, **kw):
        """Place a symbol and stub every listed pin to its net (None = no-connect flag)."""
        sid = self.symbol(lib_id, ref, value, at, rot=rot, mirror_y=mirror_y, footprint=footprint,
                          datasheet=datasheet, ref_xy=kw.pop("ref_xy", (at[0] + field_dx, at[1] - 1.27)),
                          val_xy=kw.pop("val_xy", (at[0] + field_dx, at[1] + 1.27)),
                          justify=kw.pop("justify", "left"), props=props, **kw)
        for pin, net in nets.items():
            self.stub(lib_id, at, pin, net, rot, mirror_y, length=stub_len)
        return sid

    def tie(self, lib_id, at, pin_list, rot=0, mirror_y=False):
        """Wire a row/column of duplicated pins together (pin endpoints in a straight line)."""
        pts = [self.pin_xy(lib_id, at, rot, mirror_y, p) for p in pin_list]
        for a, b in zip(pts, pts[1:]):
            self.wire(a, b)

    def write(self, path):
        libs = "\n".join("    " + lib_symbol(l)[0].replace("\n", "\n    ") for l in self.used)
        out = ["(kicad_sch (version 20230121) (generator eeschema)", "",
               f"  (uuid {self.root})", "", f'  (paper "{self.paper}")', "",
               "  (title_block", f'    (title "{self.title}")', f'    (date "{self.date}")',
               f'    (rev "{self.rev}")', '    (company "Luke Jamieson")',
               f'    (comment 1 "{self.comment}")', "  )", "",
               "  (lib_symbols", libs, "  )", "", "\n".join(self.items), "",
               "  (sheet_instances", '    (path "/" (page "1"))', "  )", ")", ""]
        Path(path).write_text("\n".join(out))


# =============================================================================== project files
def write_project_files(board_dir: Path, project: str):
    board_dir.mkdir(parents=True, exist_ok=True)
    (board_dir / "fp-lib-table").write_text(
        '(fp_lib_table\n)\n')                       # stock KiCad libraries only
    (board_dir / "sym-lib-table").write_text(
        '(sym_lib_table\n)\n')


def tune_project(pro: Path):
    """Rule severities that make no sense for a headless build (no library table to diff the
    footprints against). Everything else stays at KiCad's defaults."""
    pro = Path(pro)
    if not pro.exists():
        return
    d = json.loads(pro.read_text())
    rs = d.setdefault("board", {}).setdefault("design_settings", {}).setdefault("rule_severities", {})
    rs.update({"lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore"})
    pro.write_text(json.dumps(d, indent=2))


def export_netlist(sch: Path, net: Path):
    net.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "-o", str(net), str(sch)],
                   check=True, capture_output=True)


def parse_netlist(path):
    s = Path(path).read_text()
    comps = {}
    for m in re.finditer(r'\(comp \(ref "([^"]+)"\)\s*\(value "([^"]*)"\)\s*\(footprint "([^"]*)"\)'
                         r'[\s\S]*?\(tstamps "([0-9a-f-]{36})"\)\)', s):
        comps[m.group(1)] = dict(value=m.group(2), footprint=m.group(3), uuid=m.group(4))
    pad_net = {}
    for m in re.finditer(r'\(net \(code "\d+"\) \(name "([^"]*)"\)((?:\s*\(node [^\n]*\))*)', s):
        for ref, pin in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)):
            pad_net[(ref, pin)] = m.group(1)
    return comps, pad_net


def nets_of(pad_net):
    nets = {}
    for (ref, pin), n in pad_net.items():
        nets.setdefault(n, set()).add(f"{ref}.{pin}")
    return nets


# =============================================================================== pcb
import pcbnew  # noqa: E402  (after the schematic helpers so they work without pcbnew)

MM = pcbnew.FromMM

# Silkscreen minimums for a clear print at JLCPCB (their guidance: text >= 1.0 mm high, lines >= 0.15 mm).
# Our own text is made at least this size, and fill_and_save() widens thinner library footprint lines.
SILK_MIN_TEXT = 1.0
SILK_MIN_LINE = 0.15


def widen_silk(board):
    """Bring library footprints' silkscreen up to SILK_MIN_LINE / SILK_MIN_TEXT (KiCad's own are 0.12 mm)."""
    silk = (pcbnew.F_SilkS, pcbnew.B_SilkS)
    for fp in board.GetFootprints():
        for g in fp.GraphicalItems():
            if g.GetLayer() not in silk:
                continue
            if isinstance(g, pcbnew.FP_TEXT):
                if pcbnew.ToMM(g.GetTextHeight()) < SILK_MIN_TEXT:
                    g.SetTextSize(pcbnew.VECTOR2I(MM(SILK_MIN_TEXT), MM(SILK_MIN_TEXT)))
                if pcbnew.ToMM(g.GetTextThickness()) < SILK_MIN_LINE:
                    g.SetTextThickness(MM(SILK_MIN_LINE))
            elif pcbnew.ToMM(g.GetWidth()) < SILK_MIN_LINE:
                g.SetWidth(MM(SILK_MIN_LINE))
        for t in (fp.Reference(), fp.Value()):
            if t.GetLayer() in silk and t.IsVisible():
                if pcbnew.ToMM(t.GetTextHeight()) < SILK_MIN_TEXT:
                    t.SetTextSize(pcbnew.VECTOR2I(MM(SILK_MIN_TEXT), MM(SILK_MIN_TEXT)))
                if pcbnew.ToMM(t.GetTextThickness()) < SILK_MIN_LINE:
                    t.SetTextThickness(MM(SILK_MIN_LINE))


def P(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


def circle_pts(cx, cy, r, n=72, a0=0.0):
    return [(cx + r * math.cos(a0 + 2 * math.pi * i / n), cy + r * math.sin(a0 + 2 * math.pi * i / n))
            for i in range(n)]


def rrect_pts(cx, cy, half, r, n=8, half_y=None):
    """Rounded-square polygon (absolute coords) - zone outlines for the boards. half_y: a rectangle
    (half-width `half`, half-height `half_y`) instead of a square."""
    hy = half if half_y is None else half_y
    pts = []
    for (ox, oy, a0) in ((half - r, -hy + r, -90), (half - r, hy - r, 0), (-half + r, hy - r, 90),
                         (-half + r, -hy + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + ox + r * math.cos(a), cy + oy + r * math.sin(a)))
    return pts


class Board:
    def __init__(self, out: Path, netlist: Path):
        self.out = Path(out)
        self.comps, self.pad_net = parse_netlist(netlist)
        self.board = pcbnew.NewBoard(str(self.out))
        self.nets, self.fps = {}, {}
        for name in sorted(set(self.pad_net.values())):
            ni = pcbnew.NETINFO_ITEM(self.board, name)
            self.board.Add(ni)
            self.nets[name] = ni
        ds = self.board.GetDesignSettings()
        ds.SetCopperLayerCount(2)
        ds.m_TrackMinWidth = MM(0.2)
        ds.m_MinClearance = MM(0.2)
        ds.m_ViasMinSize = MM(0.6)
        ds.m_MinThroughDrill = MM(0.3)
        ds.m_HoleClearance = MM(0.25)
        ds.m_CopperEdgeClearance = MM(0.4)
        ds.SetBoardThickness(MM(1.6))
        nc = ds.m_NetSettings.m_DefaultNetClass
        nc.SetClearance(MM(0.25)); nc.SetTrackWidth(MM(0.4))
        nc.SetViaDiameter(MM(0.9)); nc.SetViaDrill(MM(0.45))

    # -- footprints -------------------------------------------------------------
    def _load(self, ref):
        c = self.comps[ref]
        lib, name = c["footprint"].split(":")
        path = STOCK_FP / f"{lib}.pretty"
        fp = pcbnew.FootprintLoad(str(path), name)
        assert fp is not None, c["footprint"]
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetPath(pcbnew.KIID_PATH(f"/{c['uuid']}"))
        return fp

    def _finish(self, ref, fp, hide_ref):
        for pad in fp.Pads():
            n = self.pad_net.get((ref, pad.GetNumber()))
            if n:
                pad.SetNet(self.nets[n])
        if hide_ref:
            fp.Reference().SetVisible(False)
        self.fps[ref] = fp
        return fp

    def place(self, ref, x, y, rot=0.0, back=False, hide_ref=False):
        fp = self._load(ref)
        self.board.Add(fp)
        fp.SetPosition(P(x, y))
        fp.SetOrientationDegrees(rot)
        if back:
            fp.Flip(fp.GetPosition(), True)
        return self._finish(ref, fp, hide_ref)

    def place_pad(self, ref, pad, xy, rot=0.0, back=False, hide_ref=False):
        """Place so that pad `pad` lands exactly on xy (with the given rotation / side)."""
        fp = self._load(ref)
        self.board.Add(fp)
        fp.SetPosition(P(0, 0))
        fp.SetOrientationDegrees(rot)
        if back:
            fp.Flip(fp.GetPosition(), True)
        q = self._padpos(fp, pad)
        fp.SetPosition(P(xy[0] - q[0], xy[1] - q[1]))
        return self._finish(ref, fp, hide_ref)

    def place_pads(self, ref, pad1, pad2, back=False, hide_ref=False):
        """Place a 2-pad part so pad '1' lands on pad1 and pad '2' on pad2."""
        fp = self._load(ref)
        self.board.Add(fp)
        for rot in (0, 90, 180, 270):
            fp.SetPosition(P(0, 0))
            fp.SetOrientationDegrees(rot)
            if back and not fp.IsFlipped():
                fp.Flip(fp.GetPosition(), True)
                fp.SetOrientationDegrees(rot)
            q1, q2 = self._padpos(fp, "1"), self._padpos(fp, "2")
            t = (pad1[0] - q1[0], pad1[1] - q1[1])
            if abs(q2[0] + t[0] - pad2[0]) < 0.05 and abs(q2[1] + t[1] - pad2[1]) < 0.05:
                fp.SetPosition(P(*t))
                return self._finish(ref, fp, hide_ref)
        raise ValueError(f"{ref}: pads cannot land on {pad1} / {pad2}")

    @staticmethod
    def _padpos(fp, num):
        for p in fp.Pads():
            if p.GetNumber() == num:
                return (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
        raise KeyError(num)

    def pad_xy(self, ref, num):
        x, y = self._padpos(self.fps[ref], num)
        return (round(x, 4), round(y, 4))

    def netof(self, ref, num):
        return self.pad_net[(ref, num)]

    def ref_text(self, ref, xy, size=1.0, rot=0, layer=None):
        size = max(size, SILK_MIN_TEXT)
        t = self.fps[ref].Reference()
        t.SetPosition(P(*xy))
        t.SetTextAngleDegrees(rot)
        t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
        t.SetTextThickness(MM(max(SILK_MIN_LINE, size * 0.15)))
        if layer is not None:
            t.SetLayer(layer)

    # -- copper -------------------------------------------------------------------
    def track(self, pts, width, layer, net):
        for a, b in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(self.board)
            t.SetStart(P(*a)); t.SetEnd(P(*b))
            t.SetWidth(MM(width)); t.SetLayer(layer); t.SetNet(self.nets[net])
            t.SetLocked(True)               # pre-routed: exported as protected, so the autorouter keeps it
            self.board.Add(t)

    def via(self, xy, net, size=0.9, drill=0.45):
        v = pcbnew.PCB_VIA(self.board)
        v.SetPosition(P(*xy)); v.SetWidth(MM(size)); v.SetDrill(MM(drill))
        v.SetNet(self.nets[net])
        v.SetLocked(True)
        self.board.Add(v)

    def zone(self, net, layer, pts, priority=0, clearance=0.3, full=False, min_w=0.25, name=""):
        z = pcbnew.ZONE(self.board)
        z.SetLayer(layer)
        z.SetNet(self.nets[net])
        z.SetAssignedPriority(priority)
        z.SetLocalClearance(MM(clearance))
        z.SetMinThickness(MM(min_w))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if full else pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(MM(0.4))
        z.SetThermalReliefSpokeWidth(MM(0.7))
        if name:
            z.SetZoneName(name)
        ol = z.Outline(); ol.NewOutline()
        for (x, y) in pts:
            ol.Append(MM(x), MM(y))
        self.board.Add(z)
        return z

    def keepout(self, pts, tracks=False, vias=False):
        z = pcbnew.ZONE(self.board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowTracks(tracks); z.SetDoNotAllowVias(vias)
        z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu)
        z.SetLayerSet(ls)
        ol = z.Outline(); ol.NewOutline()
        for (x, y) in pts:
            ol.Append(MM(x), MM(y))
        self.board.Add(z)

    def text(self, s, x, y, layer, size=1.0, mirror=False, rot=0):
        size = max(size, SILK_MIN_TEXT)
        t = pcbnew.PCB_TEXT(self.board)
        t.SetText(s); t.SetPosition(P(x, y)); t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
        t.SetTextThickness(MM(max(SILK_MIN_LINE, size * 0.15))); t.SetMirrored(mirror)
        t.SetTextAngleDegrees(rot)
        self.board.Add(t)

    def outline_circle(self, cx, cy, r):
        s = pcbnew.PCB_SHAPE(self.board)
        s.SetShape(pcbnew.SHAPE_T_CIRCLE)
        s.SetCenter(P(cx, cy)); s.SetEnd(P(cx + r, cy))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
        self.board.Add(s)

    def outline_poly(self, pts):
        for a, b in zip(pts, pts[1:] + pts[:1]):
            s = pcbnew.PCB_SHAPE(self.board)
            s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetStart(P(*a)); s.SetEnd(P(*b))
            s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
            self.board.Add(s)

    def outline_roundrect(self, x0, y0, x1, y1, r):
        """Rectangle with rounded corners (arcs) on Edge.Cuts."""
        segs = [((x0 + r, y0), (x1 - r, y0)), ((x1, y0 + r), (x1, y1 - r)),
                ((x1 - r, y1), (x0 + r, y1)), ((x0, y1 - r), (x0, y0 + r))]
        for a, b in segs:
            s = pcbnew.PCB_SHAPE(self.board)
            s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetStart(P(*a)); s.SetEnd(P(*b))
            s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
            self.board.Add(s)
        for (cx, cy, a0) in ((x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90)):
            s = pcbnew.PCB_SHAPE(self.board)
            s.SetShape(pcbnew.SHAPE_T_ARC)
            a = math.radians(a0)
            s.SetCenter(P(cx, cy))
            s.SetStart(P(cx + r * math.cos(a), cy + r * math.sin(a)))
            s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T), True)
            s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
            self.board.Add(s)

    # -- output -------------------------------------------------------------------
    def fill_and_save(self):
        widen_silk(self.board)
        keep_texts_inside(self.board)
        filler = pcbnew.ZONE_FILLER(self.board)
        filler.Fill(self.board.Zones())
        self.board.BuildConnectivity()
        pcbnew.SaveBoard(str(self.out), self.board)
        tune_project(self.out.with_suffix(".kicad_pro"))


def keep_texts_inside(board, margin=0.3):
    """Nudge footprint texts (references, values, fab labels - shown or hidden) that stick out past
    the board outline back inside it, so nothing on any layer extends beyond the board: the
    board's bounding box in every export (SVG/PDF/3D, fab drawings) is then the outline itself."""
    e = board.GetBoardEdgesBoundingBox()
    m = MM(margin)
    L, R, T, B = e.GetLeft() + m, e.GetRight() - m, e.GetTop() + m, e.GetBottom() - m
    moved = 0
    for fp in board.GetFootprints():
        texts = [fp.Reference(), fp.Value()] + [g for g in fp.GraphicalItems() if g.GetClass() == "FP_TEXT"]
        for t in texts:
            bb = t.GetBoundingBox()
            dx = (L - bb.GetLeft()) if bb.GetLeft() < L else (R - bb.GetRight()) if bb.GetRight() > R else 0
            dy = (T - bb.GetTop()) if bb.GetTop() < T else (B - bb.GetBottom()) if bb.GetBottom() > B else 0
            if dx or dy:
                t.Move(pcbnew.VECTOR2I(int(dx), int(dy)))
                moved += 1
    return moved


# =============================================================================== autorouting
def _sexp(text):
    """Tiny S-expression parser (strings stay quoted-free)."""
    toks = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)
    stack, cur = [], []
    for t in toks:
        if t == "(":
            stack.append(cur); cur = []
        elif t == ")":
            done = cur; cur = stack.pop(); cur.append(done)
        else:
            cur.append(t[1:-1] if t.startswith('"') else t)
    return cur[0]


def import_ses(board, ses_path):
    """
    Apply a Freerouting .ses (network_out wires + vias) to `board`. pcbnew.ImportSpecctraSES only
    works inside the GUI in KiCad 7, so this does the same job headless. SES units: (resolution um N)
    -> 1/N um; Specctra Y axis points up, KiCad's down.
    """
    tree = _sexp(Path(ses_path).read_text())

    def find(node, key):
        return [c for c in node if isinstance(c, list) and c and c[0] == key]

    routes = find(tree, "routes")[0]
    res = find(routes, "resolution")[0]
    scale = 1.0 / (float(res[2]) * 1000.0)            # SES unit -> mm  (unit "um")
    layers = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    vias_def = {}
    for ps in find(find(routes, "library_out")[0], "padstack") if find(routes, "library_out") else []:
        m = re.search(r"_(\d+):(\d+)_um", ps[1])
        if m:
            vias_def[ps[1]] = (int(m.group(1)) / 1000.0, int(m.group(2)) / 1000.0)
    n_t = n_v = 0
    for net in find(find(routes, "network_out")[0], "net"):
        ni = board.FindNet(net[1])
        assert ni is not None, net[1]
        for w in find(net, "wire"):
            path = find(w, "path")[0]
            layer, width = layers[path[1]], float(path[2]) * scale
            xy = [(float(path[i]) * scale, -float(path[i + 1]) * scale) for i in range(3, len(path) - 1, 2)]
            for a, b in zip(xy, xy[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(P(*a)); t.SetEnd(P(*b)); t.SetWidth(MM(width))
                t.SetLayer(layer); t.SetNet(ni)
                board.Add(t); n_t += 1
        for v in find(net, "via"):
            size, drill = vias_def.get(v[1], (0.9, 0.45))
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(P(float(v[2]) * scale, -float(v[3]) * scale))
            via.SetWidth(MM(size)); via.SetDrill(MM(drill)); via.SetNet(ni)
            board.Add(via); n_v += 1
    return n_t, n_v

def route(pcb_path: Path, classes: dict, passes=60, keep_nets=()):
    """
    Freerouting round trip. `classes` = {name: (width_mm, clearance_mm, [nets])}; nets not
    listed use 'kicad_default'. Nets in keep_nets are removed from the DSN network so the
    router leaves them alone (they are poured / hand-routed instead).
    Returns the path of the routed board (written in place).
    """
    pcb_path = Path(pcb_path)
    build = pcb_path.parent / "build"
    build.mkdir(exist_ok=True)
    dsn, ses = build / (pcb_path.stem + ".dsn"), build / (pcb_path.stem + ".ses")
    board = pcbnew.LoadBoard(str(pcb_path))
    assert pcbnew.ExportSpecctraDSN(board, str(dsn)), "DSN export failed"
    text = dsn.read_text()

    # --- rewrite the (network ...) classes ---------------------------------------
    net_names = re.findall(r'\(net ("[^"]*"|\S+)\s*\n?\s*\(pins', text)

    def unq(n):
        return n[1:-1] if n.startswith('"') else n

    for n in keep_nets:   # drop nets we do not want autorouted
        pat = re.compile(r'\(net ' + re.escape(f'"{n}"' if f'"{n}"' in text else n) + r'\s*\(pins[^)]*\)\s*\)')
        text = pat.sub("", text)
    start = text.find("(class ")
    end = text.rfind("(wiring")
    net_sec_end = text.rfind(")", start, end)          # end of (network ...)
    # remove existing classes
    text_classes_removed = re.sub(r'\(class [\s\S]*?\(rule[\s\S]*?\)\s*\)\s*\)', "", text[start:net_sec_end])
    assigned = set()
    blocks = []
    for name, (w, clr, nets) in classes.items():
        members = [n for n in net_names if unq(n) in nets and unq(n) not in keep_nets]
        assigned |= {unq(n) for n in members}
        if members:
            blocks.append(f'    (class {name} {" ".join(members)}\n      (circuit (use_via Via[0-1]_900:450_um))\n'
                          f'      (rule (width {int(w * 1000)}) (clearance {int(clr * 1000)})))')
    rest = [n for n in net_names if unq(n) not in assigned and unq(n) not in keep_nets]
    blocks.append(f'    (class kicad_default "" {" ".join(rest)}\n      (circuit (use_via Via[0-1]_900:450_um))\n'
                  f'      (rule (width 400) (clearance 250)))')
    text = text[:start] + text_classes_removed.rstrip() + "\n" + "\n".join(blocks) + "\n  " + text[net_sec_end:]
    dsn.write_text(text)

    if ses.exists():
        ses.unlink()
    cmd = ["java", "-jar", str(FREEROUTING), "-de", str(dsn), "-do", str(ses), "-mp", str(passes),
           "--gui.enabled=false"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    if not ses.exists():
        raise RuntimeError("freerouting failed:\n" + r.stdout[-3000:] + r.stderr[-3000:])
    board = pcbnew.LoadBoard(str(pcb_path))
    import_ses(board, ses)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(pcb_path), board)
    return r.stdout[-1500:]


def add_zone(board, net, layer, pts, priority=0, clearance=0.3, full=False, min_w=0.25, name=""):
    """Copper zone on an already-loaded board (used after the autorouter has run)."""
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(board.FindNet(net))
    z.SetAssignedPriority(priority)
    z.SetLocalClearance(MM(clearance))
    z.SetMinThickness(MM(min_w))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if full else pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(MM(0.4))
    z.SetThermalReliefSpokeWidth(MM(0.7))
    if name:
        z.SetZoneName(name)
    ol = z.Outline(); ol.NewOutline()
    for (x, y) in pts:
        ol.Append(MM(x), MM(y))
    board.Add(z)
    return z


def finish(pcb_path, zones=(), stitch=()):
    """Add zones [(net, layer, pts, kwargs)] and GND stitching vias to a routed board, fill, save."""
    board = pcbnew.LoadBoard(str(pcb_path))
    for net, layer, pts, kw in zones:
        add_zone(board, net, layer, pts, **kw)
    for (x, y) in stitch:
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(P(x, y)); v.SetWidth(MM(0.9)); v.SetDrill(MM(0.45))
        v.SetNet(board.FindNet("GND"))
        board.Add(v)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(pcb_path), board)


def _on_segment(track, p, tol=1000):
    """True if point p (nm) lies on track's centreline (within tol nm), ends included."""
    ax, ay, bx, by = track.GetStart().x, track.GetStart().y, track.GetEnd().x, track.GetEnd().y
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return abs(p.x - ax) <= tol and abs(p.y - ay) <= tol
    t = ((p.x - ax) * dx + (p.y - ay) * dy) / l2
    if t < -1e-9 or t > 1 + 1e-9:
        return False
    cx, cy = ax + t * dx, ay + t * dy
    return (p.x - cx) ** 2 + (p.y - cy) ** 2 <= tol * tol


def remove_dangling(pcb_path):
    """Delete autorouter stubs: tracks with an end that touches nothing (pad, via or other track)."""
    board = pcbnew.LoadBoard(str(pcb_path))
    removed = 0
    seen = set()                                   # first drop exact duplicates (also reversed copies)
    for t in [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]:
        a, c = (t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)
        key = (t.GetLayer(), t.GetNetCode(), min(a, c), max(a, c))
        if key in seen:
            board.Remove(t)
            removed += 1
        seen.add(key)
    vseen = set()                                  # vias placed before routing come back in the .ses too
    for v in [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]:
        key = (v.GetNetCode(), v.GetPosition().x, v.GetPosition().y)
        if key in vseen:
            board.Remove(v)
            removed += 1
        vseen.add(key)
    # then segments lying entirely on top of a longer one of the same net and layer (both ends on it)
    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    inner = set()
    for i, t in enumerate(tracks):
        for j, o in enumerate(tracks):
            if (j != i and j not in inner and o.GetNetCode() == t.GetNetCode() and o.GetLayer() == t.GetLayer()
                    and o.GetLength() > t.GetLength() and o.GetWidth() >= t.GetWidth()
                    and _on_segment(o, t.GetStart()) and _on_segment(o, t.GetEnd())):
                inner.add(i)
                break
    for i in sorted(inner):
        board.Delete(tracks[i])
    removed += len(inner)
    while True:
        tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
        vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
        dead = []
        for t in tracks:
            net, layer = t.GetNetCode(), t.GetLayer()
            for p in (t.GetStart(), t.GetEnd()):
                if any(o is not t and o.GetNetCode() == net and o.GetLayer() == layer and o.HitTest(p)
                       for o in tracks):
                    continue                                  # meets another track (end or T-junction)
                if any(v.GetNetCode() == net and v.HitTest(p) for v in vias):
                    continue
                if any(pad.GetNetCode() == net and pad.IsOnLayer(layer) and pad.HitTest(p)
                       for pad in board.GetPads()):
                    continue
                if any(z.GetNetCode() == net and z.IsOnLayer(layer) and z.HitTestFilledArea(layer, p)
                       for z in board.Zones()):
                    continue
                dead.append(t)
                break
        if not dead:
            break
        for t in dead:
            board.Remove(t)
        removed += len(dead)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(pcb_path), board)
    return removed



def absorb_short_legs(pcb_path):
    """Remove track stubs shorter than half their width that join two other segments end to end
    (left behind by the autorouter as tiny jogs): the neighbour at one end is stretched to the
    stub's other end, so the jog - and the near-right-angle corner it makes - disappears."""
    board = pcbnew.LoadBoard(str(pcb_path))
    count = 0
    while True:
        tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
        vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
        pads = list(board.GetPads())
        done = False
        for t in tracks:
            if t.GetLength() >= t.GetWidth() / 2 or t.GetLength() == 0:
                continue
            net, layer = t.GetNetCode(), t.GetLayer()
            ends = [t.GetStart(), t.GetEnd()]
            if any(v.GetNetCode() == net and v.HitTest(p) for v in vias for p in ends):
                continue
            if any(pd.GetNetCode() == net and pd.IsOnLayer(layer) and pd.HitTest(p) for pd in pads for p in ends):
                continue
            touch = []
            for p in ends:
                nb = [o for o in tracks if o is not t and o.GetNetCode() == net and o.GetLayer() == layer
                      and (o.GetStart() == p or o.GetEnd() == p)]
                touch.append(nb)
            if len(touch[0]) != 1 or len(touch[1]) != 1:
                continue

            def perpendicular(o):
                ax, ay = t.GetEnd().x - t.GetStart().x, t.GetEnd().y - t.GetStart().y
                bx, by = o.GetEnd().x - o.GetStart().x, o.GetEnd().y - o.GetStart().y
                la, lb = math.hypot(ax, ay), math.hypot(bx, by)
                return la > 0 and lb > 0 and abs(ax * bx + ay * by) / (la * lb) < 0.09   # 90 +- 5 deg
            perp = [perpendicular(touch[0][0]), perpendicular(touch[1][0])]
            if perp.count(True) != 1:                # only jogs that make a right angle with one side
                continue
            k = 1 if perp[0] else 0                  # stretch the other (not perpendicular) neighbour
            o, here, there = touch[k][0], ends[k], ends[1 - k]
            if o.GetStart() == here:
                o.SetStart(there)
            else:
                o.SetEnd(there)
            board.Delete(t)
            count += 1
            done = True
            break
        if not done:
            break
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(pcb_path), board)
    return count

def chamfer_corners(pcb_path, length=1.0):
    """Replace every track corner of 90 degrees or sharper (two segments of one net meeting end to
    end on one layer, not on a pad or via) with a 45-degree cut: both segments are shortened by up to
    `length` mm and joined by a diagonal. Autorouters leave such corners; they are cleaned up here.
    Returns the number of corners changed."""
    import math
    board = pcbnew.LoadBoard(str(pcb_path))
    done = 0
    skip = set()                                   # corners that can't be cut safely
    while True:
        tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
        vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
        ends = {}
        for t in tracks:
            for p in (t.GetStart(), t.GetEnd()):
                ends.setdefault((t.GetLayer(), p.x, p.y), []).append(t)
        found = None
        for (layer, x, y), lst in ends.items():
            if len(lst) != 2 or lst[0].GetNetCode() != lst[1].GetNetCode() or (layer, x, y) in skip:
                continue
            p = pcbnew.VECTOR2I(x, y)
            if any(v.HitTest(p) for v in vias) or any(pad.IsOnLayer(layer) and pad.HitTest(p) for pad in board.GetPads()):
                continue
            legs = []
            for t in lst:
                q = t.GetEnd() if (t.GetStart().x, t.GetStart().y) == (x, y) else t.GetStart()
                legs.append((t, q.x - x, q.y - y))
            (ta, ax, ay), (tb, bx, by) = legs
            la, lb = math.hypot(ax, ay), math.hypot(bx, by)
            if la == 0 or lb == 0:
                continue
            if (ax * bx + ay * by) / (la * lb) < -1e-3:      # angle between legs > 90 deg: fine
                continue
            # A third track of the net joining either leg (T-junction) or a via / pad on a leg near
            # the corner would be cut off: keep the chamfer short of the nearest such point.
            net = ta.GetNetCode()
            # a short leg may be used up completely (the diagonal then starts at its far end)
            lim = min(la, lb) if min(la, lb) < MM(length) else min(la, lb) * 0.45
            for t in tracks:
                if t is ta or t is tb or t.GetNetCode() != net or t.GetLayer() != layer:
                    continue
                for q in (t.GetStart(), t.GetEnd()):
                    for (lt, lx, ly, ll) in ((ta, ax, ay, la), (tb, bx, by, lb)):
                        if lt.HitTest(q):
                            d = math.hypot(q.x - x, q.y - y)
                            far_end = abs(q.x - (x + lx)) < 2 and abs(q.y - (y + ly)) < 2
                            lim = min(lim, d if far_end else d - MM(0.6))   # far end stays put
            for v in vias:
                if v.GetNetCode() == net:
                    for (lt, lx, ly) in ((ta, ax, ay), (tb, bx, by)):
                        q = v.GetPosition()
                        if lt.HitTest(q):
                            d = math.hypot(q.x - x, q.y - y)
                            far_end = abs(q.x - (x + lx)) < 2 and abs(q.y - (y + ly)) < 2
                            lim = min(lim, d if far_end else d - MM(0.6))
            if lim < MM(0.25):
                skip.add((layer, x, y))
                continue
            found = (layer, x, y, ta, ax, ay, la, tb, bx, by, lb, lim)
            break
        if not found:
            break
        layer, x, y, ta, ax, ay, la, tb, bx, by, lb, lim = found
        c = min(MM(length), lim)
        pa = pcbnew.VECTOR2I(int(x + ax / la * c), int(y + ay / la * c))
        pb = pcbnew.VECTOR2I(int(x + bx / lb * c), int(y + by / lb * c))
        for t, np_ in ((ta, pa), (tb, pb)):
            if (t.GetStart().x, t.GetStart().y) == (x, y):
                t.SetStart(np_)
            else:
                t.SetEnd(np_)
        for t in (ta, tb):                         # a leg used up completely disappears
            if t.GetStart() == t.GetEnd():
                board.Remove(t)
        d = pcbnew.PCB_TRACK(board)
        d.SetStart(pa)
        d.SetEnd(pb)
        d.SetWidth(min(ta.GetWidth(), tb.GetWidth()))
        d.SetLayer(layer)
        d.SetNetCode(ta.GetNetCode())
        board.Add(d)
        done += 1
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(pcb_path), board)
    return done


# =============================================================================== checks / renders
def drc(pcb_path: Path):
    pcb_path = Path(pcb_path)
    rpt = pcb_path.parent / "build" / "drc.rpt"
    rpt.parent.mkdir(exist_ok=True)
    board = pcbnew.LoadBoard(str(pcb_path))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.WriteDRCReport(board, str(rpt), pcbnew.EDA_UNITS_MILLIMETRES, True)
    s = rpt.read_text()
    v = re.search(r"\*\* Found (\d+) DRC violations \*\*", s)
    u = re.search(r"\*\* Found (\d+) unconnected pads \*\*", s)
    f = re.search(r"\*\* Found (\d+) Footprint errors \*\*", s)
    kinds = {}
    for m in re.finditer(r"^\[(\w+)\]:[^\n]*\n[^\n]*Severity: (\w+)", s, re.M):
        kinds[f"{m.group(2)}:{m.group(1)}"] = kinds.get(f"{m.group(2)}:{m.group(1)}", 0) + 1
    return dict(violations=int(v.group(1)) if v else -1, unconnected=int(u.group(1)) if u else -1,
                footprint=int(f.group(1)) if f else -1, kinds=kinds,
                errors=sum(n for k, n in kinds.items() if k.startswith("error:")), report=rpt)


def render(pcb_path: Path, out_dir: Path, name: str, bbox=(98, 48, 202, 152), px_per_mm=16):
    """
    Front and back (mirrored) previews. Copper and silkscreen are exported as separate full-page
    SVGs (same coordinate frame), rasterised, composited with silk on top and cropped to bbox (mm).
    """
    import cairosvg
    from PIL import Image
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    pcb_path = Path(pcb_path)
    build = pcb_path.parent / "build"
    build.mkdir(exist_ok=True)
    res = {}
    for side, cu, silk, mirror in (("front", "F.Cu,Edge.Cuts", "F.SilkS", False),
                                   ("back", "B.Cu,Edge.Cuts", "B.SilkS", True)):
        imgs = []
        for tag, layers in (("cu", cu), ("silk", silk)):
            svg = build / f"{name}_{side}_{tag}.svg"
            subprocess.run(["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet",
                            "--page-size-mode", "0", *(["--mirror"] if mirror else []), "-o", str(svg),
                            str(pcb_path)], check=True, capture_output=True)
            m = re.search(r'width="([\d.]+)mm" height="([\d.]+)mm"', svg.read_text()[:2000])
            pw, ph = float(m.group(1)), float(m.group(2))
            png = build / f"{name}_{side}_{tag}.png"
            cairosvg.svg2png(url=str(svg), write_to=str(png), output_width=int(pw * px_per_mm))
            imgs.append(Image.open(png).convert("RGBA"))
        base = Image.new("RGBA", imgs[0].size, "white")
        silk_img = imgs[1]
        # darken the (pale) silkscreen so it reads on top of the copper colour
        r, g, bb, a = silk_img.split()
        dark = Image.new("RGBA", silk_img.size, (30, 30, 30, 255))
        dark.putalpha(a)
        comp = Image.alpha_composite(Image.alpha_composite(base, imgs[0]), dark)
        x0, y0, x1, y1 = bbox
        if mirror:
            x0, x1 = pw - x1, pw - x0
        crop = comp.crop((int(x0 * px_per_mm), int(y0 * px_per_mm), int(x1 * px_per_mm), int(y1 * px_per_mm)))
        out = out_dir / f"{name}_{side}.png"
        crop.convert("RGB").save(out)
        res[side] = out
    return res


def render_sch(sch_path: Path, out_png: Path):
    import cairosvg
    sch_path = Path(sch_path)
    d = sch_path.parent / "build"
    d.mkdir(exist_ok=True)
    subprocess.run(["kicad-cli", "sch", "export", "svg", "-o", str(d), str(sch_path)], check=True,
                   capture_output=True)
    svg = d / (sch_path.stem + ".svg")
    cairosvg.svg2png(url=str(svg), write_to=str(out_png), output_width=3000, background_color="white")
    return out_png
