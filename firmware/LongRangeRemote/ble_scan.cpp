// ble_scan.cpp - the list of devices heard (see ble_scan.h). No Arduino dependencies.
#include "ble_scan.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <algorithm>

namespace blescan {
namespace {
Device g_dev[MAX_DEVICES];
int g_n = 0;
bool (*g_oui)(const uint8_t mac[6], char *out, size_t n) = nullptr;

// Keep what an earlier advert told us when this one doesn't say it (names usually come only in the
// scan response, which isn't received every time).
void merge(bleid::Adv &now, const bleid::Adv &before) {
  if (!now.name[0]) memcpy(now.name, before.name, sizeof(now.name));
  if (!now.has_tx && before.has_tx) { now.has_tx = true; now.tx_power = before.tx_power; }
  if (!now.has_app && before.has_app) { now.has_app = true; now.appearance = before.appearance; }
  if (!now.has_flags && before.has_flags) { now.has_flags = true; now.flags = before.flags; }
  if (!now.has_mfr && before.has_mfr) {
    now.has_mfr = true; now.company = before.company; now.mfr_len = before.mfr_len;
    memcpy(now.mfr, before.mfr, sizeof(now.mfr));
  }
  for (int i = 0; i < before.n_uuid16; i++) {
    bool have = false;
    for (int k = 0; k < now.n_uuid16; k++) if (now.uuid16[k] == before.uuid16[i]) have = true;
    if (!have && now.n_uuid16 < (int)(sizeof(now.uuid16) / sizeof(now.uuid16[0]))) now.uuid16[now.n_uuid16++] = before.uuid16[i];
  }
  if (!now.n_sdata && before.n_sdata) { now.n_sdata = before.n_sdata; memcpy(now.sdata, before.sdata, sizeof(now.sdata)); }
  if (!now.n_uuid128) now.n_uuid128 = before.n_uuid128;
}
}  // namespace

int find(const uint8_t mac[6]) {
  for (int i = 0; i < g_n; i++) if (!memcmp(g_dev[i].mac, mac, 6)) return i;
  return -1;
}

void feed(const uint8_t mac[6], int addr_type, int rssi, const uint8_t *p, int len, uint32_t now) {
  if (len < 0) len = 0;
  if (len > (int)sizeof(g_dev[0].raw)) len = sizeof(g_dev[0].raw);
  int i = find(mac);
  bool fresh = i < 0;
  if (fresh) {
    if (g_n < MAX_DEVICES) i = g_n++;
    else {                                   // full: replace the one unheard for longest
      i = 0;
      for (int k = 1; k < g_n; k++) if ((int32_t)(g_dev[k].last_ms - g_dev[i].last_ms) < 0) i = k;
    }
    Device &d = g_dev[i];
    memset(&d, 0, sizeof(d));
    memcpy(d.mac, mac, 6);
    d.first_ms = now;
    d.rssi_max = -127;
    d.rssi_avg = rssi;
  }
  Device &d = g_dev[i];
  d.addr_type = addr_type;
  d.kind = bleid::addrKind(addr_type, mac);
  d.rssi = rssi;
  if (rssi > d.rssi_max) d.rssi_max = rssi;
  d.rssi_avg += (rssi - d.rssi_avg) * 0.25f;
  d.last_ms = now;
  d.count++;
  bool changed = fresh || len != d.raw_len || memcmp(p, d.raw, len) != 0;
  if (!changed) return;                      // same advert as before: nothing new to work out
  memcpy(d.raw, p, len);
  d.raw_len = len;
  bleid::Adv a;
  bleid::parse(p, len, a);
  if (!fresh) merge(a, d.adv);
  d.adv = a;
  if (fresh && d.kind == bleid::ADDR_PUBLIC && g_oui && !g_oui(mac, d.oui, sizeof(d.oui))) d.oui[0] = 0;
  bleid::identify(d.adv, d.kind, d.oui, d.id);
}

int count() { return g_n; }
const Device &device(int i) { return g_dev[i]; }
void clear() { g_n = 0; }
void setOuiLookup(bool (*fn)(const uint8_t mac[6], char *out, size_t n)) { g_oui = fn; }

void refreshOui() {
  for (int i = 0; i < g_n; i++) {
    Device &d = g_dev[i];
    if (d.kind != bleid::ADDR_PUBLIC) continue;
    if (!g_oui || !g_oui(d.mac, d.oui, sizeof(d.oui))) d.oui[0] = 0;
    bleid::identify(d.adv, d.kind, d.oui, d.id);
  }
}

const char *sortName(int s) {
  switch (s) { case SORT_NAME: return "name"; case SORT_NEWEST: return "newest"; default: return "signal"; }
}

void label(const Device &d, char *out, size_t n) {
  if (d.adv.name[0]) snprintf(out, n, "%s", d.adv.name);
  else if (d.id.detail[0]) snprintf(out, n, "%s", d.id.detail);
  else if (d.id.brand[0] && strcmp(d.id.type, "Unknown")) snprintf(out, n, "%s %s", d.id.brand, d.id.type);
  else if (d.id.brand[0]) snprintf(out, n, "%s device", d.id.brand);
  else if (strcmp(d.id.type, "Unknown")) snprintf(out, n, "%s", d.id.type);
  else snprintf(out, n, "Unknown device");
}

void macText(const uint8_t m[6], char *out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

int bars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  if (rssi >= -88) return 1;
  return 0;
}

int sorted(int *idx, int max, int sort, uint32_t now) {
  int n = std::min(g_n, max);
  int all[MAX_DEVICES];
  for (int i = 0; i < g_n; i++) all[i] = i;
  auto stale = [now](const Device &d) { return now - d.last_ms > STALE_MS; };
  std::stable_sort(all, all + g_n, [&](int x, int y) {
    const Device &a = g_dev[x], &b = g_dev[y];
    bool sa = stale(a), sb = stale(b);
    if (sa != sb) return !sa;                                   // heard recently first
    switch (sort) {
      case SORT_NAME: {
        char la[48], lb[48];
        label(a, la, sizeof(la)); label(b, lb, sizeof(lb));
        int c = strcasecmp(la, lb);
        if (c) return c < 0;
        return a.rssi_avg > b.rssi_avg;
      }
      case SORT_NEWEST: return (int32_t)(a.first_ms - b.first_ms) > 0;
      default: return a.rssi_avg > b.rssi_avg;
    }
  });
  for (int i = 0; i < n; i++) idx[i] = all[i];
  return n;
}

}  // namespace blescan
