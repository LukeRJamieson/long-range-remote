// ui.cpp - on-device UI for the Long-Range Remote universal remote.
//
// Controls: the six keys on the control board under the screen (direct GPIOs, see hw.h): the D-pad,
// SELECT, and BACK at the D-pad's lower right.
//
//   BACK                               one screen back, everywhere. On the remote screen (the top
//                                      level) it opens the menu. Nav pad: sends the device's own
//                                      Back / Return, held = leave. Vol pad: leave.
//   Lists (menus, brands, models...)   UP/DOWN move (hold = fast), RIGHT or SELECT = choose,
//                                      BACK, LEFT or SELECT held = back (no "< Back" rows)
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
#include "discover.h"
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
const int HEADER_H = 30, FOOTER_H = 38;

ST7305_U8g2 lcd(LCD_SCK_PIN, LCD_MOSI_PIN, LCD_DC_PIN, LCD_CS_PIN, LCD_RST_PIN);
U8G2 *g = nullptr;
bool dirty = true;

char ap_ip[20] = "192.168.4.1", sta_ip[20] = "", sta_ssid[33] = "";
char web_ip[20] = "";                 // last web client (Status screen)
uint32_t web_at = 0, web_count = 0;

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
              S_SCANSRC, S_SCANCAT, S_SCANKEY, S_SCAN, S_STATUS, S_WIFI, S_WIFINETS, S_TEXT,
              S_DFAM, S_DSWEEP, S_DNARROW, S_DLEARN, S_DNAME, S_DDONE };      // Discover (discover.h)
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
const int TILE_ALL = -2, TILE_NAVPAD = -3, TILE_VOLPAD = -4, TILE_NUMBER = -5;
bool ir_shown = false;                // the header's "IR" mark was drawn in the last frame
int remote_sel = 0, remote_scroll = 0, remote_for = -2;   // where the remote screen was left (and for which device)

// Channel number entry: digits typed so far + the one being chosen, and the send queue
char num_digits[6] = ""; int num_len = 0; int num_cur = 0;
char num_send[6] = ""; int num_send_pos = 0; uint32_t num_next_at = 0;
const uint32_t DIGIT_GAP_MS = 350;      // between digit keys (TVs ignore digits sent too quickly)
int grid[KEY__COUNT + 8]; int n_grid = 0;
const int GRID_COLS = 4, GRID_ROWS = 5;
const int LETTER_COLS = 7;

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
// Code search Auto: steps to the next code by itself every settings::scanAutoMs()
bool scan_auto = false;
uint32_t scan_last_at = 0;            // when Auto last sent a code (or was started)
bool scan_ok_stopped = false;         // OK stopped Auto: its release doesn't also send the code again
char scan_msg[80] = "";               // one line on the code search screen (Auto stopped, speed changed)

// Menu entries
enum MenuItem { M_BROWSE = -100, M_DISCOVER, M_SCAN, M_REMOVE, M_POWER, M_SOUND, M_AUTO, M_WIFI, M_STATUS };
int menu_items[MAX_FAVOURITES + 10]; int n_menu = 0;

// Currently sending (a key held on a remote / pad screen)
bool send_held = false;

// Wi-Fi menu. Rows of the Wi-Fi screen (PASSWORD and FORGET only when a network is set):
enum WifiRow { W_NETWORK, W_PASSWORD, W_FORGET };
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
const int KB_X0 = 5, KB_PITCH = 39, KB_KEY_W = 38, KB_Y0 = 92, KB_ROW_H = 33, KB_KEY_H = 30;
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

// Battery outline, 22 x 10, filled to pct (< 0 = no battery: a cross). Drawn in the current colour.
void drawBattery(int x, int y, int pct) {
  g->drawFrame(x, y, 20, 10);
  g->drawBox(x + 20, y + 3, 2, 4);
  if (pct < 0) { g->drawLine(x + 6, y + 2, x + 13, y + 7); g->drawLine(x + 6, y + 7, x + 13, y + 2); return; }
  int w = (16 * pct + 50) / 100;
  if (pct > 0 && w < 1) w = 1;
  if (w > 0) g->drawBox(x + 2, y + 2, w, 6);
}

// A code is going out right now (or did in the last half second): the header shows "IR".
bool irActive() {
  if (irtx::isSending()) return true;
  irtx::LastSent l = irtx::last();
  return l.seq && millis() - l.at_ms < 500;
}

const char *netLabel() { return sta_ip[0] ? "WiFi" : "AP"; }

// Header bar: title (+ subtitle if there is room) on the left; on the right the two batteries
// (AA = the LED pack, Scr = the display board's own cell), and the network state in a box.
void drawHeader(const char *title, const char *subtitle) {
  g->setDrawColor(1);
  g->drawBox(0, 0, W, HEADER_H);
  g->setDrawColor(0);
  // network state
  g->setFont(u8g2_font_helvB08_tr);
  const char *net = netLabel();
  int cw = g->getStrWidth(net) + 10, cx = W - 4 - cw;
  g->drawRBox(cx, 7, cw, 16, 3);
  g->setDrawColor(1);
  g->drawStr(cx + 5, 19, net);
  g->setDrawColor(0);
  // batteries: label | outline | percent
  const int BW = 76;
  int bx = cx - 8 - BW;
  int pct = hw::packPercent(), sc = hw::screenPercent();
  auto battRow = [&](int y, const char *label, int p, bool low) {
    g->setFont(u8g2_font_helvR08_tr);
    g->drawStr(bx, y + 9, label);
    drawBattery(bx + 22, y, p);
    char t[8];
    if (p < 0) snprintf(t, sizeof(t), "out");
    else snprintf(t, sizeof(t), "%d%%", p);
    if (low) g->setFont(u8g2_font_helvB08_tr);
    g->drawStr(bx + 49, y + 9, low ? "LOW" : t);
  };
  const char *pack = *PACK_NAME ? PACK_NAME : "LED";
  if (sc >= 0) { battRow(4, pack, pct, batteryLow()); battRow(16, "Scr", sc, false); }
  else battRow(10, pack, pct, batteryLow());
  // sending mark
  int left_end = bx - 8;
  ir_shown = irActive();
  if (ir_shown) {
    g->setFont(u8g2_font_helvB08_tr);
    g->drawRFrame(left_end - 22, 7, 22, 16, 3);
    g->drawStr(left_end - 16, 19, "IR");
  }
  left_end -= 28;                                // kept free either way, so the title doesn't jump
  // title + subtitle
  g->setFont(u8g2_font_helvB14_tr);
  int x = 6, tw = min(left_end - x, (int)g->getStrWidth(title));
  drawClipped(x, 21, left_end - x, title);
  x += tw + 10;
  if (subtitle && *subtitle && left_end - x >= 50) {
    g->setFont(u8g2_font_helvR10_tr);
    drawClipped(x, 20, left_end - x, subtitle);
  }
  g->setDrawColor(1);
}

// Key hints, e.g. "{OK} Send  {BACK} Menu": the parts in braces are drawn as key caps, the rest as
// text. Returns the width; draw = false only measures.
int drawHints(int x, int y, const char *s, bool draw) {
  int x0 = x;
  char seg[64];
  while (*s) {
    bool cap = *s == '{';
    if (cap) s++;
    size_t n = 0;
    while (*s && *s != (cap ? '}' : '{') && n < sizeof(seg) - 1) seg[n++] = *s++;
    seg[n] = 0;
    if (cap && *s == '}') s++;
    if (cap) {
      g->setFont(u8g2_font_helvB08_tr);
      int w = g->getStrWidth(seg) + 8;
      if (draw) {
        g->drawRBox(x, y - 11, w, 14, 3);
        g->setDrawColor(0);
        g->drawStr(x + 4, y, seg);
        g->setDrawColor(1);
      }
      x += w + 4;
    } else {
      g->setFont(u8g2_font_helvR10_tr);
      if (draw) g->drawStr(x, y, seg);
      x += g->getStrWidth(seg);
    }
  }
  return x - x0;
}

// Footer: one line of information, and the keys that do something on this screen.
void drawFooterHints(const char *hints) {
  g->drawHLine(0, H - FOOTER_H, W);
  if (hints) drawHints(6, H - 6, hints, true);
}

void drawFooter(const char *line1, const char *hints) {
  drawFooterHints(hints);
  g->setFont(u8g2_font_helvR10_tr);
  if (line1) drawClipped(6, H - FOOTER_H + 15, W - 12, line1);
}

// Footer whose information line is the last code sent (or why nothing can be sent).
void drawFooterSent(const char *hints) {
  drawFooterHints(hints);
  const int y = H - FOOTER_H + 15;
  float pv = hw::packVolts();
  const char *warn = nullptr;
  if (!isnan(pv) && pv < 1.0f) warn = "LEDs off - no AA cells in";
  else if (!isnan(pv) && pv < PACK_CUTOFF_V) warn = PACK_FLAT_MSG;
  else if (batteryLow()) warn = PACK_LOW_MSG;
  if (warn) { g->setFont(u8g2_font_helvB10_tr); drawClipped(6, y, W - 12, warn); return; }
  irtx::LastSent l = irtx::last();
  g->setFont(u8g2_font_helvR10_tr);
  if (!l.seq) { g->drawStr(6, y, "Nothing sent yet"); return; }
  // right: protocol details, small; left: what was sent
  char pr[48];
  if (l.proto == IRP_RAW) snprintf(pr, sizeof(pr), "raw %lu kHz", (unsigned long)(l.carrier_hz / 1000));
  else snprintf(pr, sizeof(pr), "%s %X.%X.%X", irProtoName(l.proto), l.d, l.s, l.f);
  g->setFont(u8g2_font_helvR08_tr);
  int pw = g->getStrWidth(pr);
  g->drawStr(W - 6 - pw, y, pr);
  g->setFont(u8g2_font_helvR10_tr);
  g->drawStr(6, y, "Sent:");
  int x = 6 + g->getStrWidth("Sent: ");
  char b[96];
  if (l.frames > 1) snprintf(b, sizeof(b), "%s  x%lu", l.label, (unsigned long)l.frames);
  else snprintf(b, sizeof(b), "%s", l.label);
  g->setFont(u8g2_font_helvB10_tr);
  drawClipped(x, y, W - 6 - pw - 10 - x, b);
}

// Generic list: items drawn by callback.
// Lists have no "< Back" row: BACK (or LEFT) goes back everywhere.
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

void drawScrollbar(int top, int track, int total, int visible, int first) {
  if (total <= visible) return;
  int bar = max(12, track * visible / total);
  int y = top + (track - bar) * first / max(1, total - visible);
  g->drawFrame(W - 8, top, 6, track);
  g->drawBox(W - 8, y, 6, bar);
}

// One row of a list: text on the left; optionally a value on the right, an arrow (opens another
// screen), a mark in front (1 = ring, 2 = dot: the active one) and a rule above (new group).
struct Row {
  char text[96];
  char value[48];
  uint8_t mark;
  bool more;
  bool rule;
};

template <typename F>
void drawList(int n_items, F fill) {
  keepVisible(n_items, LIST_ROWS);
  for (int r = 0; r < LIST_ROWS && scroll + r < n_items; r++) {
    int i = scroll + r;
    int y = LIST_TOP + r * ROW_H;
    Row row;
    memset(&row, 0, sizeof(row));
    fill(i, row);
    if (row.rule && r > 0) g->drawHLine(8, y - 1, W - 26);
    if (i == sel) {
      g->drawRBox(2, y, W - 14, ROW_H - 2, 4);
      g->setDrawColor(0);
    }
    int tx = 10, rx = W - 20;
    if (row.mark) {
      if (row.mark == 2) g->drawDisc(16, y + 12, 4); else g->drawCircle(16, y + 12, 4);
      tx = 28;
    }
    if (row.more) { g->drawTriangle(rx - 6, y + 7, rx - 6, y + 18, rx, y + 12); rx -= 14; }
    if (row.value[0]) {
      g->setFont(u8g2_font_helvR10_tr);
      int vw = min((int)g->getStrWidth(row.value), 190);
      drawClipped(rx - vw, y + 17, vw, row.value);
      rx -= vw + 10;
    }
    g->setFont(u8g2_font_helvB12_tr);
    drawClipped(tx, y + 18, rx - tx, row.text);
    g->setDrawColor(1);
  }
  drawScrollbar(LIST_TOP, LIST_ROWS * ROW_H, n_items, LIST_ROWS, scroll);
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
  for (int i = 0; i < settings::favCount(); i++) menu_items[n_menu++] = settings::fav(i);
  menu_items[n_menu++] = M_BROWSE;
  menu_items[n_menu++] = M_SCAN;
  menu_items[n_menu++] = M_DISCOVER;
  if (settings::active() >= 0) menu_items[n_menu++] = M_REMOVE;
  menu_items[n_menu++] = M_POWER;
  menu_items[n_menu++] = M_SOUND;
  menu_items[n_menu++] = M_AUTO;
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
  for (int b = 0; b < irdb::numBrands() && n_letters < (int)sizeof(letters); b++) {
    char c = letterOf(irdb::brandName(b));
    if (!memchr(letters, c, n_letters)) letters[n_letters++] = c;
  }
  std::sort(letters, letters + n_letters);
}

void buildBrands() {
  n_brand_list = 0;
  if (!brand_list) return;
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

// Device types that have remotes in the current search scope ("All types" first)
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

void buildDNames();                   // Discover: the list of key names (further down)

void go(Screen s, int new_sel = 0) {
  if (screen == S_REMOTE) { remote_sel = sel; remote_scroll = scroll; }
  screen = s; sel = new_sel; scroll = 0; dirty = true;
  if (s != S_SCAN) scan_auto = false;          // Auto only runs while its screen is showing
  // A key that is still held while the screen changes must not also act on the new screen.
  for (Key &k : keys) if (k.down) k.swallow = true;
  stopSend();
  switch (s) {
    case S_DNAME:   buildDNames(); break;
    case S_REMOTE:
      buildGrid();
      // back on the tile it was left on (same device), so a pad or the menu doesn't lose the place
      if (remote_for == settings::active()) { sel = remote_sel; scroll = remote_scroll; }
      remote_for = settings::active();
      if (sel >= n_grid) { sel = 0; scroll = 0; }
      break;
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
    case TILE_ALL:    return "All keys";
    case TILE_NAVPAD: return "Nav pad";
    case TILE_VOLPAD: return "Vol pad";
    case TILE_NUMBER: return "Ch 123";
    default:          return IR_KEY_LABELS[t];
  }
}

void drawRemote() {
  int p = settings::active();
  drawHeader(p >= 0 ? irdb::brandName(irdb::profileBrand(p)) : "Long-Range Remote", p >= 0 ? irdb::profileName(p) : "");

  if (irdb::numProfiles() == 0) {                // no codes at all: say how to get them
    g->setFont(u8g2_font_helvB14_tr);
    g->drawStr(12, 62, "No IR codes loaded");
    g->setFont(u8g2_font_helvR10_tr);
    drawClipped(12, 86, W - 24, sdstore::status());
    g->drawStr(12, 114, "Copy irdb.bin (firmware/sd_card) to the SD card,");
    g->drawStr(12, 132, "or upload it from a phone: join Wi-Fi \"" WIFI_AP_SSID "\"");
    char b[80]; snprintf(b, sizeof(b), "(password " WIFI_AP_PASSWORD "), open http://%s,", sta_ip[0] ? sta_ip : ap_ip);
    g->drawStr(12, 150, b);
    g->drawStr(12, 168, "Settings > SD card > Upload code database.");
    drawFooter(nullptr, "{OK} or {BACK} Menu");
    return;
  }
  if (p < 0) {
    g->setFont(u8g2_font_helvB14_tr);
    g->drawStr(12, 64, "No device chosen yet");
    g->setFont(u8g2_font_helvR12_tr);
    g->drawStr(12, 96, "Press OK, then pick your device's brand");
    g->drawStr(12, 116, "or run a code search.");
    g->setFont(u8g2_font_helvR10_tr);
    g->drawStr(12, 152, "Or set it up from a phone: join Wi-Fi \"" WIFI_AP_SSID "\"");
    char b[80]; snprintf(b, sizeof(b), "(password " WIFI_AP_PASSWORD ") and open http://%s", sta_ip[0] ? sta_ip : ap_ip);
    g->drawStr(12, 170, b);
    drawFooter(nullptr, "{OK} or {BACK} Menu");
    return;
  }
  const int TW = 95, TH = 41, X0 = 3, Y0 = HEADER_H + 4;
  if (sel >= n_grid) sel = 0;
  if (sel < 0) sel = n_grid - 1;
  int row = sel / GRID_COLS;
  if (row < scroll) scroll = row;
  if (row >= scroll + GRID_ROWS) scroll = row - GRID_ROWS + 1;
  for (int i = scroll * GRID_COLS; i < n_grid && i < (scroll + GRID_ROWS) * GRID_COLS; i++) {
    int r = i / GRID_COLS - scroll, c = i % GRID_COLS;
    int x = X0 + c * (TW + 2), y = Y0 + r * (TH + 3);
    const char *label = tileLabel(grid[i]);
    bool special = grid[i] < 0;                  // opens a screen instead of sending a code
    if (i == sel) {
      g->drawRBox(x, y, TW, TH, 6);
      g->setDrawColor(0);
      if (special) g->drawRFrame(x + 3, y + 3, TW - 6, TH - 6, 4);
    } else {
      g->drawRFrame(x, y, TW, TH, 6);
      if (special) g->drawRFrame(x + 3, y + 3, TW - 6, TH - 6, 4);
    }
    g->setFont(u8g2_font_helvB14_tr);
    if (g->getStrWidth(label) > TW - 10) g->setFont(u8g2_font_helvB10_tr);
    drawCentered(x + TW / 2, y + TH / 2 + 6, label);
    g->setDrawColor(1);
  }
  int total_rows = (n_grid + GRID_COLS - 1) / GRID_COLS;
  drawScrollbar(Y0, GRID_ROWS * (TH + 3) - 3, total_rows, GRID_ROWS, scroll);
  bool sp = sel < n_grid && grid[sel] < 0;
  drawFooterSent(sp ? "{OK} Open    {BACK} Menu" : "{OK} Send    {OK hold} Repeat    {BACK} Menu");
}

void drawPad() {
  const Pad &pad = PADS[cur_pad];
  int p = settings::active();
  drawHeader(pad.name, p >= 0 ? irdb::profileName(p) : "");
  // A picture of the control board's keys with what each one sends.
  // Layout matches the board: 4-way cross on the left, BACK at its lower right, big SELECT on the right.
  struct Rect { int x, y, w, h; } const rc[K__N] = {
    {110, 38, 84, 50}, {110, 196, 84, 50}, {16, 117, 84, 50}, {204, 117, 84, 50}, {300, 104, 92, 76},
    {204, 196, 84, 50}};
  for (int s = 0; s < K__N; s++) {
    uint8_t k = KEY_NONE;
    int c = padCode(cur_pad, s, &k);
    const char *lab = c >= 0 ? IR_KEY_LABELS[k] : (s == K_BACK ? "Leave" : "-");
    int x = rc[s].x, y = rc[s].y, w = rc[s].w, h = rc[s].h;
    bool active = keys[s].down && !keys[s].swallow;
    if (active) { g->drawRBox(x, y, w, h, 8); g->setDrawColor(0); }
    else g->drawRFrame(x, y, w, h, 8);
    g->setFont(u8g2_font_helvB14_tr);
    if (g->getStrWidth(lab) > w - 8) g->setFont(u8g2_font_helvB10_tr);
    drawCentered(x + w / 2, y + h / 2 + 6, lab);
    g->setDrawColor(1);
  }
  drawFooterSent(padCode(cur_pad, K_BACK) >= 0 ? "Each key sends what it shows    {BACK hold} Leave"
                                               : "Each key sends what it shows    {BACK} Leave");
}

void drawNumber() {
  int p = settings::active();
  drawHeader("Channel", p >= 0 ? irdb::profileName(p) : "");
  // the number so far, the digit being chosen in a box
  g->setFont(u8g2_font_logisoso42_tn);
  char done[8]; snprintf(done, sizeof(done), "%s", num_digits);
  char cur[2] = {(char)('0' + num_cur), 0};
  int wd = g->getStrWidth(done), wc = g->getStrWidth("0");
  int x = W / 2 - (wd + wc + 8) / 2, y = 122;
  g->drawStr(x, y, done);
  int cx = x + wd + 4;
  g->drawRFrame(cx - 5, y - 51, wc + 10, 62, 6);
  g->drawStr(cx, y, cur);
  // small arrows above and below the box: UP / DOWN change this digit
  g->drawTriangle(cx + wc / 2 - 6, y - 55, cx + wc / 2 + 6, y - 55, cx + wc / 2, y - 62);
  g->drawTriangle(cx + wc / 2 - 6, y + 15, cx + wc / 2 + 6, y + 15, cx + wc / 2, y + 22);
  int hw1 = drawHints(0, 0, "{UP}{DOWN} Digit    {RIGHT} Next digit    {LEFT} Delete", false);
  drawHints(W / 2 - hw1 / 2, 176, "{UP}{DOWN} Digit    {RIGHT} Next digit    {LEFT} Delete", true);
  if (num_send[0]) {
    char b[48]; snprintf(b, sizeof(b), "Sending %s ...", num_send);
    g->setFont(u8g2_font_helvB12_tr);
    drawCentered(W / 2, 212, b);
  } else {
    g->setFont(u8g2_font_helvR10_tr);
    drawCentered(W / 2, 210, "The digits are sent one after another, like typing them.");
  }
  drawFooterSent("{OK} Send the number    {BACK} Back");
}

void drawAllKeys() {
  int p = settings::active();
  drawHeader("All keys", irdb::profileName(p));
  int c0 = irdb::profileFirstCode(p), n = irdb::profileCodeCount(p);
  drawList(n, [&](int i, Row &r) {
    const IrCode &c = irdb::code(c0 + i);
    snprintf(r.text, sizeof(r.text), "%s", irdb::codeName(c0 + i));
    snprintf(r.value, sizeof(r.value), "%s", irProtoName(c.proto));
  });
  drawFooterSent("{OK} Send    {OK hold} Repeat    {BACK} Back");
}

void drawMenu() {
  drawHeader("Menu", "");
  int first_tool = settings::favCount();
  drawList(n_menu, [&](int i, Row &r) {
    int m = menu_items[i];
    r.rule = i == first_tool || m == M_POWER;
    switch (m) {
      case M_BROWSE: snprintf(r.text, sizeof(r.text), "Add a device: pick the brand"); r.more = true; break;
      case M_SCAN:   snprintf(r.text, sizeof(r.text), "Add a device: code search"); r.more = true; break;
      case M_DISCOVER: snprintf(r.text, sizeof(r.text), "Add a device: not in the list"); r.more = true; break;
      case M_REMOVE: snprintf(r.text, sizeof(r.text), "Remove the current device"); break;
      case M_POWER: {
        static const char *const names[LED_POWER_LEVELS] = LED_POWER_NAMES;
        snprintf(r.text, sizeof(r.text), "LED power");
        snprintf(r.value, sizeof(r.value), "%s", names[settings::ledPower()]);
        break;
      }
      case M_SOUND:
        snprintf(r.text, sizeof(r.text), "Key sounds");
        if (!sound::ready()) snprintf(r.value, sizeof(r.value), "%s", sound::status());
        else if (settings::soundMuted()) snprintf(r.value, sizeof(r.value), "muted");
        else snprintf(r.value, sizeof(r.value), "volume %d of %d", settings::soundVolume(), SOUND_VOLUME_MAX);
        break;
      case M_AUTO: {
        char s[12]; settings::formatSeconds(settings::scanAutoMs(), s, sizeof(s));
        snprintf(r.text, sizeof(r.text), "Code search Auto");
        snprintf(r.value, sizeof(r.value), "a code every %s", s);
        break;
      }
      case M_WIFI: {
        String s = settings::wifiSsid();
        snprintf(r.text, sizeof(r.text), "Wi-Fi");
        snprintf(r.value, sizeof(r.value), "%s", s.length() ? s.c_str() : "not set");
        r.more = true;
        break;
      }
      case M_STATUS: snprintf(r.text, sizeof(r.text), "Status"); r.more = true; break;
      default:
        r.mark = m == settings::active() ? 2 : 1;
        snprintf(r.text, sizeof(r.text), "%s", irdb::brandName(irdb::profileBrand(m)));
        snprintf(r.value, sizeof(r.value), "%s", irdb::profileName(m));
    }
  });
  int m = sel < n_menu ? menu_items[sel] : M_STATUS;
  switch (m) {
    case M_BROWSE: { char b[64]; snprintf(b, sizeof(b), "%d brands in the code database.", irdb::numBrands());
                     drawFooter(b, "{OK} Open    {BACK} Remote"); break; }
    case M_SCAN:   drawFooter("Tries codes one by one until your device reacts.", "{OK} Open    {BACK} Remote"); break;
    case M_DISCOVER: drawFooter("Discover: tries every protocol, then you name the keys.", "{OK} Open    {BACK} Remote"); break;
    case M_REMOVE: drawFooter("Takes it off this list. The codes stay in the database.", "{OK} Remove    {BACK} Remote"); break;
    case M_POWER:  drawFooter("Full for long range; Half and Low save the AA cells.", "{OK} Change    {BACK} Remote"); break;
    case M_SOUND:  drawFooter("A click on each key press.", "{LEFT}{RIGHT} Volume    {OK} Mute    {BACK} Remote"); break;
    case M_AUTO:   drawFooter("Code search Auto speed, here and on the web page.", "{LEFT} Faster    {RIGHT} Slower    {BACK} Remote"); break;
    case M_WIFI:   drawFooter("Your home network, for the web remote.", "{OK} Open    {BACK} Remote"); break;
    case M_STATUS: drawFooter("Batteries, SD card, web addresses.", "{OK} Open    {BACK} Remote"); break;
    default:       drawFooter("Your devices. The dot marks the one in use.", "{OK} Use this device    {BACK} Remote");
  }
}

void drawLetters() {
  drawHeader(browse_for_scan ? "Code search" : "Add a device", "brand's first letter");
  const int COLS = LETTER_COLS, TW = 54, TH = 52, X0 = 5, Y0 = HEADER_H + 6;
  if (sel >= n_letters) sel = 0;
  if (sel < 0) sel = n_letters - 1;
  g->setFont(u8g2_font_helvB18_tr);
  for (int i = 0; i < n_letters; i++) {
    int x = X0 + (i % COLS) * (TW + 2), y = Y0 + (i / COLS) * (TH + 3);
    char s[2] = {letters[i], 0};
    if (i == sel) { g->drawRBox(x, y, TW, TH, 6); g->setDrawColor(0); }
    else g->drawRFrame(x, y, TW, TH, 6);
    drawCentered(x + TW / 2, y + TH / 2 + 9, s);
    g->setDrawColor(1);
  }
  drawFooter(nullptr, "{OK} Choose    {BACK} Back        # = other brands (3M, ...)");
}

void drawBrands() {
  char t[24]; snprintf(t, sizeof(t), "Brands: %c", cur_letter);
  drawHeader(t, browse_for_scan ? "code search" : "");
  drawList(n_brand_list, [&](int i, Row &r) {
    int br = brand_list[i];
    int n = irdb::brandProfileCount(br);
    snprintf(r.text, sizeof(r.text), "%s", irdb::brandName(br));
    snprintf(r.value, sizeof(r.value), "%d remote%s", n, n == 1 ? "" : "s");
    r.more = true;
  });
  drawFooter(nullptr, "{OK} Choose    {BACK} Back        hold {UP}{DOWN} = fast");
}

void drawProfiles() {
  drawHeader(irdb::brandName(cur_brand), "choose the remote");
  int p0 = irdb::brandFirstProfile(cur_brand), n = irdb::brandProfileCount(cur_brand);
  drawList(n, [&](int i, Row &r) {
    int p = p0 + i;
    snprintf(r.text, sizeof(r.text), "%s", irdb::profileName(p));
    snprintf(r.value, sizeof(r.value), "%s, %d keys", IR_CAT_NAMES[irdb::profileCategory(p)], irdb::profileCodeCount(p));
  });
  drawFooter("Pick the closest model. If none works, try a code search.", "{OK} Use this remote    {BACK} Back");
}

// "Search in" list for code search: All brands, each brand group, Pick one brand...
// item i: 0 = all, 1..groups = group i-1, last = pick a brand
int scanSrcCount() { return irdb::numGroups() + 2; }

void drawScanSrc() {
  drawHeader("Code search", "where to search");
  drawList(scanSrcCount(), [&](int i, Row &r) {
    int ng = irdb::numGroups();
    r.more = true;
    if (i == 0) { snprintf(r.text, sizeof(r.text), "All brands"); snprintf(r.value, sizeof(r.value), "%d brands", irdb::numBrands()); }
    else if (i - 1 < ng) { snprintf(r.text, sizeof(r.text), "%s", irdb::groupName(i - 1));
                           snprintf(r.value, sizeof(r.value), "%d brands", irdb::groupBrandCount(i - 1)); }
    else { snprintf(r.text, sizeof(r.text), "One brand..."); r.rule = true; }
  });
  drawFooter("Narrower = fewer codes to try. Duplicates are skipped.", "{OK} Choose    {BACK} Back");
}

void drawScanCat() {
  drawHeader("Code search", "kind of device");
  drawList(n_cat_list, [&](int i, Row &r) {
    int c = cat_list[i];
    snprintf(r.text, sizeof(r.text), "%s", c < 0 ? "All kinds" : catLabel(c));
    snprintf(r.value, sizeof(r.value), "%d remote%s", cat_count[i], cat_count[i] == 1 ? "" : "s");
    r.more = true;
  });
  drawFooter("Choosing the kind skips codes for other devices.", "{OK} Choose    {BACK} Back");
}

void drawScanKey() {
  drawHeader("Code search", "key to test with");
  drawList(N_SCAN_KEYS, [&](int i, Row &r) {
    snprintf(r.text, sizeof(r.text), "%s", IR_KEY_LABELS[SCAN_KEYS[i]]);
    r.more = true;
  });
  drawFooter("Aim at the device. Each step sends a different code.", "{OK} Start    {BACK} Back");
}

void drawScan() {
  drawHeader("Code search", IR_KEY_LABELS[scan_key]);
  if (n_scan == 0) {
    g->setFont(u8g2_font_helvB14_tr);
    g->drawStr(12, 70, "No remotes here have this key");
    g->setFont(u8g2_font_helvR12_tr);
    g->drawStr(12, 98, "Press BACK and pick another key or kind.");
    drawFooter(nullptr, "{BACK} Back");
    return;
  }
  int p = scan_cands[scan_pos].profile;
  char b[96];
  // progress: "12 of 48" with a bar
  g->setFont(u8g2_font_logisoso32_tn);
  snprintf(b, sizeof(b), "%d", scan_pos + 1);
  int nw = g->getStrWidth(b);
  char of[24]; snprintf(of, sizeof(of), "of %d", n_scan);
  g->setFont(u8g2_font_helvR12_tr);
  int ow = g->getStrWidth(of);
  int x = W / 2 - (nw + 8 + ow) / 2;
  g->drawStr(x + nw + 8, 78, of);
  g->setFont(u8g2_font_logisoso32_tn);
  g->drawStr(x, 80, b);
  g->drawFrame(40, 90, W - 80, 7);
  g->drawBox(40, 90, max(3, (W - 80) * (scan_pos + 1) / n_scan), 7);
  g->setFont(u8g2_font_helvB14_tr);
  drawCenteredFit(124, irdb::brandName(irdb::profileBrand(p)));
  g->setFont(u8g2_font_helvR12_tr);
  snprintf(b, sizeof(b), "%s  (%s)", irdb::profileName(p), IR_CAT_NAMES[irdb::profileCategory(p)]);
  drawCenteredFit(146, b);
  g->setFont(u8g2_font_helvR10_tr);
  char secs[12]; settings::formatSeconds(settings::scanAutoMs(), secs, sizeof(secs));
  if (scan_auto) {                       // the Auto line, inverted so it is seen from across the room
    g->setFont(u8g2_font_helvB10_tr);
    snprintf(b, sizeof(b), "AUTO  -  a code every %s", secs);
    int w = g->getStrWidth(b) + 16;
    g->drawRBox(W / 2 - w / 2, 153, w, 17, 3);
    g->setDrawColor(0); drawCentered(W / 2, 166, b); g->setDrawColor(1);
  } else if (scan_msg[0]) {
    g->setFont(u8g2_font_helvB10_tr);
    drawCenteredFit(166, scan_msg);
  } else if (scan_cands[scan_pos].shared > 1) {
    snprintf(b, sizeof(b), "the same code is on %u remotes", (unsigned)scan_cands[scan_pos].shared);
    drawCenteredFit(165, b);
  }
  // the two answers, as big as the question
  g->drawRFrame(10, 178, 186, 76, 6);
  g->drawRFrame(204, 178, 186, 76, 6);
  g->setFont(u8g2_font_helvB12_tr);
  drawCentered(103, 199, "It reacted");
  drawCentered(297, 199, scan_auto ? "Auto is running" : "Nothing happened");
  int w1 = drawHints(0, 0, "{OK hold} Use this remote", false);
  drawHints(103 - w1 / 2, 224, "{OK hold} Use this remote", true);
  const char *right = scan_auto ? "{DOWN} Stop Auto" : "{RIGHT} Next code";
  int w2 = drawHints(0, 0, right, false);
  drawHints(297 - w2 / 2, 224, right, true);
  g->setFont(u8g2_font_helvR08_tr);
  drawCentered(103, 245, scan_auto ? "OK = stop here" : "OK = send it again");
  if (scan_auto) drawCentered(297, 245, "UP = change the speed");
  else { snprintf(b, sizeof(b), "DOWN = Auto, a code every %s", secs); drawCentered(297, 245, b); }
  drawFooterSent(scan_auto ? "{DOWN} Stop    {UP} Speed    {OK hold} Use this"
                           : "{RIGHT} Next    {LEFT} Prev    {DOWN} Auto    {UP} Speed");
}

void drawStatus() {
  drawHeader("Status", "");
  char b[112];
  const int LX = 10, VX = 104, DY = 18;
  int y = 50;
  auto label = [&](const char *t) { g->setFont(u8g2_font_helvB10_tr); g->drawStr(LX, y, t); g->setFont(u8g2_font_helvR10_tr); };
  auto value = [&](const char *t) { drawClipped(VX, y, W - VX - 8, t); y += DY; };
  auto battery = [&](int pct, float volts, const char *extra) {
    drawBattery(VX, y - 10, pct);
    if (pct < 0) snprintf(b, sizeof(b), "%s", extra);
    else snprintf(b, sizeof(b), "%d%%   %.2f V%s%s", pct, volts, *extra ? "   " : "", extra);
    drawClipped(VX + 30, y, W - VX - 38, b); y += DY;
  };
  float v = hw::packVolts();
  label("AA cells");
  if (isnan(v) || v < 1) battery(-1, 0, "none in - the LEDs are off");
  else battery(hw::packPercent(), v, batteryLow() ? "LOW - put in charged cells" : "");
  float sv = hw::screenVolts();
  label("Screen");
  if (sv < 2.0f) battery(-1, 0, "battery not readable");
  else battery(hw::screenPercent(), sv, "");
  if (uint8_t st = hw::stuckKeys()) {
    label("Keys");
    snprintf(b, sizeof(b), "stuck or shorted:%s%s%s%s%s%s", st & (1 << KEYBIT_UP) ? " UP" : "",
             st & (1 << KEYBIT_DOWN) ? " DOWN" : "", st & (1 << KEYBIT_LEFT) ? " LEFT" : "",
             st & (1 << KEYBIT_RIGHT) ? " RIGHT" : "", st & (1 << KEYBIT_SELECT) ? " SELECT" : "",
             st & (1 << KEYBIT_BACK) ? " BACK" : "");
    value(b);
  }
  label("SD card"); value(sdstore::status());
  label("Codes");
  snprintf(b, sizeof(b), "%d brands, %d remotes, %d codes", irdb::numBrands(), irdb::numProfiles(), irdb::numCodes());
  value(b);
  if (irdb::numUserProfiles()) {
    snprintf(b, sizeof(b), "incl. %d of your own remotes (.ir files)", irdb::numUserProfiles());
    value(b);
  }
  g->drawHLine(LX, y - 11, W - 2 * LX); y += 6;
  label("Phone link");
  snprintf(b, sizeof(b), "\"%s\" (%s)  http://%s", WIFI_AP_SSID, WIFI_AP_PASSWORD, ap_ip); value(b);
  label("Home Wi-Fi");
  if (sta_ip[0]) {
    snprintf(b, sizeof(b), "\"%s\"", sta_ssid); value(b);
    snprintf(b, sizeof(b), "http://%s  or  http://%s.local", sta_ip, MDNS_HOSTNAME); value(b);
  } else {
    String s = settings::wifiSsid();
    if (s.length()) { snprintf(b, sizeof(b), "\"%s\"", s.c_str()); value(b); value(wifimgr::statusText()); }
    else value("not set  (Menu > Wi-Fi)");
  }
  label("Web page");
  if (!web_count) value("no requests yet - open the page and watch here");
  else {
    uint32_t s = (millis() - web_at) / 1000;
    const char *pl = web_count == 1 ? "" : "s";
    if (s < 2) snprintf(b, sizeof(b), "just now from %s  (%lu request%s)", web_ip, (unsigned long)web_count, pl);
    else snprintf(b, sizeof(b), "%lu s ago from %s  (%lu request%s)", (unsigned long)s, web_ip, (unsigned long)web_count, pl);
    value(b);
  }
  drawFooter(nullptr, "{any key} Back");
}

// ---- Discover: a device that isn't in the database (discover.h) ----
int d_names[KEY__COUNT + 2];          // S_DNAME rows: key numbers, -1 = keep it as "F nn"
int n_d_names = 0;
char d_msg[80] = "";                  // S_DDONE: result of saving
struct { int mode, pos, func, fam, nl; bool sending; } d_seen = {-1, -1, -1, -1, -1, false};

void buildDNames() {
  discover::State st = discover::state();
  n_d_names = 0;
  int guess = discover::guessKey(st.fam, st.func);
  if (guess) d_names[n_d_names++] = guess;
  for (uint8_t k : KEY_ORDER) if (k != guess && n_d_names < KEY__COUNT) d_names[n_d_names++] = k;
  d_names[n_d_names++] = -1;
}

void dTitle(char *out, size_t n, const discover::State &st) {
  char at[40];
  discover::addrText(st.fam, st.addr, at, sizeof(at));
  snprintf(out, n, "%s, %s", discover::family(st.fam).name, at);
}

void drawDFam() {
  drawHeader("Discover", "which protocol?");
  drawList(discover::numFamilies() + 1, [&](int i, Row &r) {
    r.more = true;
    if (i == 0) { snprintf(r.text, sizeof(r.text), "All of them"); snprintf(r.value, sizeof(r.value), "about 35 minutes"); }
    else {
      snprintf(r.text, sizeof(r.text), "%s", discover::family(i - 1).name);
      snprintf(r.value, sizeof(r.value), "%s", discover::family(i - 1).brands);
      r.rule = i == 1;
    }
  });
  drawFooter("Tries every address until your device does something.", "{OK} Start    {BACK} Back");
}

void dProgress(const discover::State &st, int y) {
  char b[64];
  snprintf(b, sizeof(b), "address %d of %d", st.pos + 1, st.total);
  g->setFont(u8g2_font_helvR12_tr);
  drawCentered(W / 2, y, b);
  g->drawFrame(40, y + 8, W - 80, 7);
  if (st.total > 0) g->drawBox(40, y + 8, max(3, (W - 80) * (st.pos + 1) / st.total), 7);
}

void drawDSweep() {
  discover::State st = discover::state();
  drawHeader("Discover", "sweeping");
  char b[96], at[40];
  discover::addrText(st.fam, st.addr, at, sizeof(at));
  g->setFont(u8g2_font_helvB18_tr);
  drawCenteredFit(68, discover::family(st.fam).name);
  g->setFont(u8g2_font_helvB14_tr);
  drawCenteredFit(92, at);
  dProgress(st, 116);
  g->setFont(u8g2_font_helvR10_tr);
  if (st.all) snprintf(b, sizeof(b), "protocol %d of %d, about %lu min left", st.fam_pos + 1, st.fam_total, (unsigned long)((st.eta_s + 59) / 60));
  else if (st.eta_s >= 90) snprintf(b, sizeof(b), "about %lu min left", (unsigned long)((st.eta_s + 59) / 60));
  else snprintf(b, sizeof(b), "about %lu s left", (unsigned long)st.eta_s);
  drawCentered(W / 2, 150, b);
  g->drawRFrame(10, 166, W - 20, 86, 6);
  g->setFont(u8g2_font_helvB14_tr);
  drawCentered(W / 2, 190, "Aim at the device and watch it.");
  g->setFont(u8g2_font_helvR12_tr);
  drawCentered(W / 2, 212, "The moment it does anything");
  int hw1 = drawHints(0, 0, "(on, off, volume, mute):  press {OK}", false);
  drawHints(W / 2 - hw1 / 2, 236, "(on, off, volume, mute):  press {OK}", true);
  drawFooterHints(st.all ? "{OK} It reacted    {RIGHT} Skip this protocol    {BACK} Stop" : "{OK} It reacted    {BACK} Stop");
}

void drawDNarrow() {
  discover::State st = discover::state();
  drawHeader("Discover", "which address was it?");
  char at[40];
  discover::addrText(st.fam, st.addr, at, sizeof(at));
  g->setFont(u8g2_font_helvB18_tr);
  drawCenteredFit(68, discover::family(st.fam).name);
  g->setFont(u8g2_font_helvB14_tr);
  drawCenteredFit(92, at);
  dProgress(st, 116);
  g->setFont(u8g2_font_helvB12_tr);
  drawCentered(W / 2, 152, st.sending ? "trying this address now..." : "tried - did the device react?");
  g->setFont(u8g2_font_helvR10_tr);
  if (st.done) {
    drawCentered(W / 2, 178, "That was the last address and nothing reacted so far.");
    drawCentered(W / 2, 196, "LEFT steps back through them; BACK leaves.");
  } else {
    drawCentered(W / 2, 178, "A reaction shows up late, so this is a few addresses back.");
    drawCentered(W / 2, 196, "Step forward one address at a time until it reacts again.");
  }
  int w1 = drawHints(0, 0, "It reacted to this one:  {OK hold}", false);
  drawHints(W / 2 - w1 / 2, 236, "It reacted to this one:  {OK hold}", true);
  drawFooterHints(st.done ? "{LEFT} Previous   {RIGHT} Next   {OK} Again   {BACK} Leave"
                          : "{RIGHT} Next   {LEFT} Previous   {OK} Again   {BACK} Resume");
}

void drawDLearn() {
  discover::State st = discover::state();
  char t[80], b[96];
  dTitle(t, sizeof(t), st);
  drawHeader("Learn keys", t);
  g->setFont(u8g2_font_helvR12_tr);
  drawCentered(W / 2, 56, "function");
  g->setFont(u8g2_font_logisoso32_tn);
  snprintf(b, sizeof(b), "%d", st.func);
  drawCentered(W / 2, 98, b);
  g->setFont(u8g2_font_helvR10_tr);
  snprintf(b, sizeof(b), "%d of %d, the likely ones first", st.func_pos + 1, discover::numFuncs(st.fam));
  drawCentered(W / 2, 118, b);
  // what it is: named by the user, or what other remotes use it for
  const char *named = nullptr;
  for (int i = 0; i < st.n_learned; i++) if (discover::learned(i).func == st.func) named = discover::learned(i).name;
  uint8_t guess = discover::guessKey(st.fam, st.func);
  g->drawRFrame(10, 130, W - 20, 54, 6);
  if (named) {
    g->setFont(u8g2_font_helvR10_tr);
    drawCentered(W / 2, 150, "you named it");
    g->setFont(u8g2_font_helvB14_tr);
    drawCenteredFit(172, named);
  } else {
    g->setFont(u8g2_font_helvR10_tr);
    drawCentered(W / 2, 150, guess ? "on other remotes this is usually" : "not used by any remote in the database");
    g->setFont(u8g2_font_helvB14_tr);
    drawCenteredFit(172, guess ? IR_KEY_LABELS[guess] : "?");
  }
  g->setFont(u8g2_font_helvR10_tr);
  int w1 = drawHints(0, 0, "Did the device do something?  {OK hold} Name this key", false);
  drawHints(W / 2 - w1 / 2, 208, "Did the device do something?  {OK hold} Name this key", true);
  g->setFont(u8g2_font_helvB10_tr);
  snprintf(b, sizeof(b), "%d key%s named%s", st.n_learned, st.n_learned == 1 ? "" : "s",
           st.n_learned ? "  -  BACK when you have enough" : "");
  drawCentered(W / 2, 236, b);
  drawFooterHints("{RIGHT} Next    {LEFT} Previous    {OK} Again    {BACK} Finish");
}

void drawDName() {
  discover::State st = discover::state();
  char t[40]; snprintf(t, sizeof(t), "function %d does...", st.func);
  drawHeader("Name it", t);
  uint8_t guess = discover::guessKey(st.fam, st.func);
  drawList(n_d_names, [&](int i, Row &r) {
    int k = d_names[i];
    if (k < 0) { snprintf(r.text, sizeof(r.text), "Something else: keep it as \"F %d\"", st.func); r.rule = true; }
    else {
      snprintf(r.text, sizeof(r.text), "%s", IR_KEY_LABELS[k]);
      if (i == 0 && k == guess) snprintf(r.value, sizeof(r.value), "usual for this function");
      r.rule = i == 1 && guess;
    }
  });
  drawFooter("Named keys get their tile on the remote screen.", "{OK} Choose    {BACK} Back        hold {UP}{DOWN} = fast");
}

void drawDDone() {
  discover::State st = discover::state();
  char t[80];
  dTitle(t, sizeof(t), st);
  drawHeader("Discover", t);
  drawList(3, [&](int i, Row &r) {
    if (i == 0) { snprintf(r.text, sizeof(r.text), "Save as a device"); snprintf(r.value, sizeof(r.value), "%d key%s", st.n_learned, st.n_learned == 1 ? "" : "s"); }
    else if (i == 1) snprintf(r.text, sizeof(r.text), "Go on naming keys");
    else { snprintf(r.text, sizeof(r.text), "Throw it away"); r.rule = true; }
  });
  // the keys named so far
  char b[160] = "";
  for (int i = 0; i < st.n_learned && strlen(b) < sizeof(b) - 24; i++) {
    if (i) strcat(b, ", ");
    strcat(b, discover::learned(i).name);
  }
  g->setFont(u8g2_font_helvB10_tr);
  g->drawStr(10, LIST_TOP + 3 * ROW_H + 26, "Keys:");
  g->setFont(u8g2_font_helvR10_tr);
  drawClipped(56, LIST_TOP + 3 * ROW_H + 26, W - 64, b);
  char brand[24], remote[40], line[100];
  discover::fileNames(brand, sizeof(brand), remote, sizeof(remote));
  snprintf(line, sizeof(line), "It becomes \"%s\" under %s (/ir on the SD card).", remote, brand);
  drawClipped(10, LIST_TOP + 3 * ROW_H + 46, W - 20, line);
  drawFooter(d_msg[0] ? d_msg : "Saving restarts the remote (a few seconds).", "{OK} Choose    {BACK} Back");
}

// The engine is shared with the web page: follow it when it moves on (or was stopped) from there.
void dFollow() {
  if (screen < S_DSWEEP || screen > S_DDONE) return;
  discover::State st = discover::state();
  if (discover::restartPending()) return;
  if (st.mode == discover::IDLE) { go(S_MENU); return; }
  if (st.mode == discover::SWEEP && screen != S_DSWEEP) go(S_DSWEEP);
  else if (st.mode == discover::NARROW && screen != S_DNARROW) go(S_DNARROW);
  else if (st.mode == discover::LEARN && (screen == S_DSWEEP || screen == S_DNARROW)) go(S_DLEARN);
  int shown = screen == S_DLEARN ? st.func : st.pos;
  if (st.mode != d_seen.mode || shown != d_seen.pos || st.fam != d_seen.fam || st.n_learned != d_seen.nl || st.sending != d_seen.sending) {
    d_seen.mode = st.mode; d_seen.pos = shown; d_seen.fam = st.fam; d_seen.nl = st.n_learned; d_seen.sending = st.sending;
    dirty = true;
  }
}

// ---- Wi-Fi ----
bool wifiHasNetwork() { return settings::wifiSsid().length() > 0; }
int wifiRows() { return wifiHasNetwork() ? 3 : 1; }

void drawWifi() {
  drawHeader("Wi-Fi", "your home network");
  if (sel != W_FORGET) forget_armed = false;
  String ssid = settings::wifiSsid(), pass = settings::wifiPass();
  drawList(wifiRows(), [&](int i, Row &r) {
    switch (i) {
      case W_NETWORK:
        snprintf(r.text, sizeof(r.text), "Network");
        snprintf(r.value, sizeof(r.value), "%s", ssid.length() ? ssid.c_str() : "none - choose one");
        r.more = true;
        break;
      case W_PASSWORD:
        snprintf(r.text, sizeof(r.text), "Password");
        if (pass.length()) snprintf(r.value, sizeof(r.value), "%u characters", (unsigned)pass.length());
        else snprintf(r.value, sizeof(r.value), "none (open network)");
        r.more = true;
        break;
      case W_FORGET:
        snprintf(r.text, sizeof(r.text), forget_armed ? "Forget it?  OK again = yes" : "Forget this network");
        r.rule = true;
        break;
    }
  });
  char b[140];
  const int LX = 10, VX = 84;
  int y = LIST_TOP + 3 * ROW_H + 26;
  auto line = [&](const char *lab, const char *val) {
    g->setFont(u8g2_font_helvB10_tr); g->drawStr(LX, y, lab);
    g->setFont(u8g2_font_helvR10_tr); drawClipped(VX, y, W - VX - 8, val); y += 19;
  };
  if (!ssid.length()) line("Status", "no home network set");
  else if (sta_ip[0]) { snprintf(b, sizeof(b), "connected - web remote at http://%s", sta_ip); line("Status", b); }
  else line("Status", wifimgr::statusText());
  line("Kept in", settings::wifiSource());
  if (wifi_msg[0]) { g->setFont(u8g2_font_helvB10_tr); drawClipped(LX, y, W - 20, wifi_msg); }
  drawFooter(nullptr, sel == W_FORGET ? "{OK} Forget    {BACK} Back" : "{OK} Change    {BACK} Back");
}

const char *signalWord(int rssi) {
  return rssi >= -60 ? "strong" : rssi >= -70 ? "good" : rssi >= -80 ? "weak" : "very weak";
}

// Network list rows: the networks (or one "scanning" row), then "type a name", "scan again".
int netRows() { return (n_net_list < 0 ? 1 : n_net_list) + 2; }
int netRowOther() { return netRows() - 2; }
int netRowRescan() { return netRows() - 1; }

void drawWifiNets() {
  drawHeader("Wi-Fi", "nearby networks");
  String cur = settings::wifiSsid();
  drawList(netRows(), [&](int i, Row &r) {
    if (i == netRowOther()) { snprintf(r.text, sizeof(r.text), "Hidden network: type its name"); r.more = true; r.rule = true; }
    else if (i == netRowRescan()) snprintf(r.text, sizeof(r.text), "Scan again");
    else if (n_net_list < 0) snprintf(r.text, sizeof(r.text), "Scanning...");
    else {
      const wifimgr::Network &n = net_list[i];
      r.mark = cur == n.ssid ? 2 : 1;
      snprintf(r.text, sizeof(r.text), "%s", n.ssid);
      snprintf(r.value, sizeof(r.value), "%s%s", signalWord(n.rssi), n.open ? ", open" : "");
    }
  });
  char line[80];
  if (n_net_list < 0) snprintf(line, sizeof(line), "Scanning for networks...");
  else if (n_net_list == 0) snprintf(line, sizeof(line), "No networks found - try Scan again");
  else snprintf(line, sizeof(line), "%d found (2.4 GHz only). The dot marks the one in use.", n_net_list);
  drawFooter(line, "{OK} Choose    {BACK} Back");
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
             "{OK} Type    {BACK} Delete    {BACK hold} Cancel");
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
    case S_DFAM:     drawDFam(); break;
    case S_DSWEEP:   drawDSweep(); break;
    case S_DNARROW:  drawDNarrow(); break;
    case S_DLEARN:   drawDLearn(); break;
    case S_DNAME:    drawDName(); break;
    case S_DDONE:    drawDDone(); break;
  }
  g->sendBuffer();
  dirty = false;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
int listSize() {
  switch (screen) {
    case S_ALLKEYS:  return irdb::profileCodeCount(settings::active());
    case S_MENU:     return n_menu;
    case S_BRANDS:   return n_brand_list;
    case S_PROFILES: return irdb::brandProfileCount(cur_brand);
    case S_SCANSRC:  return scanSrcCount();
    case S_SCANCAT:  return n_cat_list;
    case S_SCANKEY:  return N_SCAN_KEYS;
    case S_WIFI:     return wifiRows();
    case S_WIFINETS: return netRows();
    case S_DFAM:     return discover::numFamilies() + 1;
    case S_DNAME:    return n_d_names;
    case S_DDONE:    return 3;
    default:         return 1;
  }
}

void scanSendCurrent() {
  if (n_scan > 0) irtx::tap(scan_cands[scan_pos].code);
}

void scanAutoStop(const char *why) {
  if (!scan_auto) return;
  scan_auto = false;
  snprintf(scan_msg, sizeof(scan_msg), "%s", why);
  dirty = true;
}

void scanAutoStart() {
  if (n_scan <= 0) return;
  scan_auto = true;
  scan_msg[0] = 0;
  scan_last_at = millis();               // the code on screen was just tried: the next one after the gap
  dirty = true;
}

// From loop(): the next code when its time has come and the transmitter is free. The speed is read
// each time, so a change (UP, the menu or the web page) applies straight away.
void scanAutoLoop() {
  if (!scan_auto) return;
  if (screen != S_SCAN || n_scan <= 0) { scan_auto = false; return; }
  if ((int32_t)(millis() - (scan_last_at + (uint32_t)settings::scanAutoMs())) < 0 || irtx::isSending()) return;
  if (scan_pos + 1 >= n_scan) { scanAutoStop("Auto reached the last code. LEFT steps back."); return; }
  scan_pos++;
  scanSendCurrent();
  scan_last_at = millis();
  dirty = true;
}

void startSend(int code) {
  if (code < 0) return;
  stopSend();
  irtx::holdStart(code);
  send_held = true;
}

// One screen back, landing on the row that led here.
void back() {
  switch (screen) {
    case S_PAD: case S_NUMBER: case S_ALLKEYS: case S_MENU: go(S_REMOTE); break;
    case S_LETTERS:
      if (browse_for_scan) go(S_SCANSRC, scanSrcCount() - 1);
      else { go(S_MENU); sel = menuIndex(M_BROWSE); }
      break;
    case S_SCANSRC: go(S_MENU); sel = menuIndex(M_SCAN); break;
    case S_BRANDS: {
      go(S_LETTERS);
      const char *at = (const char *)memchr(letters, cur_letter, n_letters);
      if (at) sel = (int)(at - letters);
      break;
    }
    case S_PROFILES: {
      int b = cur_brand;
      go(S_BRANDS);
      for (int i = 0; i < n_brand_list; i++) if (brand_list[i] == b) sel = i;
      break;
    }
    case S_SCANCAT:
      if (cur_brand >= 0) {
        int b = cur_brand;
        go(S_BRANDS);
        for (int i = 0; i < n_brand_list; i++) if (brand_list[i] == b) sel = i;
      } else {
        go(S_SCANSRC, scan_group >= 0 ? scan_group + 1 : 0);
      }
      break;
    case S_SCANKEY: {
      int c = scan_cat;
      go(S_SCANCAT);
      for (int i = 0; i < n_cat_list; i++) if (cat_list[i] == c) sel = i;
      break;
    }
    case S_SCAN: {
      go(S_SCANKEY);
      for (int i = 0; i < N_SCAN_KEYS; i++) if (SCAN_KEYS[i] == scan_key) sel = i;
      break;
    }
    case S_STATUS:  go(S_MENU); sel = menuIndex(M_STATUS); break;
    case S_DFAM:    go(S_MENU); sel = menuIndex(M_DISCOVER); break;
    case S_DSWEEP: {
      discover::State st = discover::state();
      discover::stop();
      go(S_DFAM, st.all ? 0 : st.fam + 1);
      break;
    }
    case S_DNARROW:
      if (discover::state().done) { discover::stop(); go(S_DFAM); }
      else { discover::resumeSweep(); go(S_DSWEEP); }
      break;
    case S_DLEARN:
      if (discover::state().n_learned) { d_msg[0] = 0; go(S_DDONE); }
      else { discover::stop(); go(S_DFAM); }
      break;
    case S_DNAME:   go(S_DLEARN); break;
    case S_DDONE:   go(S_DLEARN); break;
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
      if (n_grid == 0) { go(S_MENU); break; }      // no device yet: the menu is the only way on
      if (sel >= n_grid) break;
      if (grid[sel] == TILE_ALL) go(S_ALLKEYS);
      else if (grid[sel] == TILE_NAVPAD) { cur_pad = 0; go(S_PAD); }
      else if (grid[sel] == TILE_VOLPAD) { cur_pad = 1; go(S_PAD); }
      else if (grid[sel] == TILE_NUMBER) { num_len = 0; num_digits[0] = 0; num_cur = digitAvail(0) ? 0 : 1; go(S_NUMBER); }
      break;
    case S_MENU: {
      if (sel >= n_menu) break;
      int m = menu_items[sel];
      if (m == M_BROWSE) { browse_for_scan = false; go(S_LETTERS); }
      else if (m == M_SCAN) { browse_for_scan = true; go(S_SCANSRC); }
      else if (m == M_DISCOVER) go(S_DFAM);
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
      if (sel < n_letters) { cur_letter = letters[sel]; go(S_BRANDS); }
      break;
    case S_BRANDS:
      if (sel < n_brand_list) {
        cur_brand = brand_list[sel];
        go(browse_for_scan ? S_SCANCAT : S_PROFILES);
      }
      break;
    case S_PROFILES:
      if (sel < irdb::brandProfileCount(cur_brand)) { settings::setActive(irdb::brandFirstProfile(cur_brand) + sel); go(S_REMOTE); }
      break;
    case S_SCANSRC: {
      int ng = irdb::numGroups();
      if (sel == 0) { cur_brand = -1; scan_group = -1; go(S_SCANCAT); }
      else if (sel - 1 < ng) { cur_brand = -1; scan_group = sel - 1; go(S_SCANCAT); }
      else { scan_group = -1; go(S_LETTERS); }
      break;
    }
    case S_SCANCAT:
      if (sel < n_cat_list) { scan_cat = cat_list[sel]; go(S_SCANKEY); }
      break;
    case S_SCANKEY:
      if (sel < N_SCAN_KEYS) { scan_key = SCAN_KEYS[sel]; buildScan(); go(S_SCAN); scanSendCurrent(); }
      break;
    case S_STATUS:
      back();
      break;
    case S_DFAM:
      discover::sweepStart(sel == 0 ? -1 : sel - 1);
      go(S_DSWEEP);
      break;
    case S_DNAME:
      if (sel < n_d_names) {
        if (d_names[sel] < 0) discover::learnName(0, nullptr);
        else discover::learnName((uint8_t)d_names[sel], nullptr);
        go(S_DLEARN);
      }
      break;
    case S_DDONE:
      if (discover::restartPending()) break;
      if (sel == 0) {
        char err[64];
        if (discover::saveAndRestart(err, sizeof(err))) snprintf(d_msg, sizeof(d_msg), "Saved. Restarting...");
        else snprintf(d_msg, sizeof(d_msg), "Not saved: %s", err);
        dirty = true;
      } else if (sel == 1) go(S_DLEARN);
      else { discover::stop(); go(S_MENU); }
      break;
    case S_WIFI:
      if (sel == W_NETWORK) { n_net_list = -1; wifimgr::requestNetworks(); go(S_WIFINETS); }
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
      if (sel == netRowRescan()) { n_net_list = -1; wifimgr::requestNetworks(); sel = 0; dirty = true; }
      else if (sel == netRowOther()) openText(T_SSID, String(""));
      else if (n_net_list > 0 && sel < n_net_list) {
        const wifimgr::Network &n = net_list[sel];
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
  // on the remote screen, the top level, it opens the menu.
  if (k == K_BACK && screen == S_SCAN && scan_auto) {       // BACK stops Auto first, a second press leaves
    if (a == A_PRESS) scanAutoStop("Auto stopped. Slow device? LEFT steps back a code.");
    return;
  }
  if (k == K_BACK && screen != S_PAD && screen != S_TEXT) {
    if (a == A_PRESS) { if (screen == S_REMOTE) go(S_MENU); else back(); }
    return;
  }
  // Menu > Code search Auto: LEFT / RIGHT = faster / slower
  if (screen == S_MENU && sel < n_menu && menu_items[sel] == M_AUTO && (k == K_LEFT || k == K_RIGHT)) {
    if (a == A_PRESS || a == A_REPEAT) {
      settings::setScanAutoMs(settings::scanAutoStep(settings::scanAutoMs(), k == K_RIGHT ? 1 : -1));
      dirty = true;
    }
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
          else choose();                           // a pad / number / all-keys tile, or no device: the menu
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
          if (sel < listSize()) startSend(irdb::profileFirstCode(settings::active()) + sel);
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
        gridMove(k, LETTER_COLS, n_letters);
      }
      break;
    }
    // ---- code search ----
    // RIGHT next, LEFT previous (hold: another key), OK again (hold: use this remote),
    // DOWN Auto on / off, UP Auto speed. While Auto runs, pressing OK, LEFT or RIGHT stops it first.
    case S_SCAN: {
      if (k == K_UP && a == A_PRESS) {           // Auto speed: the next slower step, after the slowest the fastest
        int ms = settings::scanAutoMs(), nx = settings::scanAutoStep(ms, 1);
        settings::setScanAutoMs(nx != ms ? nx : settings::scanAutoStep(0, 0));
        char s[12]; settings::formatSeconds(settings::scanAutoMs(), s, sizeof(s));
        if (!scan_auto) snprintf(scan_msg, sizeof(scan_msg), "Auto speed: a code every %s  (DOWN starts it)", s);
        dirty = true;
        break;
      }
      if (k == K_DOWN && a == A_PRESS) {
        if (scan_auto) scanAutoStop("Auto stopped. Slow device? LEFT steps back a code.");
        else scanAutoStart();
        break;
      }
      if (scan_auto && a == A_PRESS && (k == K_SELECT || k == K_LEFT || k == K_RIGHT)) {
        scanAutoStop("Auto stopped. Slow device? LEFT steps back a code.");   // stopped now: a code going by during an OK hold isn't the one used
        if (k == K_SELECT) scan_ok_stopped = true;
      }
      bool fwd = k == K_RIGHT && a == A_PRESS;
      bool rev = k == K_LEFT && a == A_RELEASE;
      if (fwd && n_scan) { scan_pos = (scan_pos + 1) % n_scan; scan_msg[0] = 0; scanSendCurrent(); }
      else if (rev && n_scan) { scan_pos = (scan_pos + n_scan - 1) % n_scan; scan_msg[0] = 0; scanSendCurrent(); }
      else if (k == K_SELECT && a == A_RELEASE) { if (!scan_ok_stopped) scanSendCurrent(); scan_ok_stopped = false; }
      else if (k == K_SELECT && a == A_LONG && n_scan) {     // "it works" -> use that code set
        scan_ok_stopped = false;
        settings::setActive(scan_cands[scan_pos].profile);
        go(S_REMOTE);
      }
      else if (k == K_LEFT && a == A_LONG) go(S_SCANKEY);
      break;
    }
    case S_STATUS:
      if (a == A_PRESS) back();
      break;
    // ---- Discover ----
    case S_DSWEEP:
      if (k == K_SELECT && a == A_PRESS) { discover::reacted(); go(S_DNARROW); }
      else if (k == K_RIGHT && a == A_PRESS) discover::skipFamily();
      break;
    case S_DNARROW:
      if (k == K_RIGHT && a == A_PRESS) discover::narrowStep(1);
      else if (k == K_LEFT && a == A_PRESS) discover::narrowStep(-1);
      else if (k == K_SELECT && a == A_RELEASE) discover::narrowStep(0);
      else if (k == K_SELECT && a == A_LONG) { discover::learnPick(); go(S_DLEARN); discover::learnStep(0); }
      break;
    case S_DLEARN:
      if (k == K_RIGHT && a == A_PRESS) discover::learnStep(1);
      else if (k == K_LEFT && a == A_PRESS) discover::learnStep(-1);
      else if (k == K_SELECT && a == A_RELEASE) discover::learnStep(0);
      else if (k == K_SELECT && a == A_LONG) go(S_DNAME);
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
  scanAutoLoop();
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
  uint32_t status_every = screen == S_WIFI || screen == S_STATUS ? 2000 : 10000;   // Wi-Fi / Status: connection, web requests
  if (millis() - last_status_draw > status_every) { last_status_draw = millis(); dirty = true; }   // battery
  dFollow();
  if (ir_shown && !irActive()) dirty = true;                    // the send is over: clear the header's IR mark
  if (dirty) render();
}

void requestRedraw() { dirty = true; }

void noteWebRequest(const char *ip) {
  strncpy(web_ip, ip ? ip : "", sizeof(web_ip) - 1);
  web_at = millis();
  web_count++;
  if (screen == S_STATUS) dirty = true;
}

void setNetworkInfo(const char *ap, const char *sta, const char *ssid) {
  strncpy(ap_ip, ap ? ap : "", sizeof(ap_ip) - 1);
  strncpy(sta_ip, sta ? sta : "", sizeof(sta_ip) - 1);
  strncpy(sta_ssid, ssid ? ssid : "", sizeof(sta_ssid) - 1);
  dirty = true;
}

}  // namespace ui
