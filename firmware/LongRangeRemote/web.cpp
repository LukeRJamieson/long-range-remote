// web.cpp - Wi-Fi + HTTP server exposing the IR database and transmitter to a phone browser.
//
// JSON API (all GET):
//   /api/state                         active device, favourites, network, last code sent
//   /api/meta                          key labels, category names, manual-entry protocol names
//   /api/brands                        [[name, profileCount, firstProfile], ...]   (index = brand id)
//   /api/profiles?b=ID                 [[profile, name, category, codeCount, fromIrdb], ...]
//   /api/codes?p=ID                    {p, brand, name, cat, codes:[[code, name, key, proto], ...]}
//   /api/send?c=CODE&a=tap|down|ka|up  send a database code (down = start hold, ka = keep-alive, up = release)
//   /api/manual?proto=NEC1&d=&s=&f=&a= send any code in IRP terms (s=-1 for default)
//   /api/groups                        brand groups [[name, brandCount, remoteCount], ...]
//   /api/scan?b=ID|g=GROUP&t=TYPE&k=KEY code-search candidates, one per distinct code (b/g/t optional):
//                                      [[code, profile, brand, profileName, codeName, remotesSharingIt], ...]
//   /api/fav?op=add|del|active&p=ID    manage "My devices"   (/api/fav?op=clear removes all)
//   POST /api/upload?kind=db           upload a new code database (irdb.bin) to the SD card
//   POST /api/upload?kind=ir&path=P    upload one of your own .ir files to /ir/P on the SD card
//   /api/power?l=N                     LED power level (0 = full); the level is also in /api/state (lp, lpn)
//   /api/restart                       restart (loads the new codes)
#include "web.h"
#include "web_page.h"
#include "config.h"
#include "hw.h"
#include "ir_db.h"
#include "ir_keys.h"
#include "ir_tx.h"
#include "log.h"
#include "power.h"
#include "sd_store.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>
#include <new>

namespace web {
namespace {

WebServer server(80);
uint32_t net_changes_seen = 0xFFFFFFFF;           // last wifimgr::changeCounter() pushed to the UI
bool wifi_was_suspended = false;
uint32_t restart_at = 0;                          // /api/restart: when to restart (0 = no)

// ---------------------------------------------------------------------------
// Streaming JSON writer (chunked transfer, 1 KB buffer) - keeps RAM use low
// even for the "scan all brands" list.
// ---------------------------------------------------------------------------
class Json {
 public:
  Json() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "");
  }
  ~Json() { flush(); server.sendContent(""); }
  Json &raw(const char *s) { while (*s) put(*s++); return *this; }
  Json &num(long v) { char b[16]; snprintf(b, sizeof(b), "%ld", v); return raw(b); }
  Json &str(const char *s) {
    put('"');
    for (; *s; s++) {
      unsigned char c = *s;
      if (c == '"' || c == '\\') { put('\\'); put(c); }
      else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); raw(b); }
      else if (c >= 0x80) put('?');            // database is ASCII; be safe
      else put(c);
    }
    put('"');
    return *this;
  }
 private:
  char buf[1024];
  size_t n = 0;
  void put(char c) { buf[n++] = c; if (n == sizeof(buf)) flush(); }
  void flush() { if (n) { server.sendContent(buf, n); n = 0; } }
};

long argInt(const char *name, long def) {
  if (!server.hasArg(name) || server.arg(name).length() == 0) return def;
  String v = server.arg(name);
  return strtol(v.c_str(), nullptr, 0);          // accepts decimal or 0x.. hex
}

void badRequest(const char *msg) { server.send(400, "text/plain", msg); }

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
void hRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", WEB_PAGE);
}

void hState() {
  Json j;
  j.raw("{\"active\":").num(settings::active());
  j.raw(",\"favs\":[");
  for (int i = 0; i < settings::favCount(); i++) {
    int p = settings::fav(i);
    if (i) j.raw(",");
    j.raw("[").num(p).raw(",").str(irdb::brandName(irdb::profileBrand(p))).raw(",").str(irdb::profileName(p)).raw("]");
  }
  j.raw("],\"ap\":").str(wifimgr::apIp().c_str());
  j.raw(",\"sta\":").str(wifimgr::ip().c_str());
  j.raw(",\"ssid\":").str(wifimgr::ssid().c_str());
  j.raw(",\"irout\":").str(irtx::outputDesc());
  {
    static const char *const lpn[LED_POWER_LEVELS] = LED_POWER_NAMES;
    j.raw(",\"lp\":").num(settings::ledPower()).raw(",\"lpn\":[");
    for (int i = 0; i < LED_POWER_LEVELS; i++) { if (i) j.raw(","); j.str(lpn[i]); }
    j.raw("]");
  }
  float v = hw::packVolts();
  char vb[16]; snprintf(vb, sizeof(vb), "%.2f", isnan(v) ? 0.0f : v);
  char sb[16]; snprintf(sb, sizeof(sb), "%.2f", hw::screenVolts());
  j.raw(",\"scr\":").raw(sb).raw(",\"scrpct\":").num(hw::screenPercent());
  j.raw(",\"pack\":").raw(vb).raw(",\"pct\":").num(hw::packPercent()).raw(",\"low\":").raw(!isnan(v) && v > 1 && v < PACK_LOW_V ? "true" : "false");
  j.raw(",\"sd\":").str(sdstore::status()).raw(",\"dbsrc\":").str(irdb::sourceName()).raw(",\"own\":").num(irdb::numUserProfiles());
  j.raw(",\"db\":").raw(sdstore::databaseLoaded() ? "true" : "false").raw(",\"card\":").raw(sdstore::mounted() ? "true" : "false");
  j.raw(",\"sta_status\":").str(wifimgr::statusText()).raw(",\"sta_attempts\":").num(wifimgr::attempts());
  irtx::LastSent l = irtx::last();
  j.raw(",\"last\":{\"label\":").str(l.label).raw(",\"proto\":").str(irProtoName(l.proto));
  j.raw(",\"d\":").num(l.d).raw(",\"s\":").num(l.s).raw(",\"f\":").num(l.f);
  j.raw(",\"frames\":").num(l.frames).raw(",\"seq\":").num(l.seq).raw("}");
  j.raw(",\"counts\":[").num(irdb::numBrands()).raw(",").num(irdb::numProfiles()).raw(",").num(irdb::numCodes()).raw("]}");
}

void hMeta() {
  Json j;
  j.raw("{\"keys\":[");
  for (int k = 0; k < KEY__COUNT; k++) {
    if (k) j.raw(",");
    j.raw("[").str(IR_KEY_IDS[k]).raw(",").str(IR_KEY_LABELS[k]).raw("]");
  }
  j.raw("],\"cats\":[");
  for (int c = 0; c < IR_NUM_CATS; c++) { if (c) j.raw(","); j.str(IR_CAT_NAMES[c]); }
  j.raw("],\"protos\":[");
  // names accepted by irParseManual(), in IRP terms
  static const char *const manual[] = {
    "NEC1", "NEC2", "NECx1", "Pioneer", "NEC1-y1", "NEC1-y2", "NEC1-y3", "NEC-raw", "Aiwa", "Panasonic", "Denon-K",
    "JVC-48", "Kaseikyo", "Teac-K", "Sony12", "Sony15", "Sony20", "RC5", "RC5x", "RC6", "Denon", "Denon{1}",
    "Denon{2}", "Sharp", "JVC", "JVC{2}", "Mitsubishi", "RECS80", "Bose", "RCA", "RCA-38", "RCA-38(Old)", "Akai", "Emerson",
  };
  for (size_t i = 0; i < sizeof(manual) / sizeof(manual[0]); i++) { if (i) j.raw(","); j.str(manual[i]); }
  j.raw("]}");
}

void hBrands() {
  Json j;
  j.raw("[");
  for (int b = 0; b < irdb::numBrands(); b++) {
    if (b) j.raw(",");
    j.raw("[").str(irdb::brandName(b)).raw(",").num(irdb::brandProfileCount(b)).raw(",").num(irdb::brandFirstProfile(b)).raw("]");
  }
  j.raw("]");
}

void hProfiles() {
  int b = argInt("b", -1);
  if (b < 0 || b >= irdb::numBrands()) return badRequest("bad brand");
  Json j;
  j.raw("[");
  int p0 = irdb::brandFirstProfile(b), n = irdb::brandProfileCount(b);
  for (int i = 0; i < n; i++) {
    int p = p0 + i;
    if (i) j.raw(",");
    j.raw("[").num(p).raw(",").str(irdb::profileName(p)).raw(",").num(irdb::profileCategory(p)).raw(",")
      .num(irdb::profileCodeCount(p)).raw(",").num(irdb::profileFromIrdb(p) ? 1 : 0).raw("]");
  }
  j.raw("]");
}

void hCodes() {
  int p = argInt("p", -1);
  if (p < 0 || p >= irdb::numProfiles()) return badRequest("bad profile");
  Json j;
  j.raw("{\"p\":").num(p).raw(",\"brand\":").str(irdb::brandName(irdb::profileBrand(p)));
  j.raw(",\"name\":").str(irdb::profileName(p)).raw(",\"cat\":").str(IR_CAT_NAMES[irdb::profileCategory(p)]);
  j.raw(",\"codes\":[");
  int c0 = irdb::profileFirstCode(p), n = irdb::profileCodeCount(p);
  for (int i = 0; i < n; i++) {
    const IrCode &c = irdb::code(c0 + i);
    if (i) j.raw(",");
    j.raw("[").num(c0 + i).raw(",").str(irdb::codeName(c0 + i)).raw(",").num(c.key).raw(",").str(irProtoName(c.proto)).raw("]");
  }
  j.raw("]}");
}

// a = tap | down | ka | up
void doAction(const String &a, int code, const IrCodeParams *params, const char *label) {
  if (a == "ka") { irtx::holdKeepAlive(); return; }
  if (a == "up") { irtx::holdStop(); return; }
  bool hold = (a == "down");
  if (params) { if (hold) irtx::holdStartParams(*params, label); else irtx::tapParams(*params, label); }
  else        { if (hold) irtx::holdStart(code); else irtx::tap(code); }
}

void hSend() {
  String a = server.hasArg("a") ? server.arg("a") : String("tap");
  int c = argInt("c", -1);
  if ((a == "tap" || a == "down") && (c < 0 || c >= irdb::numCodes())) return badRequest("bad code");
  doAction(a, c, nullptr, nullptr);
  server.send(200, "text/plain", "ok");
}

void hManual() {
  String a = server.hasArg("a") ? server.arg("a") : String("tap");
  if (a == "ka" || a == "up") { doAction(a, -1, nullptr, nullptr); server.send(200, "text/plain", "ok"); return; }
  String proto = server.arg("proto");
  IrCodeParams p;
  long d = argInt("d", 0), s = argInt("s", -1), f = argInt("f", 0);
  if (!irParseManual(proto.c_str(), d, s, f, p)) return badRequest("unknown protocol");
  char label[64];
  snprintf(label, sizeof(label), "Manual %s D=%ld S=%ld F=%ld", proto.c_str(), d, s, f);
  doAction(a, -1, &p, label);
  server.send(200, "text/plain", "ok");
}

void hScan() {
  int b = argInt("b", -1);
  int k = argInt("k", KEY_POWER);
  if (k <= 0 || k >= KEY__COUNT) return badRequest("bad key");
  // one entry per distinct signal (see irdb::buildScan); worst case one per remote
  const int MAX = irdb::numProfiles();
  irdb::ScanCand *cands = new (std::nothrow) irdb::ScanCand[MAX];
  if (!cands) return badRequest("out of memory");
  irdb::ScanFilter f;
  f.brand = b;
  f.group = argInt("g", -1);
  f.category = argInt("t", -1);
  int n = irdb::buildScan(f, (uint8_t)k, cands, MAX);
  Json j;
  j.raw("[");
  for (int i = 0; i < n; i++) {
    int c = cands[i].code, p = cands[i].profile;
    if (i) j.raw(",");
    j.raw("[").num(c).raw(",").num(p).raw(",").str(irdb::brandName(irdb::profileBrand(p))).raw(",")
      .str(irdb::profileName(p)).raw(",").str(irdb::codeName(c)).raw(",").num(cands[i].shared).raw("]");
  }
  j.raw("]");
  delete[] cands;
}

void hGroups() {
  Json j;
  j.raw("[");
  for (int g = 0; g < irdb::numGroups(); g++) {
    irdb::ScanFilter f;
    f.group = g;
    if (g) j.raw(",");
    j.raw("[").str(irdb::groupName(g)).raw(",").num(irdb::groupBrandCount(g)).raw(",").num(irdb::countProfiles(f)).raw("]");
  }
  j.raw("]");
}

void hFav() {
  String op = server.arg("op");
  if (op == "clear") {                            // remove every saved device
    settings::clearFavs();
    ui::requestRedraw();
    return server.send(200, "text/plain", "ok");
  }
  int p = argInt("p", -1);
  if (p < 0 || p >= irdb::numProfiles()) return badRequest("bad profile");
  if (op == "add") { if (!settings::addFav(p)) return badRequest("list full"); }
  else if (op == "del") settings::removeFav(p);
  else if (op == "active") settings::setActive(p);
  else return badRequest("bad op");
  ui::requestRedraw();
  server.send(200, "text/plain", "ok");
}

// ---- uploads to the SD card (streamed: the database is ~6 MB, far more than RAM) ----
char up_err[128] = "";
bool up_ok = false;

void hUploadData() {
  HTTPUpload &u = server.upload();
  power::activity();
  if (u.status == UPLOAD_FILE_START) {
    bool db = server.arg("kind") == "db";
    up_err[0] = 0;
    up_ok = sdstore::uploadStart(db, server.arg("path").c_str(), up_err, sizeof(up_err));
    logf("Upload %s: %s\n", db ? "database" : server.arg("path").c_str(), up_ok ? "started" : up_err);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (up_ok && !sdstore::uploadWrite(u.buf, u.currentSize)) { up_ok = false; snprintf(up_err, sizeof(up_err), "write error (card full?)"); }
  } else if (u.status == UPLOAD_FILE_END) {
    if (up_ok) up_ok = sdstore::uploadFinish(up_err, sizeof(up_err));
    else sdstore::uploadAbort();
    logf("Upload done: %s\n", up_err);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    sdstore::uploadAbort();
    up_ok = false;
    snprintf(up_err, sizeof(up_err), "upload interrupted");
  }
}

void hUploadDone() {
  server.send(up_ok ? 200 : 400, "text/plain", up_err[0] ? up_err : (up_ok ? "ok" : "upload failed"));
}

void hPower() {
  if (server.hasArg("l")) settings::setLedPower(server.arg("l").toInt());
  ui::requestRedraw();
  server.send(200, "text/plain", String(settings::ledPower()));
}

void hRestart() {
  server.send(200, "text/plain", "restarting - the page reloads in a few seconds");
  restart_at = millis() + 500;
  if (!restart_at) restart_at = 1;
}

void updateUiNet() {
  String sta = wifimgr::ip();
  ui::setNetworkInfo(wifimgr::apIp().c_str(), sta.c_str(), sta.length() ? wifimgr::ssid().c_str() : "");
}

}  // namespace

void begin() {
  wifimgr::begin();

  // every request counts as activity: the remote stays awake while the web remote is in use
  auto on = [](const char *uri, void (*h)()) { server.on(uri, [h] { power::activity(); h(); }); };
  on("/", hRoot);
  on("/api/state", hState);
  on("/api/meta", hMeta);
  on("/api/brands", hBrands);
  on("/api/profiles", hProfiles);
  on("/api/codes", hCodes);
  on("/api/send", hSend);
  on("/api/manual", hManual);
  on("/api/scan", hScan);
  on("/api/groups", hGroups);
  on("/api/fav", hFav);
  on("/api/power", hPower);
  on("/api/restart", hRestart);
  server.on("/api/upload", HTTP_POST, hUploadDone, hUploadData);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();

  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);
  updateUiNet();
}

void loop() {
  if (restart_at && (int32_t)(millis() - restart_at) >= 0) ESP.restart();
  // Wi-Fi was off while the remote slept: announce the name again once it is back
  if (wifimgr::suspended()) { wifi_was_suspended = true; return; }
  if (wifi_was_suspended) {
    wifi_was_suspended = false;
    MDNS.end();
    if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);
  }
  server.handleClient();
  wifimgr::loop();
  if (wifimgr::changeCounter() != net_changes_seen) {
    net_changes_seen = wifimgr::changeCounter();
    updateUiNet();
  }
}

}  // namespace web
