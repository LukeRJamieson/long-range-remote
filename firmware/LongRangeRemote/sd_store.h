// sd_store.h - the TF card holds all IR codes:
//   /irdb.bin      the code database (made by tools/build_db.py): index into PSRAM at boot, codes read on demand
//   /irdb.old      the previous database, kept by a web upload; used if irdb.bin fails to load
//   /ir/**/*.ir    your own remotes (Flipper Zero format)
//   /wifi.txt      your home Wi-Fi network (see wifi_file.h; read and written by settings.cpp)
//   /oui.bin       MAC prefix -> maker, for the Bluetooth scanner (optional; ble_glue.cpp)
// Nothing is compiled into the firmware, so the codes can be updated by swapping the file (or
// uploading a new one from the web page) without reflashing.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace sdstore {
// Mount the card, open the database (index into PSRAM), then read your own .ir files. Safe without a card
// (the database is then empty and the UI says how to fix it). Call before anything uses irdb.
void begin();
bool mounted();
bool databaseLoaded();
const char *status();          // one line for the status screen / web page
int userFiles();               // .ir files loaded

// Web uploads (streamed straight to the card, a chunk at a time).
//   database = true:  written to /irdb.new; finish() checks its header and size, keeps the current
//                     file as /irdb.old and renames the new one to /irdb.bin.
//   database = false: `relpath` below /ir (e.g. "Living room/TV.ir"); folders are created.
//                     relpath "oui.bin": the Bluetooth scanner's MAC prefix file, checked and saved as /oui.bin.
// The new codes are used after a restart.
bool uploadStart(bool database, const char *relpath, char *err, size_t err_len);
bool uploadWrite(const uint8_t *data, size_t len);
bool uploadFinish(char *msg, size_t msg_len);     // false = rejected, msg says why
void uploadAbort();

// Format the card: FAT32 (FAT16 on cards under 2 GB), 32 KB clusters. ERASES EVERYTHING ON IT.
// Also works on a card that didn't mount (never formatted, or corrupted), and on one inserted after
// start-up. keep_codes: the code database in memory is written back as /irdb.bin afterwards, so the
// remote works on the next start without an upload (the caller puts wifi.txt back: settings.h).
// Your own .ir files can't be put back - they are erased. msg says what happened.
bool format(bool keep_codes, char *msg, size_t msg_len);

// Small settings files (e.g. /wifi.txt).
//   readFile:    the whole file; false if there is no card, the file is missing or longer than max_len.
//   replaceFile: writes <path>.new, reads it back to check it, then swaps it in for <path>. A power
//                cut or a pulled card at any moment leaves the old file or the new one, never half of
//                one: readFile finishes a swap that was cut short.
bool readFile(const char *path, std::string &out, size_t max_len);
bool fileExists(const char *path);         // the file (or a finished <path>.new) is on the card
bool replaceFile(const char *path, const std::string &content, char *err, size_t err_len);
// Write one of the user's own remotes: relpath below /ir, e.g. "Discovered/NEC 4.ir" (folders are
// created). It is read at the next start-up like any other .ir file there.
bool saveUserRemote(const char *relpath, const std::string &content, char *err, size_t err_len);
// A whole file into PSRAM (heap_caps_malloc; free() it). nullptr if missing, empty, over max_len or unreadable.
uint8_t *loadToPsram(const char *path, size_t *len, size_t max_len);
}
