// flipper_ir.h - read Flipper Zero ".ir" remote files (the format of Flipper-IRDB, and what a Flipper Zero
// saves when it learns a remote) into the user part of the IR database.
// No Arduino dependencies (tools/test_all_codes.cpp tests it on a PC).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ir_protocols.h"

namespace flipperir {

// Category (IR_CAT_NAMES index) for a Flipper-IRDB folder name ("TVs", "ACs", ...), -1 if unknown.
int categoryForFolder(const char *folder);

// Convert a parsed Flipper signal (protocol name, address and command as little-endian hex byte
// strings such as "07 00 00 00") to the firmware's parameters. false if the protocol is unsupported.
bool convertParsed(const char *protocol, const char *address, const char *command, IrCodeParams &out);

struct FileStats { int parsed = 0, raw = 0, skipped = 0; };

// Parse one .ir file held in memory (text, need not be 0-terminated) and add it to the user database
// as remote `name` of brand `brand`. Returns false if nothing usable was in it.
bool addFile(const char *text, size_t len, const char *brand, const char *name, uint8_t cat, FileStats &st);

}  // namespace flipperir
