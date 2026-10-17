// ui.cpp - on-device UI for the Long-Range Remote universal remote.
//
// Controls: the six keys on the control board under the screen (direct GPIOs, see hw.h): the D-pad,
// SELECT, and BACK at the D-pad's lower right.
//
//   BACK                               one screen back, everywhere (the older ways below still work).
//                                      Remote screen: nothing. Nav pad: sends the device's own
//                                      Back / Return, held = leave. Vol pad: leave.
//   Lists (menus, brands, models...)   UP/DOWN move (hold = fast), RIGHT or SELECT = choose,
//                                      LEFT or SELECT held = back
//   Remote screen (grid of keys)       arrows move, SELECT press = send (held = repeat, e.g. volume)
//   Letter grid                        arrows move, SELECT = choose, SELECT held = back
//   Nav pad / Vol pad                  the arrows and SELECT become the device's own keys:
//                                        Nav pad: Up/Down/Left/Right + OK
//                                        Vol pad: Vol +/- , Channel -/+ , Mute
//                                      hold SELECT to leave
//   Channel number ("123" tile)        UP/DOWN change the digit, RIGHT adds a digit, LEFT deletes,
//                                      SELECT sends the number (digit keys, paced like a person
//                                      typing), hold SELECT to leave
//   Code search                        RIGHT/DOWN next code, UP/LEFT previous code (each sends),
//                                      SELECT = send again, SELECT held = "it worked, use it",
//                                      LEFT held = stop searching
//   Keyboard (Wi-Fi name / password)   arrows move, SELECT types the key (held = repeat),
//                                      BACK deletes the last character (on an empty line: leave),
//                                      BACK held = leave without saving, "Done" = save
//   Menu > Sound row                   LEFT/RIGHT = quieter/louder, SELECT = mute / unmute
//
// Sounds (speaker on the display board, sound.h): a click on every key press, a lower two-note
// sound when holding a key leaves a screen or picks something, and a buzz when a code can't be sent.
//
// Screens:
//   REMOTE  - grid of the active device's standard keys (Power, Vol, Channel, Inputs, ...)
//   PAD     - Nav pad / Vol pad (arrows = device keys, for aiming the remote and pressing keys blind)
//   NUMBER  - type a channel number and send it
//   ALLKEYS - every button of the active device
//   MENU    - your devices, browse, code search, LED power, sound, Wi-Fi, status
//   WIFI -> WIFINETS (nearby networks) -> TEXT (keyboard)   home Wi-Fi, saved to wifi.txt on the card
//   LETTERS -> BRANDS -> PROFILES  (browse the database)
//   SCANSRC -> (LETTERS -> BRANDS) -> SCANCAT -> SCANKEY -> SCAN   (code search)
//   STATUS  - battery, SD card, database, Wi-Fi
#include "ui.h"
#include "config.h"
#include "log.h"

#include "hw.h"
#include "ir_db.h"
#include "ir_keys.h"
#include "ir_tx.h"
#include "sd_store.h"
#include "settings.h"
#include "sound.h"
#include "wifi_file.h"
#include "wifi_mgr.h"
#include "ST7305_U8g2.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <math.h>

namespace ui {
namespace {

const int W = 400, H = 300;
const int HEADER_H = 30, FOOTER_H = 34;

ST7305_U8g2 lcd(LCD_SCK_PIN, LCD_MOSI_PIN, LCD_DC_PIN, LCD_CS_PIN, LCD_RST_PIN);
U8G2 *g = nullptr;
bool dirty = true;

char ap_ip[20] = "192.168.4.1", sta_ip[20] = "", sta_ssid[33] = "";

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------
enum KeyId { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_SELECT, K_BACK, K__N };
const uint8_t KEY_BITS[K__N] = {KEYBIT_UP, KEYBIT_DOWN, KEYBIT_LEFT, KEYBIT_RIGHT, KEYBIT_SELECT, KEYBIT_BACK};

// What happened to a key:
//   A_PRESS    debounced press
//   A_RELEASE  release after a short press (no A_LONG fired)
//   A_LONG     held past LONG_MS (once)
//   A_REPEAT   auto-repeat while held past REPEAT_DELAY_MS (for moving through lists)
//   A_UP       any release (also after A_LONG) - for stopping a held IR send
enum Act { A_PRESS, A_RELEASE, A_LONG, A_REPEAT, A_UP };

const uint32_t DEBOUNCE_MS = 20, LONG_MS = 700, REPEAT_DELAY_MS = 400;

struct Key {
  bool down = false;           // debounced state
  bool raw_last = false;
  uint32_t changed_at = 0;
  uint32_t down_at = 0;
  uint32_t last_repeat = 0;
  bool long_fired = false;
  bool swallow = false;        // held across a screen change: ignore until released
};
Key keys[K__N];

// ---------------------------------------------------------------------------
// Screen state
// ---------------------------------------------------------------------------
enum Screen { S_REMOTE, S_PAD, S_NUMBER, S_ALLKEYS, S_MENU, S_LETTERS, S_BRANDS, S_PROFILES,
              S_SCANSRC, S_SCANCAT, S_SCANKEY, S_SCAN, S_STATUS, S_WIFI, S_WIFINETS, S_TEXT };
Screen screen = S_REMOTE;
int sel = 0, scroll = 0;              // selection + first visible item/row of the current screen
int last_active = -2;
uint32_t last_seq = 0, last_frames = 0;
uint32_t last_status_draw = 0;

// Browse state
char cur_letter = 0;                  // brand filter letter ('#' = digits/other)
int cur_brand = -1;
int scan_group = -1;                  // code search over a brand group (-1 = none)
int scan_cat = -1;                    // code search restricted to one device type (-1 = all)
bool browse_for_scan = false;
int *brand_list = nullptr; int n_brand_list = 0;     // sized numBrands()+1 at begin()
char letters[40]; int n_letters = 0;
int cat_list[IR_NUM_CATS + 1]; int cat_count[IR_NUM_CATS + 1]; int n_cat_list = 0;

// Remote grid items: >= 0 canonical key id (code resolved per profile), special tiles < 0
const int TILE_MENU = -1, TILE_ALL = -2, TILE_NAVPAD = -3, TILE_VOLPAD = -4, TILE_NUMBER = -5;
bool asleep = false;

// Channel number entry: digits typed so far + the one being chosen, and the send queue
char num_digits[6] = ""; int num_len = 0; int num_cur = 0;
char num_send[6] = ""; int num_send_pos = 0; uint32_t num_next_at = 0;
const uint32_t DIGIT_GAP_MS = 350;      // between digit keys (TVs ignore digits sent too quickly)
int grid[KEY__COUNT + 8]; int n_grid = 0;
const int GRID_COLS = 4, GRID_ROWS = 5;

// Order of the standard keys on the remote screen: the everyday TV / receiver keys first.
const uint8_t KEY_ORDER[] = {
  KEY_POWER, KEY_POWER_ON, KEY_POWER_OFF, KEY_VOL_UP, KEY_VOL_DOWN, KEY_MUTE, KEY_CH_UP, KEY_CH_DOWN,
  KEY_PREV_CH, KEY_INPUT, KEY_HOME, KEY_MENU, KEY_BACK, KEY_GUIDE, KEY_INFO, KEY_UP, KEY_DOWN, KEY_LEFT,
  KEY_RIGHT, KEY_OK, KEY_APPS, KEY_NETFLIX, KEY_YOUTUBE, KEY_RED, KEY_GREEN, KEY_YELLOW, KEY_BLUE,
  KEY_PLAY, KEY_PAUSE, KEY_PLAY_PAUSE, KEY_STOP, KEY_PREV, KEY_NEXT, KEY_REW, KEY_FFWD, KEY_RECORD,
  KEY_EJECT, KEY_TOP_MENU, KEY_REPEAT, KEY_SHUFFLE, KEY_SUBTITLE, KEY_AUDIO_LANG, KEY_TEXT, KEY_LIST,
  KEY_FAV, KEY_ASPECT, KEY_PIC_MODE, KEY_SOUND_MODE,
  KEY_IN_HDMI, KEY_IN_HDMI2, KEY_IN_HDMI3, KEY_IN_HDMI4, KEY_IN_TV, KEY_IN_BD, KEY_IN_GAME, KEY_IN_CD,
  KEY_IN_TUNER, KEY_IN_PHONO, KEY_IN_AUX, KEY_IN_OPTICAL, KEY_IN_COAX, KEY_IN_USB, KEY_IN_BT, KEY_IN_NET,
  KEY_IN_TAPE,
  KEY_SETUP, KEY_DIMMER, KEY_SLEEP, KEY_BASS_UP, KEY_BASS_DOWN, KEY_TREBLE_UP, KEY_TREBLE_DOWN,
  KEY_SUB_UP, KEY_SUB_DOWN, KEY_TUNE_UP, KEY_TUNE_DOWN, KEY_PRESET_UP, KEY_PRESET_DOWN,
  KEY_SPEED_UP, KEY_SPEED_DOWN, KEY_TEMP_UP, KEY_TEMP_DOWN, KEY_OSCILLATE, KEY_TIMER,
  KEY_D1, KEY_D2, KEY_D3, KEY_D4, KEY_D5, KEY_D6, KEY_D7, KEY_D8, KEY_D9, KEY_D0,
};

// Pads: what each control-board key sends. Each entry lists candidates, the first one the device
// has is used (KEY_NONE ends a list). Index = KeyId. BACK with nothing to send leaves the pad.
struct Pad { const char *name; uint8_t keys[K__N][4]; };
const Pad PADS[2] = {
  {"Nav pad", {{KEY_UP}, {KEY_DOWN}, {KEY_LEFT}, {KEY_RIGHT}, {KEY_OK, KEY_PLAY_PAUSE}, {KEY_BACK}}},
  {"Vol pad", {{KEY_VOL_UP, KEY_SPEED_UP, KEY_TEMP_UP},
               {KEY_VOL_DOWN, KEY_SPEED_DOWN, KEY_TEMP_DOWN},
               {KEY_CH_DOWN, KEY_PRESET_DOWN, KEY_TUNE_DOWN, KEY_PREV},
               {KEY_CH_UP, KEY_PRESET_UP, KEY_TUNE_UP, KEY_NEXT},
               {KEY_MUTE, KEY_PLAY_PAUSE, KEY_POWER},
               {KEY_NONE}}},
};
int cur_pad = 0;

// Code search
const uint8_t SCAN_KEYS[] = {KEY_POWER, KEY_POWER_ON, KEY_VOL_UP, KEY_MUTE, KEY_CH_UP, KEY_PLAY, KEY_INPUT, KEY_MENU};
const int N_SCAN_KEYS = sizeof(SCAN_KEYS);
uint8_t scan_key = KEY_POWER;
irdb::ScanCand *scan_cands = nullptr; int scan_cap = 0;   // sized numProfiles() at begin() (PSRAM)
int n_scan = 0; int scan_pos = 0;

// Menu entries
enum MenuItem { M_BACK = -100, M_BROWSE, M_SCAN, M_REMOVE, M_POWER, M_SOUND, M_WIFI, M_STATUS };
int menu_items[MAX_FAVOURITES + 10]; int n_menu = 0;

// Currently sending (a key held on a remote / pad screen)
bool send_held = false;

// Wi-Fi menu. Rows of the Wi-Fi screen (PASSWORD and FORGET only when a network is set):
enum WifiRow { W_BACK, W_NETWORK, W_PASSWORD, W_FORGET };
const int MAX_NET_LIST = 20;
wifimgr::Network net_list[MAX_NET_LIST];
int n_net_list = -1;                  // -1 = scan still running
bool forget_armed = false;            // "Forget" needs a second SELECT
char wifi_msg[100] = "";              // result of the last change, shown on the Wi-Fi screen
String pending_ssid;                  // network whose password is being typed
bool pending_secured = false;         // it is known to need a password (seen in the scan, not open)

// Keyboard for the network name / password. Rows 0-3 are characters (per page), row 4 the actions.
enum TextFor { T_SSID, T_PASS };
TextFor text_for = T_PASS;
char text_buf[65] = ""; int text_len = 0, text_max = 63;
char text_err[80] = "";
bool text_empty_ok = false;           // "Done" on an empty password was pressed once: the next one saves
int kb_page = 0, kb_row = 1, kb_col = 0;
Screen text_return = S_WIFI; int text_return_sel = 0;
const char *const KB_ROWS[3][4] = {
  {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"},
  {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.@"},
  {"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~"},       // with the letters and digits: all of printable ASCII
};
const char *const KB_PAGE_LABEL[3] = {"ABC", "#+=", "abc"};    // the page the page key switches to
enum KbAction { KA_PAGE, KA_SPACE, KA_DEL, KA_DONE, KA__N };
const int KB_X0 = 5, KB_PITCH = 39, KB_KEY_W = 38, KB_Y0 = 94, KB_ROW_H = 31, KB_KEY_H = 28;
const int KB_ACT_X[KA__N] = {5, 89, 233, 317}, KB_ACT_W[KA__N] = {80, 140, 80, 78};

// Long names for device types (IR_CAT_NAMES are short ids)
const char *catLabel(int c) {
  static const char *const MAP[][2] = {
    {"TV", "TVs"}, {"AVR", "AV receivers"}, {"Soundbar", "Soundbars"}, {"Projector", "Projectors"},
    {"SetTop", "Set-top / cable / satellite"}, {"Streaming", "Streaming boxes"}, {"Disc", "Blu-ray / DVD"},
    {"VCR", "VCRs"}, {"Amp", "Amplifiers"}, {"CD", "CD players"}, {"Tuner", "Tuners"}, {"Tape", "Tape decks"},
    {"MD", "MiniDisc"}, {"System", "Music systems"}, {"Speaker", "Active speakers"}, {"Audio", "Other audio"},
    {"Console", "Game consoles"}, {"Monitor", "Monitors / displays"}, {"Climate", "Fans / heaters"},
    {"AC", "Air conditioners"}, {"Light", "Lighting"}, {"Car", "Car audio"}, {"Camera", "Cameras"},
    {"Other", "Other"}};
  if (c < 0 || c >= IR_NUM_CATS) return "?";
  for (auto &m : MAP) if (!strcmp(m[0], IR_CAT_NAMES[c])) return m[1];
  return IR_CAT_NAMES[c];
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
void drawClipped(int x, int y, int maxw, const char *s) {
  if (g->getStrWidth(s) <= maxw) { g->drawStr(x, y, s); return; }
  char buf[96];
  size_t n = strlen(s);
  if (n > sizeof(buf) - 3) n = sizeof(buf) - 3;
  memcpy(buf, s, n); buf[n] = 0;
  while (n > 0) {
    buf[n - 1] = 0; n--;
    char tmp[100];
    snprintf(tmp, sizeof(tmp), "%s..", buf);
    if (g->getStrWidth(tmp) <= maxw) { g->drawStr(x, y, tmp); return; }
  }
}

void drawCentered(int cx, int y, const char *s) { g->drawStr(cx - g->getStrWidth(s) / 2, y, s); }

// Centred line that must fit the screen: smaller font if needed, then clipped.
void drawCenteredFit(int y, const char *s) {
  if (g->getStrWidth(s) > W - 12) g->setFont(u8g2_font_helvR10_tr);
  if (g->getStrWidth(s) > W - 12) { drawClipped(6, y, W - 12, s); return; }
  drawCentered(W / 2, y, s);
}

bool batteryLow() {
  float v = hw::packVolts();
  return !isnan(v) && v > 1.0f && v < PACK_LOW_V;
}

void drawHeader(const char *title, const char *subtitle) {
  g->setDrawColor(1);
  g->drawBox(0, 0, W, HEADER_H);
  g->setDrawColor(0);
  g->setFont(u8g2_font_helvB14_tr);
  int x = 6;
  drawClipped(x, 21, 170, title);
  x += min(170, (int)g->getStrWidth(title)) + 10;
  if (subtitle && *subtitle) {
    g->setFont(u8g2_font_helvR10_tr);
    drawClipped(x, 20, 300 - x, subtitle);
  }
  // right side: battery + network
  g->setFont(u8g2_font_helvR08_tr);
  char r[40];
  int pct = hw::packPercent();
  const char *sp = *PACK_NAME ? " " : "";
  if (pct < 0) snprintf(r, sizeof(r), "AA out");
  else if (batteryLow()) snprintf(r, sizeof(r), "%s%sLOW %d%%", PACK_NAME, sp, pct);
  else snprintf(r, sizeof(r), "%s%s%d%%", PACK_NAME, sp, pct);
  int sc = hw::screenPercent();                 // the display's own 18650 runs the ESP32
  if (sc >= 0) snprintf(r + strlen(r), sizeof(r) - strlen(r), "  scr %d%%", sc);
  g->drawStr(W - 6 - g->getStrWidth(r), 13, r);
  const char *net = asleep ? "asleep" : sta_ip[0] ? "WiFi" : "AP";
  g->drawStr(W - 6 - g->getStrWidth(net), 26, net);
  g->setDrawColor(1);
}

void drawFooter(const char *line1, const char *hint) {
  g->drawHLine(0, H - FOOTER_H, W);
  g->setFont(u8g2_font_helvR10_tr);
  if (line1) drawClipped(6, H - FOOTER_H + 14, W - 12, line1);
  g->setFont(u8g2_font_helvR08_tr);
  if (hint) drawClipped(6, H - 5, W - 12, hint);
}

void lastSentLine(char *out, size_t n) {
  float pv = hw::packVolts();
  if (!isnan(pv) && pv < 1.0f) { snprintf(out, n, "LEDs off - no AA cells in"); return; }
  if (!isnan(pv) && pv < PACK_CUTOFF_V) { snprintf(out, n, PACK_FLAT_MSG); return; }
  if (batteryLow()) { snprintf(out, n, PACK_LOW_MSG); return; }
  irtx::LastSent l = irtx::last();
  if (!l.seq) { snprintf(out, n, "Nothing sent yet"); return; }
  if (l.proto == IRP_RAW)
    snprintf(out, n, "> %s  [raw %lukHz] x%lu", l.label, (unsigned long)(l.carrier_hz / 1000), (unsigned long)l.frames);
  else
    snprintf(out, n, "> %s  [%s %X.%X.%X] x%lu", l.label, irProtoName(l.proto), l.d, l.s, l.f, (unsigned long)l.frames);
}

// Generic list: items drawn by callback.
const int ROW_H = 26;
const int LIST_TOP = HEADER_H + 4;
const int LIST_ROWS = (H - FOOTER_H - LIST_TOP) / ROW_H;

void keepVisible(int n_items, int rows) {
  if (sel < 0) sel = n_items ? n_items - 1 : 0;
  if (sel >= n_items) sel = 0;
  if (sel < scroll) scroll = sel;
  if (sel >= scroll + rows) scroll = sel - rows + 1;
  if (scroll < 0) scroll = 0;
}

template <typename F>
void drawList(int n_items, F label) {
  keepVisible(n_items, LIST_ROWS);
  g->setFont(u8g2_font_helvB12_tr);
  for (int r = 0; r < LIST_ROWS && scroll + r < n_items; r++) {
    int i = scroll + r;
    int y = LIST_TOP + r * ROW_H;
    char buf[96];
    label(i, buf, sizeof(buf));
    if (i == sel) {
      g->drawRBox(2, y, W - 14, ROW_H - 2, 4);
      g->setDrawColor(0);
      drawClipped(10, y + 18, W - 30, buf);
      g->setDrawColor(1);
    } else {
      drawClipped(10, y + 18, W - 30, buf);
    }
  }
  if (n_items > LIST_ROWS) {                    // scrollbar
    int track = LIST_ROWS * ROW_H;
    int bar = max(12, track * LIST_ROWS / n_items);
    int y = LIST_TOP + (track - bar) * scroll / max(1, n_items - LIST_ROWS);
    g->drawFrame(W - 9, LIST_TOP, 6, track);
    g->drawBox(W - 9, y, 6, bar);
  }
}

// ---------------------------------------------------------------------------
// Model helpers
// ---------------------------------------------------------------------------
// First key of a pad slot that the active device has: code index, or -1.
int padCode(int pad, int slot, uint8_t *key_out = nullptr) {
  int p = settings::active();
  if (p < 0) return -1;
  for (uint8_t k : PADS[pad].keys[slot]) {
    if (k == KEY_NONE) break;
    int c = irdb::profileFindKey(p, k);
    if (c >= 0) { if (key_out) *key_out = k; return c; }
  }
  return -1;
}

int padUsable(int pad) {                         // number of the D-pad + SELECT slots the device can fill
  int n = 0;
  for (int s = 0; s <= K_SELECT; s++) if (padCode(pad, s) >= 0) n++;
  return n;
}

bool digitAvail(int d) {
  int p = settings::active();
  return p >= 0 && irdb::profileFindKey(p, (uint8_t)(KEY_D0 + d)) >= 0;
}

bool hasDigits() {                               // at least 9 of 0..9 (some files lack the 0)
  int n = 0;
  for (int d = 0; d <= 9; d++) n += digitAvail(d);
  return n >= 9;
}

// Next digit the device has, going up (dir = 1) or down (dir = -1) from d.
int stepDigit(int d, int dir) {
  for (int i = 0; i < 10; i++) {
    d = (d + 10 + dir) % 10;
    if (digitAvail(d)) return d;
  }
  return d;
}

void buildGrid() {
  n_grid = 0;
  grid[n_grid++] = TILE_MENU;
  int p = settings::active();
  if (p < 0) return;
  // The pads are only offered when they would do something useful (at least 3 of 5 keys).
  if (padUsable(1) >= 3) grid[n_grid++] = TILE_VOLPAD;
  if (padUsable(0) >= 4) grid[n_grid++] = TILE_NAVPAD;
  if (hasDigits()) grid[n_grid++] = TILE_NUMBER;
  for (uint8_t k : KEY_ORDER)
    if (irdb::profileFindKey(p, k) >= 0 && n_grid < (int)(sizeof(grid) / sizeof(grid[0])) - 1) grid[n_grid++] = k;
  grid[n_grid++] = TILE_ALL;
}

void buildMenu() {
  n_menu = 0;
  menu_items[n_menu++] = M_BACK;
  for (int i = 0; i < settings::favCount(); i++) menu_items[n_menu++] = settings::fav(i);
  menu_items[n_menu++] = M_BROWSE;
  menu_items[n_menu++] = M_SCAN;
  if (settings::active() >= 0) menu_items[n_menu++] = M_REMOVE;
  menu_items[n_menu++] = M_POWER;
  menu_items[n_menu++] = M_SOUND;
  menu_items[n_menu++] = M_WIFI;
  menu_items[n_menu++] = M_STATUS;
}

int menuIndex(int item) {
  for (int i = 0; i < n_menu; i++) if (menu_items[i] == item) return i;
  return 0;
}

char letterOf(const char *name) {
  char c = toupper((unsigned char)name[0]);
  return (c >= 'A' && c <= 'Z') ? c : '#';
}

void buildLetters() {
  n_letters = 0;
  letters[n_letters++] = '<';                  // back
  for (int b = 0; b < irdb::numBrands() && n_letters < (int)sizeof(letters); b++) {
    char c = letterOf(irdb::brandName(b));
    if (!memchr(letters, c, n_letters)) letters[n_letters++] = c;
  }
  std::sort(letters + 1, letters + n_letters);
}

void buildBrands() {
  n_brand_list = 0;
  if (!brand_list) return;
  brand_list[n_brand_list++] = -1;             // back
  for (int b = 0; b < irdb::numBrands(); b++)
    if (letterOf(irdb::brandName(b)) == cur_letter) brand_list[n_brand_list++] = b;
}

irdb::ScanFilter scanFilter(int cat) {
  irdb::ScanFilter f;
  f.brand = cur_brand;
  f.group = cur_brand >= 0 ? -1 : scan_group;
  f.category = cat;
  return f;
}

// Device types that have remotes in the current search scope (+ "All types" first, after < Back)
void buildCats() {
  n_cat_list = 0;
  cat_list[n_cat_list] = -1; cat_count[n_cat_list++] = irdb::countProfiles(scanFilter(-1));
  for (int c = 0; c < IR_NUM_CATS; c++) {
    int n = irdb::countProfiles(scanFilter(c));
    if (n > 0) { cat_list[n_cat_list] = c; cat_count[n_cat_list++] = n; }
  }
}

void buildScan() {
  scan_pos = 0;
  n_scan = scan_cands ? irdb::buildScan(scanFilter(scan_cat), scan_key, scan_cands, scan_cap) : 0;
}

void stopSend() {
  if (send_held) { irtx::holdStop(); send_held = false; }
}

void go(Screen s, int new_sel = 0) {
  screen = s; sel = new_sel; scroll = 0; dirty = true;
  // A key that is still held while the screen changes must not also act on the new screen.
  for (Key &k : keys) if (k.down) k.swallow = true;
  stopSend();
  switch (s) {
    case S_REMOTE:  buildGrid(); if (sel >= n_grid) sel = 0; break;
    case S_MENU:    buildMenu(); break;
    case S_LETTERS: buildLetters(); break;
    case S_BRANDS:  buildBrands(); break;
    case S_SCANCAT: buildCats(); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Screen renderers
// ---------------------------------------------------------------------------
const char *tileLabel(int t) {
  switch (t) {
    case TILE_MENU:   return "Menu";
    case TILE_ALL:    return "All keys...";
    case TILE_NAVPAD: return "Nav pad";
    case TILE_VOLPAD: return "Vol pad";
    case TILE_NUMBER: return "Ch 123";
    default:          return IR_KEY_LABELS[t];
  }
}

void drawRemote() {
  int p = settings::active();
  char sub[64] = "";
  if (p >= 0) snprintf(sub, sizeof(sub), "%s", irdb::profileName(p));
  drawHeader(p >= 0 ? irdb::brandName(irdb::profileBrand(p)) : "Long-Range Remote", sub);

  int y_off = 0, rows_vis = GRID_ROWS;
  if (irdb::numProfiles() == 0) {                // no codes at all: say how to get them
    g->setFont(u8g2_font_helvB12_tr);
    g->drawStr(12, 62, "No IR codes loaded.");
    g->setFont(u8g2_font_helvR10_tr);
    drawClipped(12, 86, W - 24, sdstore::status());
    g->drawStr(12, 112, "Copy irdb.bin (firmware/sd_card) to the SD card,");
    g->drawStr(12, 130, "or upload it from a phone: join Wi-Fi \"" WIFI_AP_SSID "\"");
    char b[80]; snprintf(b, sizeof(b), "(password " WIFI_AP_PASSWORD "), open http://%s,", sta_ip[0] ? sta_ip : ap_ip);
    g->drawStr(12, 148, b);
    g->drawStr(12, 166, "Settings > SD card > Upload code database.");
    y_off = 170; rows_vis = 1;
  } else if (p < 0) {
    g->setFont(u8g2_font_helvB12_tr);
    g->drawStr(12, 62, "No device selected yet.");
    g->setFont(u8g2_font_helvR10_tr);
    g->drawStr(12, 88, "On the remote: SELECT on Menu, then Browse");
    g->drawStr(12, 106, "brands or Code search.");
    g->drawStr(12, 132, "Or on a phone: join Wi-Fi \"" WIFI_AP_SSID "\"");
    char b[80]; snprintf(b, sizeof(b), "(password " WIFI_AP_PASSWORD "), open http://%s", sta_ip[0] ? sta_ip : ap_ip);
    g->drawStr(12, 150, b);
    y_off = 170; rows_vis = 1;
  }
  const int TW = 97, TH = 42, X0 = 3, Y0 = HEADER_H + 5;
  if (sel >= n_grid) sel = 0;
  if (sel < 0) sel = n_grid - 1;
  int row = sel / GRID_COLS;
  if (row < scroll) scroll = row;
  if (row >= scroll + rows_vis) scroll = row - rows_vis + 1;
  for (int i = scroll * GRID_COLS; i < n_grid && i < (scroll + rows_vis) * GRID_COLS; i++) {
    int r = i / GRID_COLS - scroll, c = i % GRID_COLS;
    int x = X0 + c * (TW + 2), y = Y0 + y_off + r * (TH + 3);
    const char *label = tileLabel(grid[i]);
    bool special = grid[i] < 0;
    if (i == sel) {
      g->drawRBox(x, y, TW, TH, 6);
      g->setDrawColor(0);
    } else {
      g->drawRFrame(x, y, TW, TH, 6);
      if (special) g->drawRFrame(x + 2, y + 2, TW - 4, TH - 4, 5);
    }
    g->setFont(u8g2_font_helvB14_tr);
    if (g->getStrWidth(label) > TW - 8) g->setFont(u8g2_font_helvB10_tr);
    drawCentered(x + TW / 2, y + TH / 2 + 6, label);
    g->setDrawColor(1);
  }
  int total_rows = (n_grid + GRID_COLS - 1) / GRID_COLS;
  if (total_rows > GRID_ROWS && p >= 0) {
    char pg[48]; snprintf(pg, sizeof(pg), "rows %d-%d of %d", scroll + 1, min(total_rows, scroll + GRID_ROWS), total_rows);
    g->setFont(u8g2_font_helvR08_tr);
    g->drawStr(W - 4 - g->getStrWidth(pg), H - FOOTER_H - 2, pg);
  }
  char line[120]; lastSentLine(line, sizeof(line));
  drawFooter(line, "Arrows: move  |  SELECT: press = send, hold = repeat");
}

void drawPad() {
  const Pad &pad = PADS[cur_pad];
  int p = settings::active();
  drawHeader(pad.name, p >= 0 ? irdb::profileName(p) : "");
  // A picture of the control board's keys with what each one sends.
  // Layout matches the board: 4-way cross on the left, BACK at its lower right, big SELECT on the right.
  struct Rect { int x, y, w, h; } const rc[K__N] = {
    {110, 42, 84, 50}, {110, 200, 84, 50}, {16, 121, 84, 50}, {204, 121, 84, 50}, {300, 108, 92, 76},
    {204, 200, 84, 50}};
  for (int s = 0; s < K__N; s++) {
    uint8_t k = KEY_NONE;
    int c = padCode(cur_pad, s, &k);
    const char *lab = c >= 0 ? IR_KEY_LABELS[k] : (s == K_BACK ? "< exit" : "-");
    int x = rc[s].x, y = rc[s].y, w = rc[s].w, h = rc[s].h;
    bool active = keys[s].down && !keys[s].swallow;
    if (active) { g->drawRBox(x, y, w, h, 8); g->setDrawColor(0); }
    else g->drawRFrame(x, y, w, h, 8);
    g->setFont(u8g2_font_helvB14_tr);
    if (g->getStrWidth(lab) > w - 8) g->setFont(u8g2_font_helvB10_tr);
    drawCentered(x + w / 2, y + h / 2 + 6, lab);
    g->setDrawColor(1);
  }
  char line[120]; lastSentLine(line, sizeof(line));
  drawFooter(line, "Keys send these directly (hold = repeat)  |  hold BACK or SELECT: leave");
}

void drawNumber() {
  int p = settings::active();
  drawHeader("Channel", p >= 0 ? irdb::profileName(p) : "");
  // the number so far, the digit being chosen in a box
  g->setFont(u8g2_font_logisoso42_tn);
  char done[8]; snprintf(done, sizeof(done), "%s", num_digits);
  char cur[2] = {(char)('0' + num_cur), 0};
  int wd = g->getStrWidth(done), wc = g->getStrWidth("0");
  int x = W / 2 - (wd + wc + 8) / 2, y = 130;
  g->drawStr(x, y, done);
  int cx = x + wd + 4;
  g->drawRFrame(cx - 4, y - 50, wc + 8, 60, 6);
  g->drawStr(cx, y, cur);
  g->setFont(u8g2_font_helvR10_tr);
  drawCentered(W / 2, 170, "UP / DOWN: change digit    RIGHT: next digit    LEFT: delete");
  drawCentered(W / 2, 190, "SELECT: send the number    BACK: back");
  if (num_send[0]) {
    char b[48]; snprintf(b, sizeof(b), "sending %s ...", num_send);
    g->setFont(u8g2_font_helvB12_tr);
    drawCentered(W / 2, 222, b);
  }
  char line[120]; lastSentLine(line, sizeof(line));
  drawFooter(line, "Sends the digit keys one after another, like typing on the TV remote");
}

void drawAllKeys() {
  int p = settings::active();
  drawHeader("All keys", irdb::profileName(p));
  int c0 = irdb::profileFirstCode(p), n = irdb::profileCodeCount(p);
  drawList(n + 1, [&](int i, char *b, size_t sz) {
    if (i == 0) { snprintf(b, sz, "< Back"); return; }
    const IrCode &c = irdb::code(c0 + i - 1);
    snprintf(b, sz, "%s   (%s)", irdb::codeName(c0 + i - 1), irProtoName(c.proto));
  });
  char line[120]; lastSentLine(line, sizeof(line));
  drawFooter(line, "UP/DOWN: move  |  SELECT: send (hold repeats)  |  BACK: back");
}

void drawMenu() {
  drawHeader("Menu", "devices & tools");
  drawList(n_menu, [&](int i, char *b, size_t sz) {
    int m = menu_items[i];
    switch (m) {
      case M_BACK:   snprintf(b, sz, "< Back to remote"); break;
      case M_BROWSE: snprintf(b, sz, "Browse brands (%d in database)", irdb::numBrands()); break;
      case M_SCAN:   snprintf(b, sz, "Code search (find codes for your device)"); break;
      case M_REMOVE: snprintf(b, sz, "Remove current device from list"); break;
      case M_POWER: {
        static const char *const names[LED_POWER_LEVELS] = LED_POWER_NAMES;
        snprintf(b, sz, "LED power: %s  (SELECT = change)", names[settings::ledPower()]);
        break;
      }
      case M_SOUND:
        if (!sound::ready()) snprintf(b, sz, "Sound: %s", sound::status());
        else if (settings::soundMuted()) snprintf(b, sz, "Sound: muted  (volume %d of %d)", settings::soundVolume(), SOUND_VOLUME_MAX);
        else snprintf(b, sz, "Sound: volume %d of %d", settings::soundVolume(), SOUND_VOLUME_MAX);
        break;
      case M_WIFI: {
        String s = settings::wifiSsid();
        snprintf(b, sz, "Wi-Fi network: %s", s.length() ? s.c_str() : "not set");
        break;
      }
      case M_STATUS: snprintf(b, sz, "Status: battery, SD card, Wi-Fi"); break;
      default:
        snprintf(b, sz, "%s %s - %s", m == settings::active() ? "*" : " ",
                 irdb::brandName(irdb::profileBrand(m)), irdb::profileName(m));
    }
  });
  if (sel < n_menu && menu_items[sel] == M_SOUND)
    drawFooter("Sounds: a click on each key press.", "LEFT/RIGHT: quieter / louder  |  SELECT: mute / unmute  |  BACK: back");
  else
    drawFooter("Your devices are listed here; * = active.", "UP/DOWN: move  |  SELECT/RIGHT: choose  |  BACK: back");
}

void drawLetters() {
  drawHeader(browse_for_scan ? "Code search" : "Browse", "first letter of the brand");
  const int COLS = 8, TW = 47, TH = 38, X0 = 4, Y0 = HEADER_H + 8;
  if (sel >= n_letters) sel = 0;
  if (sel < 0) sel = n_letters - 1;
  g->setFont(u8g2_font_helvB14_tr);
  for (int i = 0; i < n_letters; i++) {
    int x = X0 + (i % COLS) * (TW + 2), y = Y0 + (i / COLS) * (TH + 3);
    char s[8];
    if (letters[i] == '<') strcpy(s, "<");
    else { s[0] = letters[i]; s[1] = 0; }
    if (i == sel) { g->drawRBox(x, y, TW, TH, 5); g->setDrawColor(0); }
    else g->drawRFrame(x, y, TW, TH, 5);
    drawCentered(x + TW / 2, y + TH / 2 + 7, s);
    g->setDrawColor(1);
  }
  drawFooter(nullptr, "Arrows: move  |  SELECT: choose  |  BACK: back");
}

void drawBrands() {
  char t[24]; snprintf(t, sizeof(t), "Brands: %c", cur_letter);
  drawHeader(t, browse_for_scan ? "code search" : "browse");
  drawList(n_brand_list, [&](int i, char *b, size_t sz) {
    int br = brand_list[i];
    if (br < 0) snprintf(b, sz, "< Back");
    else snprintf(b, sz, "%s   (%d)", irdb::brandName(br), irdb::brandProfileCount(br));
  });
  drawFooter(nullptr, "UP/DOWN: move (hold = fast)  |  SELECT/RIGHT: choose  |  BACK: back");
}

void drawProfiles() {
  drawHeader(irdb::brandName(cur_brand), "choose the remote / code set");
  int p0 = irdb::brandFirstProfile(cur_brand), n = irdb::brandProfileCount(cur_brand);
  drawList(n + 1, [&](int i, char *b, size_t sz) {
    if (i == 0) { snprintf(b, sz, "< Back"); return; }
    int p = p0 + i - 1;
    snprintf(b, sz, "[%s] %s  (%d keys)", IR_CAT_NAMES[irdb::profileCategory(p)], irdb::profileName(p),
             irdb::profileCodeCount(p));
  });
  drawFooter("Pick the closest model; try Code search if none works.",
             "UP/DOWN: move  |  SELECT/RIGHT: use this device  |  BACK: back");
}

// "Search in" list for code search: < Back, All brands, each brand group, Pick one brand...
// item i: 0 = back, 1 = all, 2..1+groups = group i-2, last = pick a brand
int scanSrcCount() { return irdb::numGroups() + 3; }

void drawScanSrc() {
  drawHeader("Code search", "where to search");
  drawList(scanSrcCount(), [&](int i, char *b, size_t sz) {
    int ng = irdb::numGroups();
    if (i == 0) snprintf(b, sz, "< Back");
    else if (i == 1) snprintf(b, sz, "All brands (%d)", irdb::numBrands());
    else if (i - 2 < ng) snprintf(b, sz, "%s  (%d brands)", irdb::groupName(i - 2), irdb::groupBrandCount(i - 2));
    else snprintf(b, sz, "Pick a single brand...");
  });
  drawFooter("Narrower = fewer codes to try. Duplicates are skipped.", "UP/DOWN: move  |  SELECT/RIGHT: choose  |  BACK: back");
}

void drawScanCat() {
  drawHeader("Code search", "device type");
  drawList(n_cat_list + 1, [&](int i, char *b, size_t sz) {
    if (i == 0) { snprintf(b, sz, "< Back"); return; }
    int c = cat_list[i - 1];
    snprintf(b, sz, "%s  (%d remotes)", c < 0 ? "All types" : catLabel(c), cat_count[i - 1]);
  });
  drawFooter("Choosing the type skips codes for other kinds of device.", "UP/DOWN: move  |  SELECT/RIGHT: choose  |  BACK: back");
}

void drawScanKey() {
  drawHeader("Code search", scan_cat >= 0 ? catLabel(scan_cat)
                            : cur_brand >= 0 ? irdb::brandName(cur_brand)
                            : scan_group >= 0 ? irdb::groupName(scan_group) : "all brands");
  drawList(N_SCAN_KEYS + 1, [&](int i, char *b, size_t sz) {
    if (i == 0) snprintf(b, sz, "< Back");
    else snprintf(b, sz, "Test with: %s", IR_KEY_LABELS[SCAN_KEYS[i - 1]]);
  });
  drawFooter("Aim at the device. Each step sends a different code.",
             "UP/DOWN: move  |  SELECT/RIGHT: start  |  BACK: back");
}

void drawScan() {
  drawHeader("Code search", IR_KEY_LABELS[scan_key]);
  g->setFont(u8g2_font_helvB12_tr);
  if (n_scan == 0) {
    g->drawStr(12, 70, "No code sets with this key here.");
    g->setFont(u8g2_font_helvR10_tr);
    g->drawStr(12, 96, "Press BACK to pick another key or type.");
  } else {
    int c = scan_cands[scan_pos].code;
    int p = scan_cands[scan_pos].profile;
    char b[96];
    g->setFont(u8g2_font_logisoso32_tn);
    snprintf(b, sizeof(b), "%d/%d", scan_pos + 1, n_scan);
    drawCentered(W / 2, 90, b);
    g->setFont(u8g2_font_helvB14_tr);
    drawCenteredFit(125, irdb::brandName(irdb::profileBrand(p)));
    g->setFont(u8g2_font_helvR12_tr);
    snprintf(b, sizeof(b), "[%s] %s", IR_CAT_NAMES[irdb::profileCategory(p)], irdb::profileName(p));
    drawCenteredFit(148, b);
    const IrCode &x = irdb::code(c);
    g->setFont(u8g2_font_helvR12_tr);
    if (scan_cands[scan_pos].shared > 1)
      snprintf(b, sizeof(b), "%s  (%s)  - same code on %u remotes", irdb::codeName(c), irProtoName(x.proto),
               (unsigned)scan_cands[scan_pos].shared);
    else
      snprintf(b, sizeof(b), "%s  (%s)", irdb::codeName(c), irProtoName(x.proto));
    drawCenteredFit(170, b);
    g->setFont(u8g2_font_helvR10_tr);
    drawCentered(W / 2, 205, "Did the device react?");
    drawCentered(W / 2, 223, "Yes: hold SELECT to use this code set.");
    drawCentered(W / 2, 241, "No: RIGHT for the next one (LEFT goes back one).");
  }
  char line[120]; lastSentLine(line, sizeof(line));
  drawFooter(line, "RIGHT: next  |  LEFT: prev  |  SELECT: again, hold = use it  |  BACK: stop");
}

void drawStatus() {
  drawHeader("Status", "");
  g->setFont(u8g2_font_helvR10_tr);
  char b[112];
  int y = 52;
  const int DY = 19;
  float v = hw::packVolts();
  if (isnan(v) || v < 1) snprintf(b, sizeof(b), "AA pack (LEDs):  out (no cells in)");
  else snprintf(b, sizeof(b), "AA pack (LEDs):  %.2f V  (%d %%)%s", v, hw::packPercent(),
                batteryLow() ? "  LOW - swap in charged cells" : "");
  g->drawStr(10, y, b); y += DY;
  float sv = hw::screenVolts();
  if (sv < 2.0f) snprintf(b, sizeof(b), "Screen battery:  none / not readable");
  else snprintf(b, sizeof(b), "Screen battery:  %.2f V  (%d %%)", sv, hw::screenPercent());
  g->drawStr(10, y, b); y += DY;
  if (uint8_t st = hw::stuckKeys()) {
    snprintf(b, sizeof(b), "Keys:  held since power-up - stuck or shorted?%s%s%s%s%s%s", st & (1 << KEYBIT_UP) ? " UP" : "",
             st & (1 << KEYBIT_DOWN) ? " DOWN" : "", st & (1 << KEYBIT_LEFT) ? " LEFT" : "",
             st & (1 << KEYBIT_RIGHT) ? " RIGHT" : "", st & (1 << KEYBIT_SELECT) ? " SELECT" : "",
             st & (1 << KEYBIT_BACK) ? " BACK" : "");
    drawClipped(10, y, W - 20, b); y += DY;
  }
  snprintf(b, sizeof(b), "SD card:  %s", sdstore::status()); drawClipped(10, y, W - 20, b); y += DY;
  snprintf(b, sizeof(b), "Database:  %s", irdb::sourceName()); g->drawStr(10, y, b); y += DY;
  snprintf(b, sizeof(b), "   %d brands, %d remotes, %d codes", irdb::numBrands(), irdb::numProfiles(), irdb::numCodes());
  g->drawStr(10, y, b); y += DY;
  if (irdb::numUserProfiles()) {
    snprintf(b, sizeof(b), "   incl. %d of your own remotes from the SD card", irdb::numUserProfiles());
    g->drawStr(10, y, b); y += DY;
  }
  y += 6;
  snprintf(b, sizeof(b), "Wi-Fi \"%s\"  password  %s", WIFI_AP_SSID, WIFI_AP_PASSWORD); g->drawStr(10, y, b); y += DY;
  snprintf(b, sizeof(b), "   web remote:  http://%s", ap_ip); g->drawStr(10, y, b); y += DY;
  if (sta_ip[0]) {
    snprintf(b, sizeof(b), "Home Wi-Fi \"%s\":  http://%s", sta_ssid, sta_ip); drawClipped(10, y, W - 20, b); y += DY;
    snprintf(b, sizeof(b), "   or  http://%s.local", MDNS_HOSTNAME); g->drawStr(10, y, b);
  } else {
    String s = settings::wifiSsid();
    if (s.length()) snprintf(b, sizeof(b), "Home Wi-Fi \"%s\":  %s", s.c_str(), wifimgr::statusText());
    else snprintf(b, sizeof(b), "Home Wi-Fi: not set (Menu > Wi-Fi network)");
    drawClipped(10, y, W - 20, b);
  }
  drawFooter(nullptr, "Any key: back");
}

// ---- Wi-Fi ----
bool wifiHasNetwork() { return settings::wifiSsid().length() > 0; }
int wifiRows() { return wifiHasNetwork() ? 4 : 2; }

void drawWifi() {
  drawHeader("Wi-Fi", "your home network");
  if (sel != W_FORGET) forget_armed = false;
  String ssid = settings::wifiSsid(), pass = settings::wifiPass();
  drawList(wifiRows(), [&](int i, char *b, size_t sz) {
    switch (i) {
      case W_BACK: snprintf(b, sz, "< Back"); break;
      case W_NETWORK:
        if (ssid.length()) snprintf(b, sz, "Network:  %s", ssid.c_str());
        else snprintf(b, sz, "Network:  none - choose one");
        break;
      case W_PASSWORD:
        if (pass.length()) {
          char stars[17];
          int n = std::min((int)pass.length(), 16);
          memset(stars, '*', n); stars[n] = 0;
          snprintf(b, sz, "Password:  %s  (%u characters)", stars, (unsigned)pass.length());
        } else {
          snprintf(b, sz, "Password:  none (open network)");
        }
        break;
      case W_FORGET:
        snprintf(b, sz, forget_armed ? "Forget it?  SELECT again = yes" : "Forget this network");
        break;
    }
  });
  g->setFont(u8g2_font_helvR10_tr);
  char b[140];
  int y = LIST_TOP + 4 * ROW_H + 20;
  if (!ssid.length()) snprintf(b, sizeof(b), "Status:  not set - only the remote's own LR-Remote network");
  else if (sta_ip[0]) snprintf(b, sizeof(b), "Status:  connected - web remote at http://%s", sta_ip);
  else snprintf(b, sizeof(b), "Status:  %s", wifimgr::statusText());
  drawClipped(10, y, W - 20, b); y += 19;
  snprintf(b, sizeof(b), "Kept in:  %s", settings::wifiSource());
  drawClipped(10, y, W - 20, b); y += 19;
  if (wifi_msg[0]) { g->setFont(u8g2_font_helvB10_tr); drawClipped(10, y, W - 20, wifi_msg); }
  drawFooter(nullptr, "UP/DOWN: move  |  SELECT: change  |  BACK: back");
}

const char *signalWord(int rssi) {
  return rssi >= -60 ? "strong" : rssi >= -70 ? "good" : rssi >= -80 ? "weak" : "very weak";
}

// Network list rows: 0 back, then the networks (or one "scanning" row), then "type a name", "scan again".
int netRows() { return 1 + (n_net_list < 0 ? 1 : n_net_list) + 2; }
int netRowOther() { return netRows() - 2; }
int netRowRescan() { return netRows() - 1; }

void drawWifiNets() {
  drawHeader("Wi-Fi", "nearby 2.4 GHz networks");
  String cur = settings::wifiSsid();
  drawList(netRows(), [&](int i, char *b, size_t sz) {
    if (i == 0) snprintf(b, sz, "< Back");
    else if (i == netRowOther()) snprintf(b, sz, "Other: type the name (hidden network)...");
    else if (i == netRowRescan()) snprintf(b, sz, "Scan again");
    else if (n_net_list < 0) snprintf(b, sz, "   scanning...");
    else {
      const wifimgr::Network &n = net_list[i - 1];
      snprintf(b, sz, "%s%s   (%s%s)", cur == n.ssid ? "* " : "", n.ssid, signalWord(n.rssi), n.open ? ", open" : "");
    }
  });
  char line[64];
  if (n_net_list < 0) snprintf(line, sizeof(line), "Scanning for networks...");
  else if (n_net_list == 0) snprintf(line, sizeof(line), "No networks found - try Scan again");
  else snprintf(line, sizeof(line), "%d network%s found;  * = the one in use", n_net_list, n_net_list == 1 ? "" : "s");
  drawFooter(line, "UP/DOWN: move  |  SELECT: choose  |  BACK: back");
}

// ---- keyboard ----
int kbRowLen(int r) { return r == 4 ? KA__N : (int)strlen(KB_ROWS[kb_page][r]); }
int kbKeyX(int r, int c) { return r == 4 ? KB_ACT_X[c] : KB_X0 + c * KB_PITCH; }
int kbKeyW(int r, int c) { return r == 4 ? KB_ACT_W[c] : KB_KEY_W; }

void openText(TextFor f, const String &initial) {
  text_for = f;
  text_max = f == T_SSID ? 32 : 63;
  snprintf(text_buf, sizeof(text_buf), "%.*s", text_max, initial.c_str());
  text_len = strlen(text_buf);
  text_err[0] = 0;
  text_empty_ok = false;
  kb_page = 0; kb_row = 1; kb_col = 0;          // on "q"
  text_return = screen; text_return_sel = sel;
  go(S_TEXT);
}

void drawText() {
  if (text_for == T_SSID) drawHeader("Network name", "exactly as the router shows it");
  else drawHeader("Password", pending_ssid.c_str());
  // the line being typed (the end of it, if it is too long for the box), with a cursor
  g->drawRFrame(4, HEADER_H + 6, W - 8, 32, 4);
  g->setFont(u8g2_font_courB14_tr);
  const int fx = 12, fmax = W - 8 - 16 - 8;      // room for the text and the cursor
  int x = fx, start = 0;
  if (g->getStrWidth(text_buf) > fmax) {         // too long: show the end, marked with "<"
    g->drawStr(x, HEADER_H + 28, "<");
    x += g->getStrWidth("<") + 2;
    while (start < text_len && g->getStrWidth(text_buf + start) > fmax - (x - fx)) start++;
  }
  g->drawStr(x, HEADER_H + 28, text_buf + start);
  int cx = x + g->getStrWidth(text_buf + start) + 1;
  g->drawBox(cx, HEADER_H + 12, 2, 20);
  // one line of help + the length
  g->setFont(u8g2_font_helvR08_tr);
  const char *help = text_for == T_SSID ? "Case-sensitive. 2.4 GHz networks only."
                                        : "8 to 63 characters, case-sensitive. Empty = open network.";
  g->drawStr(8, HEADER_H + 52, help);
  char cnt[16]; snprintf(cnt, sizeof(cnt), "%d / %d", text_len, text_max);
  g->drawStr(W - 8 - g->getStrWidth(cnt), HEADER_H + 52, cnt);
  // keys
  for (int r = 0; r < 5; r++) {
    int y = KB_Y0 + r * KB_ROW_H;
    for (int c = 0; c < kbRowLen(r); c++) {
      int kx = kbKeyX(r, c), kw = kbKeyW(r, c);
      char s[8];
      if (r < 4) { s[0] = KB_ROWS[kb_page][r][c]; s[1] = 0; g->setFont(u8g2_font_helvB12_tr); }
      else {
        static const char *const ACT[KA__N] = {nullptr, "space", "delete", "Done"};
        snprintf(s, sizeof(s), "%s", c == KA_PAGE ? KB_PAGE_LABEL[kb_page] : ACT[c]);
        g->setFont(u8g2_font_helvB10_tr);
      }
      bool on = r == kb_row && c == kb_col;
      if (on) { g->drawRBox(kx, y, kw, KB_KEY_H, 4); g->setDrawColor(0); }
      else g->drawRFrame(kx, y, kw, KB_KEY_H, 4);
      drawCentered(kx + kw / 2, y + KB_KEY_H / 2 + 6, s);
      g->setDrawColor(1);
    }
  }
  drawFooter(text_err[0] ? text_err : nullptr,
             "SELECT: type  |  BACK: delete, hold = cancel  |  Done: save");
}

// Move on the keyboard. UP/DOWN go to the key in the next row nearest the current one (rows differ
// in length: the action row has four wide keys).
void kbMove(int k) {
  if (k == K_LEFT || k == K_RIGHT) {
    int n = kbRowLen(kb_row);
    kb_col = (kb_col + (k == K_RIGHT ? 1 : n - 1)) % n;
    return;
  }
  int cx = kbKeyX(kb_row, kb_col) + kbKeyW(kb_row, kb_col) / 2;
  kb_row = (kb_row + (k == K_DOWN ? 1 : 4)) % 5;
  int best = 0, bd = 1 << 30;
  for (int c = 0; c < kbRowLen(kb_row); c++) {
    int d = abs(kbKeyX(kb_row, c) + kbKeyW(kb_row, c) / 2 - cx);
    if (d < bd) { bd = d; best = c; }
  }
  kb_col = best;
}

void textCancel() { go(text_return, text_return_sel); }

// Save a network: checked and saved by settings, then the Wi-Fi manager starts over with it.
bool applyWifi(const String &ssid, const String &pass, char *err, size_t en) {
  if (!settings::setWifi(ssid, pass, err, en)) return false;
  snprintf(wifi_msg, sizeof(wifi_msg), "%s", err);
  wifimgr::reconnect();
  return true;
}

void textDone() {
  text_err[0] = 0;
  String t(text_buf);
  if (text_for == T_SSID) {
    if (!text_len) { snprintf(text_err, sizeof(text_err), "Type the network's name (or BACK to cancel)"); return; }
    pending_ssid = t;
    pending_secured = false;                   // not seen in a scan: it may be open
    String keep = pending_ssid == settings::wifiSsid() ? settings::wifiPass() : String("");
    openText(T_PASS, keep);
    text_return = S_WIFI; text_return_sel = W_NETWORK;
    return;
  }
  if (!text_len && pending_secured) {
    snprintf(text_err, sizeof(text_err), "This network needs its password");
    return;
  }
  if (!text_len && !text_empty_ok) {            // never wipe a password by accident
    text_empty_ok = true;
    snprintf(text_err, sizeof(text_err), "No password = open network. Done again to save it like that.");
    return;
  }
  if (const char *why = wififile::check(pending_ssid.c_str(), text_buf)) {
    snprintf(text_err, sizeof(text_err), "%c%s", toupper((unsigned char)why[0]), why + 1);
    return;
  }
  if (applyWifi(pending_ssid, t, text_err, sizeof(text_err))) go(S_WIFI, W_NETWORK);
}

void kbPress() {
  if (!(kb_row == 4 && kb_col == KA_DONE)) text_empty_ok = false;
  if (kb_row < 4) {
    if (text_len < text_max) { text_buf[text_len++] = KB_ROWS[kb_page][kb_row][kb_col]; text_buf[text_len] = 0; }
    text_err[0] = 0;
    return;
  }
  switch (kb_col) {
    case KA_PAGE:
      kb_page = (kb_page + 1) % 3;
      break;
    case KA_SPACE:
      if (text_len < text_max) { text_buf[text_len++] = ' '; text_buf[text_len] = 0; }
      break;
    case KA_DEL:
      if (text_len) text_buf[--text_len] = 0;
      break;
    case KA_DONE:
      textDone();
      break;
  }
}

void render() {
  g->clearBuffer();
  g->setDrawColor(1);
  g->setFontMode(1);
  switch (screen) {
    case S_REMOTE:   drawRemote(); break;
    case S_PAD:      drawPad(); break;
    case S_NUMBER:   drawNumber(); break;
    case S_ALLKEYS:  drawAllKeys(); break;
    case S_MENU:     drawMenu(); break;
    case S_LETTERS:  drawLetters(); break;
    case S_BRANDS:   drawBrands(); break;
    case S_PROFILES: drawProfiles(); break;
    case S_SCANSRC:  drawScanSrc(); break;
    case S_SCANCAT:  drawScanCat(); break;
    case S_SCANKEY:  drawScanKey(); break;
    case S_SCAN:     drawScan(); break;
    case S_STATUS:   drawStatus(); break;
    case S_WIFI:     drawWifi(); break;
    case S_WIFINETS: drawWifiNets(); break;
    case S_TEXT:     drawText(); break;
  }
  g->sendBuffer();
  dirty = false;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
int listSize() {
  switch (screen) {
    case S_ALLKEYS:  return irdb::profileCodeCount(settings::active()) + 1;
    case S_MENU:     return n_menu;
    case S_BRANDS:   return n_brand_list;
    case S_PROFILES: return irdb::brandProfileCount(cur_brand) + 1;
    case S_SCANSRC:  return scanSrcCount();
    case S_SCANCAT:  return n_cat_list + 1;
    case S_SCANKEY:  return N_SCAN_KEYS + 1;
    case S_WIFI:     return wifiRows();
    case S_WIFINETS: return netRows();
    default:         return 1;
  }
}

void scanSendCurrent() {
  if (n_scan > 0) irtx::tap(scan_cands[scan_pos].code);
}

void startSend(int code) {
  if (code < 0) return;
  stopSend();
  irtx::holdStart(code);
  send_held = true;
}

void back() {
  switch (screen) {
    case S_PAD: case S_NUMBER: case S_ALLKEYS: case S_MENU: go(S_REMOTE); break;
    case S_LETTERS: go(browse_for_scan ? S_SCANSRC : S_MENU); break;
    case S_SCANSRC: go(S_MENU); break;
    case S_BRANDS:  go(S_LETTERS); break;
    case S_PROFILES: go(S_BRANDS); break;
    case S_SCANCAT: go(cur_brand >= 0 ? S_BRANDS : S_SCANSRC); break;
    case S_SCANKEY: go(S_SCANCAT); break;
    case S_SCAN:    go(S_SCANKEY); break;
    case S_STATUS:  go(S_MENU); break;
    case S_WIFI:    go(S_MENU); sel = menuIndex(M_WIFI); break;
    case S_WIFINETS: go(S_WIFI, W_NETWORK); break;
    case S_TEXT:    textCancel(); break;
    default: break;
  }
}

// SELECT / RIGHT on a list or grid item (not a send)
void choose() {
  switch (screen) {
    case S_REMOTE:
      if (sel >= n_grid) break;
      if (grid[sel] == TILE_MENU) go(S_MENU);
      else if (grid[sel] == TILE_ALL) go(S_ALLKEYS);
      else if (grid[sel] == TILE_NAVPAD) { cur_pad = 0; go(S_PAD); }
      else if (grid[sel] == TILE_VOLPAD) { cur_pad = 1; go(S_PAD); }
      else if (grid[sel] == TILE_NUMBER) { num_len = 0; num_digits[0] = 0; num_cur = digitAvail(0) ? 0 : 1; go(S_NUMBER); }
      break;
    case S_ALLKEYS:
      if (sel == 0) go(S_REMOTE);
      break;
    case S_MENU: {
      int m = menu_items[sel];
      if (m == M_BACK) go(S_REMOTE);
      else if (m == M_BROWSE) { browse_for_scan = false; go(S_LETTERS); }
      else if (m == M_SCAN) { browse_for_scan = true; go(S_SCANSRC); }
      else if (m == M_REMOVE) { settings::removeFav(settings::active()); go(S_MENU); }
      else if (m == M_POWER) { settings::setLedPower((settings::ledPower() + 1) % LED_POWER_LEVELS); dirty = true; }
      else if (m == M_SOUND) {
        settings::setSoundMuted(!settings::soundMuted());
        sound::play(sound::CLICK);                 // unmuted: a click at the volume it will use
        dirty = true;
      }
      else if (m == M_WIFI) { wifi_msg[0] = 0; go(S_WIFI); }
      else if (m == M_STATUS) go(S_STATUS);
      else { settings::setActive(m); go(S_REMOTE); }
      break;
    }
    case S_LETTERS:
      if (letters[sel] == '<') back();
      else { cur_letter = letters[sel]; go(S_BRANDS); }
      break;
    case S_BRANDS:
      if (brand_list[sel] < 0) go(S_LETTERS);
      else {
        cur_brand = brand_list[sel];
        go(browse_for_scan ? S_SCANCAT : S_PROFILES);
      }
      break;
    case S_PROFILES:
      if (sel == 0) go(S_BRANDS);
      else { settings::setActive(irdb::brandFirstProfile(cur_brand) + sel - 1); go(S_REMOTE); }
      break;
    case S_SCANSRC: {
      int ng = irdb::numGroups();
      if (sel == 0) go(S_MENU);
      else if (sel == 1) { cur_brand = -1; scan_group = -1; go(S_SCANCAT); }
      else if (sel - 2 < ng) { cur_brand = -1; scan_group = sel - 2; go(S_SCANCAT); }
      else { scan_group = -1; go(S_LETTERS); }
      break;
    }
    case S_SCANCAT:
      if (sel == 0) back();
      else { scan_cat = cat_list[sel - 1]; go(S_SCANKEY); }
      break;
    case S_SCANKEY:
      if (sel == 0) back();
      else { scan_key = SCAN_KEYS[sel - 1]; buildScan(); go(S_SCAN); scanSendCurrent(); }
      break;
    case S_STATUS:
      go(S_MENU);
      break;
    case S_WIFI:
      if (sel == W_BACK) back();
      else if (sel == W_NETWORK) { n_net_list = -1; wifimgr::requestNetworks(); go(S_WIFINETS, 1); }
      else if (sel == W_PASSWORD) {
        pending_ssid = settings::wifiSsid();
        pending_secured = false;
        openText(T_PASS, settings::wifiPass());
      } else if (sel == W_FORGET) {
        if (!forget_armed) { forget_armed = true; dirty = true; }
        else {
          char err[80];
          forget_armed = false;
          if (!applyWifi("", "", err, sizeof(err))) snprintf(wifi_msg, sizeof(wifi_msg), "%s", err);
          go(S_WIFI, W_NETWORK);
        }
      }
      break;
    case S_WIFINETS:
      if (sel == 0) back();
      else if (sel == netRowRescan()) { n_net_list = -1; wifimgr::requestNetworks(); dirty = true; }
      else if (sel == netRowOther()) openText(T_SSID, String(""));
      else if (n_net_list > 0) {
        const wifimgr::Network &n = net_list[sel - 1];
        if (n.open) {                            // nothing to type
          char err[80];
          if (!applyWifi(n.ssid, "", err, sizeof(err))) snprintf(wifi_msg, sizeof(wifi_msg), "%s", err);
          go(S_WIFI, W_NETWORK);
        } else {
          pending_ssid = n.ssid;
          pending_secured = true;
          openText(T_PASS, pending_ssid == settings::wifiSsid() ? settings::wifiPass() : String(""));
        }
      }
      break;
    default: break;
  }
}

// Move in a grid of `cols` columns with `n` items. UP/DOWN wrap to the other end of the same column.
void gridMove(int k, int cols, int n) {
  if (n <= 0) return;
  int rows = (n + cols - 1) / cols;
  int r = sel / cols, c = sel % cols;
  switch (k) {
    case K_LEFT:  sel = (sel + n - 1) % n; return;
    case K_RIGHT: sel = (sel + 1) % n; return;
    case K_UP:    r = (r + rows - 1) % rows; break;
    case K_DOWN:  r = (r + 1) % rows; break;
  }
  int s = r * cols + c;
  sel = s < n ? s : n - 1;                      // short last row: land on its last item
}

void onKey(int k, Act a) {
  dirty = true;
  // BACK: one screen back everywhere. The pads handle it themselves (Nav pad: the device's Back key);
  // on the remote screen, the top level, it does nothing.
  if (k == K_BACK && screen != S_PAD && screen != S_TEXT) {
    if (a == A_PRESS && screen != S_REMOTE) back();
    return;
  }
  // Menu > Sound: LEFT / RIGHT change the volume on the spot (SELECT mutes: choose()). Each step
  // clicks at the new volume.
  if (screen == S_MENU && sel < n_menu && menu_items[sel] == M_SOUND && (k == K_LEFT || k == K_RIGHT)) {
    if ((a == A_PRESS || a == A_REPEAT) && sound::ready()) {
      int v = settings::soundVolume() + (k == K_RIGHT ? 1 : -1);
      if (v >= 1 && v <= SOUND_VOLUME_MAX) settings::setSoundVolume(v);
      if (settings::soundMuted()) settings::setSoundMuted(false);
      sound::play(sound::CLICK);
    }
    return;
  }
  switch (screen) {
    // ---- remote grid: arrows move, SELECT sends ----
    case S_REMOTE: {
      if (k == K_SELECT) {
        if (a == A_PRESS) {
          int p = settings::active();
          if (p >= 0 && sel < n_grid && grid[sel] >= 0) startSend(irdb::profileFindKey(p, (uint8_t)grid[sel]));
          else choose();
        } else if (a == A_UP) stopSend();
      } else if (a == A_PRESS || a == A_REPEAT) {
        gridMove(k, GRID_COLS, n_grid);
      }
      break;
    }
    // ---- pads: every key is a device key; hold SELECT or BACK to leave ----
    case S_PAD: {
      if (k == K_SELECT || k == K_BACK) {
        int c = padCode(cur_pad, k);
        if (a == A_RELEASE) { if (c >= 0) irtx::tap(c); else if (k == K_BACK) go(S_REMOTE); }
        else if (a == A_LONG) go(S_REMOTE);
      } else {
        if (a == A_PRESS) startSend(padCode(cur_pad, k));
        else if (a == A_UP) stopSend();
      }
      break;
    }
    // ---- channel number entry ----
    case S_NUMBER: {
      if (k == K_UP && (a == A_PRESS || a == A_REPEAT)) num_cur = stepDigit(num_cur, 1);
      else if (k == K_DOWN && (a == A_PRESS || a == A_REPEAT)) num_cur = stepDigit(num_cur, -1);
      else if (k == K_RIGHT && a == A_PRESS && num_len < 4) {
        num_digits[num_len++] = (char)('0' + num_cur); num_digits[num_len] = 0;
        num_cur = digitAvail(0) ? 0 : 1;
      }
      else if (k == K_LEFT && a == A_PRESS) {
        if (num_len > 0) { num_cur = num_digits[--num_len] - '0'; num_digits[num_len] = 0; }
        else back();
      }
      else if (k == K_SELECT && a == A_RELEASE && !num_send[0]) {
        // the number = typed digits + the one in the box
        snprintf(num_send, sizeof(num_send), "%.4s%c", num_digits, '0' + num_cur);
        num_send_pos = 0; num_next_at = millis();
        num_len = 0; num_digits[0] = 0; num_cur = digitAvail(0) ? 0 : 1;
      }
      else if (k == K_SELECT && a == A_LONG) back();
      break;
    }
    // ---- every button of the device: a list where SELECT sends ----
    case S_ALLKEYS: {
      if (k == K_SELECT) {
        if (a == A_PRESS) {
          if (sel == 0) go(S_REMOTE);
          else startSend(irdb::profileFirstCode(settings::active()) + sel - 1);
        } else if (a == A_UP) stopSend();
      } else if (k == K_UP && (a == A_PRESS || a == A_REPEAT)) { sel--; if (sel < 0) sel = listSize() - 1; }
      else if (k == K_DOWN && (a == A_PRESS || a == A_REPEAT)) { sel++; if (sel >= listSize()) sel = 0; }
      else if (k == K_LEFT && a == A_PRESS) back();
      break;
    }
    // ---- letter grid ----
    case S_LETTERS: {
      if (k == K_SELECT) {
        if (a == A_RELEASE) choose();
        else if (a == A_LONG) back();
      } else if (a == A_PRESS || a == A_REPEAT) {
        gridMove(k, 8, n_letters);
      }
      break;
    }
    // ---- code search ----
    case S_SCAN: {
      bool fwd = (k == K_RIGHT || k == K_DOWN) && a == A_PRESS;
      bool rev = (k == K_UP && a == A_PRESS) || (k == K_LEFT && a == A_RELEASE);
      if (fwd && n_scan) { scan_pos = (scan_pos + 1) % n_scan; scanSendCurrent(); }
      else if (rev && n_scan) { scan_pos = (scan_pos + n_scan - 1) % n_scan; scanSendCurrent(); }
      else if (k == K_SELECT && a == A_RELEASE) scanSendCurrent();
      else if (k == K_SELECT && a == A_LONG && n_scan) {     // "it works" -> use that code set
        settings::setActive(scan_cands[scan_pos].profile);
        go(S_REMOTE);
      }
      else if (k == K_LEFT && a == A_LONG) go(S_SCANKEY);
      break;
    }
    case S_STATUS:
      if (a == A_PRESS) go(S_MENU);
      break;
    // ---- keyboard: arrows move, SELECT types, BACK deletes (held = leave) ----
    case S_TEXT: {
      if (k == K_BACK) {
        if (a == A_LONG) textCancel();
        else if (a == A_RELEASE) {
          if (text_len) { text_buf[--text_len] = 0; text_err[0] = 0; }
          else textCancel();
        }
      } else if (k == K_SELECT) {
        bool repeatable = kb_row < 4 || kb_col == KA_SPACE || kb_col == KA_DEL;
        if (a == A_PRESS || (a == A_REPEAT && repeatable)) kbPress();
      } else if (a == A_PRESS || a == A_REPEAT) {
        kbMove(k);
      }
      break;
    }
    // ---- plain lists ----
    default: {
      if (k == K_UP && (a == A_PRESS || a == A_REPEAT)) { sel--; if (sel < 0) sel = listSize() - 1; }
      else if (k == K_DOWN && (a == A_PRESS || a == A_REPEAT)) { sel++; if (sel >= listSize()) sel = 0; }
      else if (k == K_RIGHT && a == A_PRESS) choose();
      else if (k == K_LEFT && a == A_PRESS) back();
      else if (k == K_SELECT && a == A_RELEASE) choose();
      else if (k == K_SELECT && a == A_LONG) back();
      break;
    }
  }
}

// Debounce the six keys and turn them into actions.
void pollInputs() {
  uint8_t bits = hw::keys();
  uint32_t now = millis();
  for (int i = 0; i < K__N; i++) {
    Key &k = keys[i];
    bool raw = bits & (1u << KEY_BITS[i]);
    if (raw != k.raw_last) { k.raw_last = raw; k.changed_at = now; }
    if (raw != k.down && now - k.changed_at >= DEBOUNCE_MS) {
      k.down = raw;
      if (raw) {
        k.down_at = now; k.last_repeat = now; k.long_fired = false;
        if (!k.swallow) { sound::play(sound::CLICK); onKey(i, A_PRESS); }
      } else {
        if (k.swallow) { k.swallow = false; dirty = true; continue; }
        if (!k.long_fired) onKey(i, A_RELEASE);
        onKey(i, A_UP);
      }
      continue;
    }
    if (!k.down || k.swallow) continue;
    uint32_t held = now - k.down_at;
    if (!k.long_fired && held >= LONG_MS) {
      k.long_fired = true;
      // the "hold" sound only where holding did something (left a screen or picked a device), not for
      // a key that is just being held to repeat
      Screen before = screen; int before_active = settings::active();
      onKey(i, A_LONG);
      if (screen != before || settings::active() != before_active) sound::play(sound::LONG);
      if (k.swallow) continue;
    }
    if (held >= REPEAT_DELAY_MS) {
      uint32_t period = held > 2000 ? 45 : 110;          // accelerate after 2 s
      if (now - k.last_repeat >= period) { k.last_repeat = now; onKey(i, A_REPEAT); }
    }
  }
  if (send_held) irtx::holdKeepAlive();
}

}  // namespace

void begin() {
  // Buffers that scale with the database (it may come from the SD card): PSRAM, fall back to RAM.
  int nb = irdb::numBrands() + 1;               // (+1: never a 0-byte request, even with no database)
  brand_list = (int *)heap_caps_malloc(nb * sizeof(int), MALLOC_CAP_SPIRAM);
  if (!brand_list) brand_list = (int *)malloc(nb * sizeof(int));
  scan_cap = irdb::numProfiles() + 1;
  scan_cands = (irdb::ScanCand *)heap_caps_malloc(scan_cap * sizeof(irdb::ScanCand), MALLOC_CAP_SPIRAM);
  if (!scan_cands) scan_cands = (irdb::ScanCand *)malloc(scan_cap * sizeof(irdb::ScanCand));
  if (!brand_list || !scan_cands) logln("UI: out of memory for the brand / code search lists");

  lcd.begin(0, U8G2_R1);                        // panel is 300x400 portrait; R1 = 400x300 landscape
  g = lcd.getU8g2();
  last_active = settings::active();
  go(S_REMOTE);
  render();
}

// Send a typed channel number one digit at a time.
static void numberSendLoop() {
  if (!num_send[0] || (int32_t)(millis() - num_next_at) < 0 || irtx::isSending()) return;
  int p = settings::active();
  char d = num_send[num_send_pos];
  if (d && p >= 0) {
    irtx::tap(irdb::profileFindKey(p, (uint8_t)(KEY_D0 + (d - '0'))));
    num_send_pos++;
    num_next_at = millis() + DIGIT_GAP_MS;
  } else {
    num_send[0] = 0;
  }
  dirty = true;
}

void loop() {
  pollInputs();
  numberSendLoop();
  // react to changes made from the web UI
  if (settings::active() != last_active) {
    last_active = settings::active();
    if (screen == S_REMOTE || screen == S_ALLKEYS || screen == S_PAD || screen == S_MENU)
      go(screen == S_MENU ? S_MENU : S_REMOTE);
  }
  irtx::LastSent l = irtx::last();
  if (l.seq != last_seq || l.frames != last_frames) { last_seq = l.seq; last_frames = l.frames; dirty = true; }
  if (screen == S_WIFINETS && n_net_list < 0) {
    int n = wifimgr::networks(net_list, MAX_NET_LIST);
    if (n >= 0) { n_net_list = n; dirty = true; }
  }
  uint32_t status_every = screen == S_WIFI ? 2000 : 10000;     // Wi-Fi screen: connection status
  if (millis() - last_status_draw > status_every) { last_status_draw = millis(); dirty = true; }   // battery
  if (dirty) render();
}

void requestRedraw() { dirty = true; }

void setAsleep(bool a) {
  asleep = a;
  if (g) render();                // now: the processor stops right after this
}

void setNetworkInfo(const char *ap, const char *sta, const char *ssid) {
  strncpy(ap_ip, ap ? ap : "", sizeof(ap_ip) - 1);
  strncpy(sta_ip, sta ? sta : "", sizeof(sta_ip) - 1);
  strncpy(sta_ssid, ssid ? ssid : "", sizeof(sta_ssid) - 1);
  dirty = true;
}

}  // namespace ui
