// discover.h - find and control a device that is NOT in the code database.
//
// The database only helps when some remote of the device's brand is in it. For anything else the
// remote can still try the protocols it speaks, in three steps:
//
//   1. SWEEP    For a protocol family (NEC, Samsung, Sony, RC5, RC6, Panasonic, ...) every device
//               address is tried in turn. Each address gets a short burst of "probe" functions: the
//               function numbers that the database's remotes most often use for Power, Vol + and
//               Mute in that protocol (makers reuse them across models). Addresses that occur in
//               the database go first. The user watches the device and says when it reacts.
//   2. NARROW   A reaction (a TV switching on) shows up seconds late, so the sweep steps back a few
//               addresses and the user tries them one at a time until the right one is found.
//   3. LEARN    With the address known, the functions are stepped through one by one (the likely
//               ones first). Whatever the device does, the user gives that function a key name.
//               The named keys are saved as a Flipper ".ir" file in /ir/Discovered on the SD card,
//               which makes it a device like any other after a restart.
//
// One engine, used by the screen (ui.cpp) and the web page (web.cpp): both show the same state.
// The core has no Arduino dependencies (tools/discover_test runs it on a PC); the firmware glue at
// the end of discover.cpp connects it to the transmitter, the SD card and the settings.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "ir_protocols.h"

namespace discover {

// ---- protocol families ----
struct Family {
  const char *name;        // "NEC", "Sony 12-bit", ...
  const char *brands;      // who uses it, for the list
  const char *file_tag;    // short name for the saved remote, "NEC", "Sony12", ...
};
int numFamilies();
const Family &family(int fam);
int numAddrs(int fam);                 // addresses the sweep covers (prepares the family on first use)
int numFuncs(int fam);                 // function numbers per address
IrCodeParams code(int fam, int addr, int func);
void addrText(int fam, int addr, char *out, size_t n);        // "device 4", "device 0x12.0xED"
// Most common meaning of a function number in this family (canonical key), 0 = not in the database
uint8_t guessKey(int fam, int func);
// The probe functions sent to every address during a sweep (most likely Power / Vol + / Mute first)
int probes(int fam, uint8_t *out, int max);

// ---- engine ----
enum Mode : uint8_t {
  IDLE,
  SWEEP,        // running through the addresses by itself
  NARROW,       // paused on one address; the user steps
  LEARN,        // stepping through one address's functions
};
struct Learned { uint8_t func; uint8_t key; char name[20]; };   // key 0: custom name
const int MAX_LEARNED = 64;

struct State {
  Mode mode;
  bool all;              // sweeping every family, not just one
  bool done;             // the sweep reached the end without a reaction (paused on the last address)
  int fam;
  int addr;              // address (a number for code() / addrText())
  int pos, total;        // sweep progress within the family, in addresses
  int fam_pos, fam_total;// when `all`: which family
  bool sending;          // a burst is still going out
  int func;              // LEARN: current function number
  int func_pos;          // LEARN: its place in the likely-first order (0..numFuncs-1)
  int n_learned;
  uint32_t eta_s;        // SWEEP: rough time left
};
State state();
const Learned &learned(int i);

void sweepStart(int fam);              // fam < 0: every family, most common first
void reacted();                        // SWEEP -> NARROW, a few addresses back, and tries that one
void narrowStep(int dir);              // -1 / 0 / +1 address, and tries it
void resumeSweep();                    // NARROW -> SWEEP from where it was stopped
void skipFamily();                     // SWEEP of every family: on to the next one
void learnStart(int fam, int addr);    // straight to LEARN (also from NARROW: "this one")
void learnPick();                      // NARROW -> LEARN on the current address
void learnStep(int dir);               // -1 / 0 / +1 in the likely-first order, and sends it
void learnGoto(int func);              // a function number, and sends it
bool learnName(uint8_t key, const char *custom);   // name the current function (key 0 + custom text)
void learnForget(int func);
void stop();                           // -> IDLE, forgets everything learned

// The .ir file for what has been learned, and the names it is saved under
const char *keyFileName(uint8_t key);
std::string fileText();
void fileNames(char *brand, size_t bn, char *remote, size_t rn);

// ---- hooks (set by the firmware glue; a test sets its own) ----
struct Hooks {
  void (*send)(const IrCodeParams &p, const char *label);
  bool (*busy)();
  uint32_t (*now)();
};
void setHooks(const Hooks &h);
void loop();                           // call often: sends the next probe when the transmitter is free

// ---- firmware glue ----
void begin();                          // hooks -> irtx, millis
// Write the learned keys to the SD card, make the new remote the active device and restart (the
// user remotes are only read at start-up). false + err if nothing was learned or the card failed.
bool saveAndRestart(char *err, size_t err_len);
bool restartPending();                 // saved: the restart is a moment away
void glueLoop();                       // from the Arduino loop: does that restart

}  // namespace discover
