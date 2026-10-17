// sd_store.h - the TF card holds all IR codes:
//   /irdb.bin      the code database (made by tools/build_db.py), loaded into PSRAM at boot
//   /irdb.old      the previous database, kept by a web upload; used if irdb.bin fails to load
//   /ir/**/*.ir    your own remotes (Flipper Zero format)
//   /wifi.txt      your home Wi-Fi network (see wifi_file.h; read and written by settings.cpp)
// Nothing is compiled into the firmware, so the codes can be updated by swapping the file (or
// uploading a new one from the web page) without reflashing.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace sdstore {
// Mount the card, load the database into PSRAM, then read your own .ir files. Safe without a card
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
// The new codes are used after a restart.
bool uploadStart(bool database, const char *relpath, char *err, size_t err_len);
bool uploadWrite(const uint8_t *data, size_t len);
bool uploadFinish(char *msg, size_t msg_len);     // false = rejected, msg says why
void uploadAbort();

// Small settings files (e.g. /wifi.txt).
//   readFile:    the whole file; false if there is no card, the file is missing or longer than max_len.
//   replaceFile: writes <path>.new, reads it back to check it, then swaps it in for <path>. A power
//                cut or a pulled card at any moment leaves the old file or the new one, never half of
//                one: readFile finishes a swap that was cut short.
bool readFile(const char *path, std::string &out, size_t max_len);
bool fileExists(const char *path);         // the file (or a finished <path>.new) is on the card
bool replaceFile(const char *path, const std::string &content, char *err, size_t err_len);
}
