// ir_db.h - Read-only universal IR code database.
//
// Hierarchy:  Brand -> Profile (a model's remote or a code set) -> Code (one button)
//
// Where the tables come from (tools/build_db.py writes them):
//   * /irdb.bin on the SD card: the database image, loaded into PSRAM at boot (sd_store.cpp). Nothing
//     is compiled into the firmware, so the codes can be updated by copying a new file to the card
//     (or uploading it from the web page). The file must have this firmware's IRDB_SCHEMA (ir_keys.h);
//   * plus "your own" remotes: Flipper-format .ir files under /ir on the SD card, appended after the
//     main database as extra brands.
// Index spaces are global: brands / profiles / codes / raw signals of your own files follow those of
// the main database, so the rest of the firmware doesn't need to know where a code came from.
// Without a card every count is 0 and the UI says so.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ir_protocols.h"

struct IrBrand   { uint32_t name; uint16_t first_prof; uint16_t n_prof; };
struct IrGroup   { uint32_t name; uint16_t first; uint16_t count; };          // brand group (TV brands, ...)
struct IrProfile { uint16_t brand; uint32_t name; uint8_t cat; uint8_t src; uint32_t first_code; uint16_t n_codes; };
struct IrCode    { uint32_t name; uint16_t d, s, f; uint8_t proto; uint8_t key; };     // 12 bytes
// Raw capture, compressed: `count` samples, each an index into a table of `table_len` durations
// (4-bit indices packed 2 per byte when table_len <= 16, else 1 byte each).
struct IrRawSig  { uint32_t data_off; uint32_t table_off; uint32_t freq; uint16_t count; uint8_t table_len; uint8_t duty; };

// Profile source (IrProfile::src)
enum IrSource : uint8_t { SRC_FLIPPER = 0, SRC_IRDB = 1, SRC_USER = 2 };

namespace irdb {

// ---- setup -------------------------------------------------------------------------------------
// Validate a database image (see tools/build_db.py, bin_image()) and switch to it. The memory must
// stay valid for as long as it is in use. On failure the previous database stays active and `err`
// explains why.
bool useImage(const uint8_t *img, size_t len, char *err, size_t err_len);
// Check only the header of an image (first 64 bytes): magic, version, schema. For a quick look at a
// file before reading megabytes of it.
bool checkHeader(const uint8_t *hdr, size_t len, char *err, size_t err_len);
const char *sourceName();               // "none", "SD card (irdb.bin)" or "SD card (irdb.old)"
void setSourceName(const char *s);

// Your own remotes, appended after the main database. Call userBegin(), then for each remote
// userProfile() followed by its userCode()/userRawCode() calls, then userFinish().
void userBegin();
// brand: shown as the brand (merged by name with other user files); returns false when full.
bool userProfile(const char *brand, const char *name, uint8_t cat);
bool userCode(const char *name, const IrCodeParams &p);
bool userRawCode(const char *name, const uint16_t *durations, int count, uint32_t freq, uint8_t duty_pct);
void userFinish();
int numUserProfiles();

// ---- queries ---------------------------------------------------------------------------------
int numBrands();
int numProfiles();
int numCodes();

const char *brandName(int b);
int brandFirstProfile(int b);
int brandProfileCount(int b);

const char *profileName(int p);
int profileBrand(int p);
int profileCategory(int p);          // index into IR_CAT_NAMES
int profileSource(int p);            // IrSource
bool profileFromIrdb(int p);         // true = IRDB code set
int profileFirstCode(int p);
int profileCodeCount(int p);
int profileFindKey(int p, uint8_t key);   // code index of a canonical key in a profile, or -1

const IrCode &code(int c);
const char *codeName(int c);
int codeProfile(int c);              // binary search
IrCodeParams codeParams(int c);

// Raw signals
int numRaw();
const IrRawSig &raw(int r);
uint16_t rawSample(const IrRawSig &r, int i);   // i-th duration (us) of a raw signal

// Code search: one candidate per DISTINCT signal. Remotes that share the exact same code for `key`
// are merged; `profile` is the one with the most keys (the best remote to keep once it works) and
// `shared` says how many remotes use that code.
struct ScanCand { int code; int profile; uint16_t shared; };
// Which remotes a code search covers. brand >= 0: one brand; else group >= 0: a brand group;
// else all brands. category >= 0 additionally restricts to one device type (IR_CAT_NAMES index).
struct ScanFilter { int brand = -1; int group = -1; int category = -1; };
// For KEY_POWER, remotes with only a discrete "On" are included.
int buildScan(const ScanFilter &filter, uint8_t key, ScanCand *out, int max_out);
int buildScan(int brand, uint8_t key, ScanCand *out, int max_out);       // = one brand / all (-1)
// Number of remotes a filter covers (any key) - for showing counts in menus.
int countProfiles(const ScanFilter &filter);

// Brand groups
int numGroups();
const char *groupName(int g);
int groupBrandCount(int g);
int groupBrand(int g, int i);
// true if two codes produce the identical IR signal (raw captures compared sample by sample)
bool sameSignal(int code_a, int code_b);

// Find a profile by "Brand" + "Profile" name (used to restore favourites). -1 if not found.
int findProfile(const char *brand, const char *profile);
// Case-insensitive substring search over brand names; returns number of matches written to out[].
int searchBrands(const char *needle, int *out, int max_out);

// Canonical key for a button name ("Vol +", "KEY_VOLUMEUP", "Ch_next" ...), 0 if none.
// Same rules as tools/build_db.py (canon_key), from the table it generates into ir_keys.h.
uint8_t canonKey(const char *button_name);
}  // namespace irdb
