"""more_sources.py - further code sources for build_db.py.

  load_lirc(db, root)      LIRC remotes database, as converted to XML (protocol / device / function and
                           Pronto hex per button) in  https://github.com/probonopd/lirc-remotes
  load_jp1(db, root)       JP1 device upgrades (RemoteMaster .rmdu and KeymapMaster .txt files) from
                           https://github.com/hifiremote/deviceupgrades
  load_smartir(db, root)   SmartIR (Home Assistant) code files: mostly air conditioners, where every
                           state (mode, fan, temperature) is one code.  https://github.com/smartHomeHub/SmartIR

All three add remotes through db.add_profile(), like the loaders in build_db.py, so the same
de-duplication, key naming and categories apply.
"""
import base64
import json
import os
import re
from collections import Counter, defaultdict

import build_db as B


# =====================================================================================================
# Pronto hex / Broadlink -> durations (microseconds: mark, space, mark, ...)
# =====================================================================================================
def pronto_durations(ccf):
    """Raw Pronto hex ("0000 006D 0022 0002 ...") -> (durations, carrier Hz), or None.
    The intro sequence is used; when there is none, the repeat sequence."""
    try:
        w = [int(x, 16) for x in ccf.split()]
    except ValueError:
        return None
    if len(w) < 6 or w[0] != 0 or w[1] == 0:
        return None
    freq = 1000000.0 / (w[1] * 0.241246)
    n1, n2 = w[2], w[3]
    if len(w) < 4 + 2 * (n1 + n2):
        return None
    seq = w[4:4 + 2 * n1] if n1 else w[4:4 + 2 * n2]
    unit = 1000000.0 / freq
    return [max(1, int(round(v * unit))) for v in seq], int(round(freq))


def broadlink_durations(b64):
    """Broadlink IR packet (base64) -> durations, or None. Timings are in units of 269/8192 ms."""
    try:
        b = base64.b64decode(b64)
    except Exception:
        return None
    if len(b) < 6 or b[0] != 0x26:
        return None
    n = b[2] | (b[3] << 8)
    d = b[4:4 + n]
    out, i = [], 0
    while i < len(d):
        v = d[i]
        i += 1
        if v == 0:
            if i + 1 >= len(d):
                break
            v = (d[i] << 8) | d[i + 1]
            i += 2
        out.append(max(1, int(round(v * 269000.0 / 8192))))
    return out


def _fit(data):
    """Even length, each duration within 16 bits (a longer gap is cut, the frame is already over)."""
    data = [min(int(abs(v)), 65535) for v in data]
    if len(data) % 2:
        data.append(40000)
    return data


# =====================================================================================================
# LIRC
# =====================================================================================================
_RX_CODE = re.compile(r'<code name="([^"]*)"[^>]*>(.*?)</code>', re.S)
_RX_DEC = re.compile(r'<decoding protocol="([^"]*)" device="(-?\d+)" subdevice="(-?\d+)" obc="(-?\d+)"')
_RX_CCF = re.compile(r"<ccf>([^<]*)</ccf>")
_RX_REMOTE = re.compile(r'<remote name="([^"]*)">(.*?)</remote>', re.S)
_XML_ENT = {"&amp;": "&", "&lt;": "<", "&gt;": ">", "&quot;": '"', "&apos;": "'"}


def _unxml(s):
    for k, v in _XML_ENT.items():
        s = s.replace(k, v)
    return s


def load_lirc(db, root):
    non_hifi = {B.brand_key(x) for x in B.NON_HIFI_BRANDS}
    for bdir in sorted(os.listdir(root)):
        base = os.path.join(root, bdir)
        if not os.path.isdir(base) or bdir.startswith("."):
            continue
        bname = B.pretty_brand(bdir)
        if B.brand_key(bname) not in db.brands and bname == bname.lower():
            bname = bname.replace("_", " ").title()               # "grundig" -> "Grundig"
        for fn in sorted(os.listdir(base)):
            if not fn.lower().endswith(".xml"):
                continue
            with open(os.path.join(base, fn), encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
            model = re.sub(r"\.(lircd\.)?(conf|xml)$", "", re.sub(r"\.xml$", "", fn), flags=re.I)
            remotes = _RX_REMOTE.findall(text)
            for ri, (rname, body) in enumerate(remotes):
                codes = []
                for cname, cbody in _RX_CODE.findall(body):
                    name = _unxml(cname).strip()[:31]
                    conv = None
                    for proto, d, s, f in _RX_DEC.findall(cbody):   # a decoding the firmware has an encoder for
                        if "?" in proto or proto.startswith("Gap"):
                            continue
                        try:
                            conv = B.irdb_convert(_unxml(proto), int(d), int(s), int(f))
                        except Exception:
                            conv = None
                        if conv is not None:
                            break
                    if conv is None:                                 # else what was recorded, as a raw signal
                        m = _RX_CCF.search(cbody)
                        pd = pronto_durations(m.group(1)) if m else None
                        if pd and 4 <= len(pd[0]) <= 2000:
                            conv = db.raw_code(_fit(pd[0]), pd[1], 0.33)
                    if conv is None:
                        db.skipped["lirc:no signal"] += 1
                        continue
                    codes.append({"name": name, "code": conv})
                    db.proto_count[B.PROTOS[conv[0]]] += 1
                if len(codes) < 2:
                    continue
                pname = model.replace("_", " ")
                if len(remotes) > 1 and rname and rname.lower() not in pname.lower():
                    pname += " " + _unxml(rname).replace("_", " ")
                pname = B.strip_brand(pname, bname)
                names = [c["name"] for c in codes]
                cat = B.guess_universal(names, pname, B.brand_key(bname) in db.brands and B.brand_key(bname) not in non_hifi)
                db.add_profile(bname, pname[:47], B.settop_override(cat, bname), "L", codes)
                db.source_count["LIRC"] += 1


# =====================================================================================================
# JP1
# =====================================================================================================
# A device upgrade stores each function as "hex" bytes for the remote's protocol executor, not as a
# function number. The translations below (hex -> D / S / F of the IRP protocol) were found per
# executor and checked against the IRDB: with the right one, most keyed functions of a JP1 upgrade match
# the IRDB code set with the same protocol / device (NEC1: 14,582 of 29,101; a wrong bit order: none).
# Executors that could not be confirmed that way (Sony Combo, Pioneer MIX, RCA Combo, "NEC1 (No
# Repeats)", ...) are skipped: a wrong code is worse than a missing one. The older KeymapMaster .txt
# files are only used for the executors in JP1_TXT_OK, where they matched as well (RC-5 and Denon
# did not: their parameters are laid out differently there).
def _rev(b, n=8):
    return int(format(b & ((1 << n) - 1), "0%db" % n)[::-1], 2)


def _rc(b):
    return _rev(b) ^ 0xFF


def _p(parms, i):
    return parms[i] if i < len(parms) else None


def _nec(irp):
    def f(p, h):
        d, s = _p(p, 0), _p(p, 1)
        if d is None or len(h) != 1:
            return None
        return (irp, d, -1 if s is None else s, _rc(h[0]))
    return f


def _one(irp, fn, sub=False, sub_default=-1):
    """Single device (and optional subdevice) from the parameters, one hex byte."""
    def f(p, h):
        d = _p(p, 0)
        if d is None or len(h) != 1:
            return None
        s = sub_default
        if sub:
            s = _p(p, 1)
            s = sub_default if s is None else s
        return (irp, d, s, fn(h[0]))
    return f


def _rc5(p, h):
    if len(h) != 1 or (h[0] & 3) > 2:
        return None
    i = h[0] & 3                                   # up to three devices; the flag adds 64 to the function
    d = _p(p, 2 * i)
    if d is None:
        return None
    return ("RC5", d, -1, ((h[0] ^ 0xFF) >> 2) + 64 * (_p(p, 2 * i + 1) or 0))


def _sony1215(p, h):
    if len(h) != 1:
        return None
    d = _p(p, 2 * (h[0] & 1))
    if d is None:
        return None
    return ("Sony12" if d < 32 else "Sony15", d, -1, _rev(h[0]) & 0x7F)


def _nec4dev(p, h):
    if len(h) != 2:
        return None
    i = (h[1] >> 6) & 3
    d, s = _p(p, 2 * i), _p(p, 2 * i + 1)
    if d is None:
        return None
    return ("NEC1", d, -1 if s is None else s, _rc(h[0]))


def _denon_combo(irp):
    return lambda p, h: (irp, _rev((h[0] ^ 0xFF) >> 3, 5), -1, _rc(h[1])) if len(h) == 2 else None


def _pana_combo(p, h):
    if len(h) != 2 or _p(p, 0) is None:
        return None
    return ("Panasonic", p[0], _rc(h[0]), _rc(h[1]))


def _nec1_combo(p, h):
    if len(h) != 2 or _p(p, 0) is None:
        return None
    return ("NEC1", p[0], _rc(h[0]), _rc(h[1]))


JP1_EXECUTORS = {
    "nec1": _nec("NEC1"), "nec2": _nec("NEC2"), "necx1": _nec("NECx1"), "necx2": _nec("NECx2"),
    "nec1 combo": _nec1_combo,
    "nec 4dev combo": _nec4dev, "nec 4dev yamaha combo": _nec4dev,
    "rc-5": _rc5,
    "rc-6": _one("RC6", lambda b: b),
    "sony 12/15": _sony1215,
    "sony20": _one("Sony20", lambda b: _rev(b) & 0x7F, sub=True, sub_default=0),
    "panasonic": _one("Panasonic", _rc, sub=True, sub_default=0),
    "panasonic combo": _pana_combo,
    "panasonic (old)": _one("Panasonic_Old", lambda b: _rev(b) & 0x3F),
    "jvc": _one("JVC", _rc),
    "jvc combo": lambda p, h: ("JVC", _rc(h[0]), -1, _rc(h[1])) if len(h) == 2 else None,
    "aiwa": _one("Aiwa", _rev, sub=True, sub_default=0),
    "denon": _one("Denon", _rev),
    "denon combo (official)": _denon_combo("Denon"),
    "sharp combo (official)": _denon_combo("Sharp"),
    "proton": _one("Proton", _rc),
    "rca": _one("RCA", lambda b: b),
    "nokia32": _one("Nokia32", lambda b: b, sub=True),
    "mce": _one("MCE", lambda b: b, sub=True),
    "recs80 (45)": _one("RECS80", lambda b: ((b ^ 0xFF) >> 2) & 0x3F),
    "directv": lambda p, h: ("DirecTV", _p(p, 0) or 0, -1, h[0]) if len(h) == 1 else None,
    "gi cable": lambda p, h: ("G.I.Cable", 0, -1, _rev(h[0])) if len(h) == 1 else None,
    "dish network": lambda p, h: ("Dish_Network", 0, 0, h[0] >> 2) if len(h) == 1 else None,
}
JP1_TXT_OK = {"nec1", "nec2", "necx1", "necx2", "jvc", "panasonic", "panasonic (old)", "aiwa", "rca", "sony20",
              "sony 12/15", "rc-6", "proton", "gi cable", "dish network", "recs80 (45)"}
JP1_CATS = {
    "A_C": "AC", "Audio": "AVR", "Audio_Video Selectors": "AVR", "CD": "CD", "Cable": "SetTop", "Car Stereos": "Car",
    "Combo Audio Systems": "System", "DVD (Blu-Ray)": "Disc", "DVD (standard)": "Disc", "DVD _ VCR combo units": "Disc",
    "DVR_PVR": "SetTop", "Digital Cameras": "Camera", "Digital STBs": "SetTop", "Fans": "Climate",
    "HTPC - Media Centers": "Streaming", "Laser Disc": "Disc", "MP3 Players": "Audio", "Media Players": "Streaming",
    "Minidisc & DAT": "MD", "Projectors": "Projector", "Satellite": "SetTop", "Satellite Radio": "Tuner",
    "Soundbars": "Soundbar", "TV": "TV", "TV Combos": "TV", "Tape": "Tape", "VCR": "VCR",
}
JP1_DEVTYPES = {"tv": "TV", "cable": "SetTop", "sat": "SetTop", "dvd": "Disc", "vcr": "VCR", "cd": "CD", "tuner": "AVR",
                "amp": "Amp", "rcvr": "AVR", "audio": "AVR", "tape": "Tape", "laser": "Disc", "pvr": "SetTop", "dvr": "SetTop"}


def _efc_hex(efc):
    """KeymapMaster's 3-digit EFC -> the function's hex byte."""
    v = ((efc + 156) & 0xFF) ^ 0xAE
    return ((v >> 3) | (v << 5)) & 0xFF


def _parse_rmdu(path):
    d, fn = {}, {}
    with open(path, encoding="latin-1") as fh:
        for line in fh:
            line = line.rstrip("\r\n")
            if "=" not in line or line.startswith("#"):
                continue
            k, v = line.split("=", 1)
            m = re.match(r"Function\.(\d+)\.(name|hex)$", k)
            if m:
                fn.setdefault(int(m.group(1)), {})[m.group(2)] = v
            elif k not in d:
                d[k] = v
    parms = []
    for x in (d.get("ProtocolParms") or "").split():
        parms.append(int(x) if x.lstrip("-").isdigit() else None)
    funcs = []
    for i in sorted(fn):
        try:
            funcs.append((fn[i].get("name", ""), [int(x, 16) for x in fn[i]["hex"].split()]))
        except (KeyError, ValueError):
            pass
    return dict(desc=d.get("Description", ""), proto=(d.get("Protocol.name") or "").replace("\\", ""),
                parms=parms, funcs=funcs, devtype=d.get("DeviceType", ""))


def _parse_km_txt(path):
    """KeymapMaster export (comma separated). 'Devices:' has the protocol and its parameters; the
    function table follows a row starting 'fFunctions', with the EFC in the second column."""
    name, proto, parms, funcs, in_fn = "", "", [], [], False
    with open(path, encoding="latin-1") as fh:
        for line in fh:
            c = [x.strip() for x in line.rstrip("\r\n").split(",")]
            if not c:
                continue
            if c[0] == "Name:" and len(c) > 1:
                name = c[1]
            elif c[0] == "Devices:" and len(c) > 1:
                proto = c[1]
                for x in c[2:8]:
                    parms.append(int(x) if x.isdigit() else None)
                while parms and parms[-1] is None:
                    parms.pop()
            elif c[0] == "fFunctions":
                in_fn = True
            elif in_fn and len(c) > 1 and c[0] and re.fullmatch(r"\d{1,3}", c[1]) and (len(c) < 3 or not c[2]):
                funcs.append((c[0], [_efc_hex(int(c[1]))]))
    return dict(desc=name, proto=proto, parms=parms, funcs=funcs, devtype="")


def load_jp1(db, root):
    known = set(db.brands)                       # brands of the sources loaded so far
    files = []
    for dirpath, _, fns in os.walk(root):
        for fn in sorted(fns):
            if fn.lower().endswith((".rmdu", ".txt")):
                files.append(os.path.join(dirpath, fn))
    files.sort()
    parsed = []
    for path in files:
        try:
            u = _parse_rmdu(path) if path.lower().endswith(".rmdu") else _parse_km_txt(path)
        except Exception:
            db.skipped["jp1:unreadable"] += 1
            continue
        u["path"] = path
        stem = os.path.splitext(os.path.basename(path))[0]
        u["title"] = re.sub(r"^\d+\s+", "", stem).replace("_", " ").strip()      # "2 Toshiba_47HL167" -> "Toshiba 47HL167"
        parsed.append(u)
    # brand: the first word(s) of the description or file name. Known brands first; an unknown first
    # word only counts as a brand when several files start with it.
    first = Counter()
    for u in parsed:
        for t in {(u["desc"].split() or [""])[0].lower(), (u["title"].split() or [""])[0].lower()}:
            if re.fullmatch(r"[a-z][a-z&'-]{2,}", t):
                first[t] += 1
    generic = {"the", "new", "my", "old", "universal", "remote", "generic", "unknown", "device", "upgrade", "copy", "test",
               "tv", "dvd", "vcr", "cable", "sat", "satellite", "audio", "amp", "receiver", "digital", "hdtv", "home", "hd",
               "led", "lcd", "plasma", "blu-ray", "bluray", "dvr", "pvr", "projector", "media", "for", "with", "all"}

    def brand_of(u):
        for text in (u["desc"], u["title"]):
            words = text.replace("_", " ").split()
            for n in (3, 2, 1):
                cand = " ".join(words[:n])
                if len(words) >= n and len(cand) > 1 and B.brand_key(B.pretty_brand(cand)) in known:
                    return B.pretty_brand(cand), " ".join(words[n:])
        for text in (u["desc"], u["title"]):
            words = text.replace("_", " ").split()
            if words and first[words[0].lower()] >= 3 and words[0].lower() not in generic:
                w = words[0]
                return (w.title() if w.islower() or (w.isupper() and len(w) > 4) else w), " ".join(words[1:])
        return None, ""

    for u in parsed:
        fn = JP1_EXECUTORS.get(u["proto"].strip().lower())
        if fn and u["path"].lower().endswith(".txt") and u["proto"].strip().lower() not in JP1_TXT_OK:
            fn = None
        if not fn:
            db.skipped["jp1:executor " + (u["proto"] or "?")] += 1
            continue
        codes = []
        for name, hb in u["funcs"]:
            name = name.strip()[:31]
            if not name or not hb:
                continue
            try:
                r = fn(u["parms"], hb)
            except Exception:
                r = None
            if not r:
                continue
            conv = B.irdb_convert(r[0], r[1], r[2], r[3])
            if conv is None and db.irp_render:
                conv = ("IRP", r[0], r[1], r[2], r[3])                # rendered to raw later
            if conv is None:
                continue
            codes.append({"name": name, "code": conv})
            if conv[0] != "IRP":
                db.proto_count[B.PROTOS[conv[0]]] += 1
        if len(codes) < 3:
            db.skipped["jp1:too few functions"] += 1
            continue
        folder = os.path.relpath(u["path"], root).split(os.sep)[0]
        brand, rest = brand_of(u)
        title = u["desc"].strip() or u["title"]
        if folder == "UEI Official":
            brand, rest = "UEI setup codes", u["title"]
        elif brand is None:
            brand, rest = "JP1 other", title
        pname = (rest or title).strip(" -_,") or u["title"]
        cat = JP1_CATS.get(folder) or JP1_DEVTYPES.get(u["devtype"].strip().lower())
        if cat is None:
            cat = B.guess_universal([c["name"] for c in codes], pname, False)
        db.add_profile(brand, pname[:47], B.settop_override(cat, brand), "J", codes)
        db.source_count["JP1"] += 1


# =====================================================================================================
# SmartIR
# =====================================================================================================
_SMART_NAMES = {"off": "Off", "on": "On", "previouschannel": "Ch -", "nextchannel": "Ch +", "volumedown": "Vol -",
                "volumeup": "Vol +", "mute": "Mute", "brighten": "Brightness +", "dim": "Brightness -",
                "colder": "Colder", "warmer": "Warmer", "night": "Night"}


def _smart_walk(o, path):
    if isinstance(o, dict):
        for k, v in o.items():
            yield from _smart_walk(v, path + [str(k)])
    else:
        yield path, o


def _smart_signal(v, enc):
    """One command -> (durations, carrier Hz) or None."""
    if enc == "Base64" and isinstance(v, str):
        d = broadlink_durations(v)
        return (d, 38000) if d else None
    if enc == "Pronto" and isinstance(v, str):
        return pronto_durations(v)
    if enc == "Raw":
        if isinstance(v, str):
            s = v.strip().strip("[]")
            if not re.fullmatch(r"[-\d,\s]+", s):
                return None                          # (Xiaomi's packed format isn't timings)
            try:
                v = [int(x) for x in re.split(r"[,\s]+", s) if x]
            except ValueError:
                return None
        if isinstance(v, list) and all(isinstance(x, (int, float)) for x in v):
            return [max(1, int(abs(x))) for x in v], 38000
    return None


def load_smartir(db, root):
    base = os.path.join(root, "codes")
    cats = {"climate": "AC", "fan": "Climate", "light": "Light", "media_player": None}
    for cls in sorted(cats):
        cdir = os.path.join(base, cls)
        if not os.path.isdir(cdir):
            continue
        for fn in sorted(os.listdir(cdir)):
            if not fn.endswith(".json"):
                continue
            try:
                with open(os.path.join(cdir, fn), encoding="utf-8") as fh:
                    j = json.load(fh)
            except Exception:
                db.skipped["smartir:bad file"] += 1
                continue
            enc = j.get("commandsEncoding")
            brand = (j.get("manufacturer") or "").strip()
            if not brand or brand.lower() in ("unknown", "generic"):
                brand = "Generic"
            models = [str(m).strip() for m in (j.get("supportedModels") or []) if str(m).strip()]
            codes = []
            for path, v in _smart_walk(j.get("commands") or {}, []):
                sig = _smart_signal(v, enc)
                if not sig or not (4 <= len(sig[0]) <= 2000):
                    db.skipped["smartir:" + str(enc) + " not timings"] += 1
                    continue
                if len(path) == 1:
                    name = _SMART_NAMES.get(path[0].lower(), path[0])
                elif path[0].lower() == "sources":
                    name = " ".join(path[1:])
                else:
                    name = " ".join(x.replace("_", " ").title() if x.isalpha() else x for x in path)   # "Cool Auto 24"
                data = _fit(sig[0])
                # a state of an air conditioner is one long frame no protocol encoder covers: keep it as it is
                conv = db.raw_store(data, sig[1], 0.33) if cls == "climate" else db.raw_code(data, sig[1], 0.33)
                codes.append({"name": name[:31], "code": conv})
                db.proto_count[B.PROTOS[conv[0]]] += 1
            if len(codes) < 2:
                continue
            pname = (models[0] if models else "SmartIR " + fn[:-5])
            if len(models) > 1:
                pname += " +%d" % (len(models) - 1)
            cat = cats[cls] or B.guess_universal([c["name"] for c in codes], pname, False)
            if cat == "Other" and cls == "media_player":
                cat = "TV"
            # one remote can't hold more than 65535 codes; SmartIR's largest has under 5000
            db.add_profile(B.pretty_brand(brand), B.strip_brand(pname, brand)[:47], cat, "S", codes[:60000])
            db.source_count["SmartIR"] += 1
