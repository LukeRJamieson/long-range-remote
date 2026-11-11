# Mock of the remote's web API, enough for the page to start, the Code search tab and the Discover tab.
import json, re, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer
src = open(sys.argv[1]).read()
PAGE = src[src.index('R"rawliteral(') + 13: src.rindex(')rawliteral"')]
import os
_k = open(os.path.join(os.path.dirname(sys.argv[1]), "ir_keys.h")).read()
def _arr(name):
    body = _k[_k.index(name):]; body = body[body.index("{") + 1: body.index("};")]
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)
KEYS = [list(x) for x in zip(_arr("IR_KEY_IDS[KEY__COUNT]"), _arr("IR_KEY_LABELS[KEY__COUNT]"))]
FAMS = [["NEC", "most Asian brands: TVs, amps, soundbars, LED lights", 256], ["NEC extended", "newer TVs and boxes", 256], ["Sony 12-bit", "Sony TVs and hi-fi", 128]]
D = dict(mode="idle", all=0, done=0, fam=0, pos=0, func=0, fpos=0, learned=[], restart=0)
LOG = []
SENDS = []
def state(err=""):
    s = dict(mode=D["mode"], all=D["all"], done=D["done"], sending=0, restart=D["restart"], err=err)
    if D["mode"] != "idle":
        s.update(fam=D["fam"], famName=FAMS[D["fam"]][0], addr=D["pos"], addrText="device %d" % D["pos"], pos=D["pos"], total=256, famPos=D["fam"], famTotal=len(FAMS), eta=200)
    if D["mode"] == "learn":
        s.update(func=D["func"], funcPos=D["fpos"], nFuncs=256, guess=(D["func"] % 7), file=["Discovered", "NEC %d" % D["pos"]], learned=D["learned"])
    return s
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def out(self, body, ct="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(200); self.send_header("Content-Type", ct); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query, keep_blank_values=True).items()}
        if u.path == "/": return self.out(PAGE, "text/html")
        if u.path == "/log": return self.out(json.dumps(LOG))
        if u.path == "/api/meta": return self.out(json.dumps(dict(keys=KEYS, cats=["TV", "AVR", "Other"], protos=["NEC1", "Sony12"])))
        if u.path == "/api/brands": return self.out(json.dumps([["Samsung", 2, 0], ["Sony", 2, 2]]))
        if u.path == "/api/groups": return self.out(json.dumps([["Most popular", 2, 4]]))
        if u.path == "/api/state": return self.out(json.dumps(dict(active=-1, favs=[], db=dict(brands=2, profiles=4, codes=100), power=0, powers=["Full", "Half", "Low"], last=dict(seq=len(SENDS), label="Power", proto="NEC", frames=1))))
        if u.path == "/api/scan": return self.out(json.dumps([[100 + i, i, "Brand%d" % i, "Remote %d" % i, "Power", 1] for i in range(40)]))
        if u.path == "/api/send":
            import time; SENDS.append([time.time(), int(q.get("c", -1))]); return self.out("ok", "text/plain")
        if u.path == "/sends": return self.out(json.dumps(SENDS))
        if u.path == "/api/disc":
            a = q.get("a", "state"); LOG.append(u.query)
            if a == "meta": return self.out(json.dumps(dict(families=FAMS)))
            if a == "sweep": D.update(mode="sweep", fam=max(0, int(q["fam"])), all=int(int(q["fam"]) < 0), pos=7, done=0, learned=[])
            elif a == "reacted": D.update(mode="narrow", pos=D["pos"] - 3)
            elif a == "step": D["pos"] += int(q["d"])
            elif a == "resume": D["mode"] = "sweep"
            elif a == "skip": D["fam"] += 1
            elif a == "pick": D.update(mode="learn", func=8, fpos=0)
            elif a == "open": D.update(mode="learn", fam=int(q["fam"]), pos=int(q["addr"], 0), func=8, fpos=0, learned=[])
            elif a == "fstep": D["fpos"] += int(q["d"]); D["func"] = (8 + D["fpos"] * 5) % 256
            elif a == "fgoto": D["func"] = int(q["f"], 0)
            elif a == "name":
                k = int(q["key"]); nm = KEYS[k][1] if k else (q.get("text") or "F %d" % D["func"])
                D["learned"] = [l for l in D["learned"] if l[0] != D["func"]] + [[D["func"], k, nm]]
            elif a == "forget": D["learned"] = [l for l in D["learned"] if l[0] != int(q["f"])]
            elif a == "save": D["restart"] = 1
            elif a == "stop": D.update(mode="idle", learned=[])
            return self.out(json.dumps(state()))
        self.send_response(404); self.end_headers()
HTTPServer(("127.0.0.1", 8765), H).serve_forever()
