// sd_store.cpp - see sd_store.h
#include "sd_store.h"
#include "config.h"
#include "flipper_ir.h"
#include "ir_db.h"
#include "ir_keys.h"
#include "log.h"
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <algorithm>
#include <string>
#include <vector>

namespace sdstore {
namespace {
bool g_mounted = false;
bool g_db = false;
int g_files = 0;
char g_status[280] = "no card";
char g_db_note[200] = "";               // why the database is missing / which file was used
File g_dbf;                             // the database file, kept open: codes are read from it on demand
char g_db_path[16] = "";
size_t g_db_len = 0;

// irdb's reader: called from the main loop and the transmit task (never at once: irdb locks)
bool dbRead(void *, uint32_t off, void *buf, uint32_t len) {
  if (!g_dbf) return false;
  if (!g_dbf.seek(off)) return false;
  return g_dbf.read((uint8_t *)buf, len) == len;
}

// Open one database file and load its index into PSRAM. false (with err) if missing or invalid.
bool loadDatabaseFile(const char *path, char *err, size_t en) {
  uint32_t t0 = millis();
  g_dbf = SD_MMC.open(path, FILE_READ);
  if (!g_dbf) { snprintf(err, en, "not found"); return false; }
  size_t len = g_dbf.size();
  if (!irdb::useFile(irdb::Reader{dbRead, nullptr, (uint32_t)len}, err, en)) { g_dbf.close(); return false; }
  snprintf(g_db_path, sizeof(g_db_path), "%s", path);
  g_db_len = len;
  logf("SD: opened %s (%u KB, index loaded in %lu ms): %d brands, %d remotes, %d codes\n", path, (unsigned)(len / 1024),
       (unsigned long)(millis() - t0), irdb::numBrands(), irdb::numProfiles(), irdb::numCodes());
  return true;
}

void loadDatabase() {
  char err[96], err_old[96];
  if (loadDatabaseFile(SD_DB_FILE, err, sizeof(err))) {
    irdb::setSourceName("SD card (irdb.bin)");
    g_db = true;
    return;
  }
  logf("SD: %s: %s\n", SD_DB_FILE, err);
  // a web upload keeps the previous file: better old codes than none
  if (loadDatabaseFile(SD_DB_OLD, err_old, sizeof(err_old))) {
    irdb::setSourceName("SD card (irdb.old)");
    snprintf(g_db_note, sizeof(g_db_note), "irdb.bin %s - using irdb.old", err);
    g_db = true;
    return;
  }
  snprintf(g_db_note, sizeof(g_db_note), "no code database: irdb.bin %s", err);
}

void collect(const String &dir, int depth, std::vector<std::string> &out) {
  if (depth > 5 || (int)out.size() >= SD_MAX_USER_FILES) return;
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return;
  for (File e = d.openNextFile(); e && (int)out.size() < SD_MAX_USER_FILES; e = d.openNextFile()) {
    String name = e.name();                      // Arduino-ESP32 3.x: base name only
    String path = dir + "/" + name;
    if (name.startsWith(".")) continue;
    if (e.isDirectory()) collect(path, depth + 1, out);
    else if (name.endsWith(".ir") || name.endsWith(".IR")) out.push_back(path.c_str());
  }
}

std::string pretty(std::string s) {
  for (char &c : s) if (c == '_') c = ' ';
  return s;
}

void loadUserFiles() {
  std::vector<std::string> files;
  collect(SD_USER_DIR, 0, files);
  if (files.empty()) return;
  std::sort(files.begin(), files.end());
  irdb::userBegin();
  std::vector<char> buf;
  int ok = 0;
  flipperir::FileStats st;
  for (const std::string &path : files) {
    File f = SD_MMC.open(path.c_str(), FILE_READ);
    if (!f) continue;
    size_t len = f.size();
    if (len == 0 || len > SD_MAX_FILE_BYTES) { f.close(); continue; }
    buf.resize(len);
    size_t got = f.read((uint8_t *)buf.data(), len);
    f.close();
    // path below /ir: folders..., file
    std::vector<std::string> parts;
    size_t start = strlen(SD_USER_DIR) + 1;
    while (start < path.size()) {
      size_t e = path.find('/', start);
      if (e == std::string::npos) e = path.size();
      parts.push_back(path.substr(start, e - start));
      start = e + 1;
    }
    std::string file = parts.back();
    file = file.substr(0, file.size() - 3);
    int cat = -1;
    for (size_t i = 0; i + 1 < parts.size() && cat < 0; i++) cat = flipperir::categoryForFolder(parts[i].c_str());
    std::string brand = "My remotes";
    if (parts.size() >= 2 && flipperir::categoryForFolder(parts[parts.size() - 2].c_str()) < 0)
      brand = pretty(parts[parts.size() - 2]);
    if (flipperir::addFile(buf.data(), got, brand.c_str(), pretty(file).c_str(),
                           (uint8_t)(cat >= 0 ? cat : IR_NUM_CATS - 1), st))
      ok++;
  }
  irdb::userFinish();
  g_files = ok;
  logf("SD: %d of %d .ir files loaded from %s (%d parsed, %d raw signals, %d skipped)\n", ok, (int)files.size(),
       SD_USER_DIR, st.parsed, st.raw, st.skipped);
}

// ---- uploads ----
File g_up;
bool g_up_db = false, g_up_failed = false;
bool g_up_oui = false;                 // the Bluetooth scanner's /oui.bin (written as /oui.new first)
size_t g_up_len = 0;
char g_up_path[160];

bool safeRelPath(const char *p) {
  if (!*p || *p == '/' || strstr(p, "..") || strlen(p) > 120) return false;
  for (const char *c = p; *c; c++)
    if (*c < 0x20 || *c == '\\' || *c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' || *c == '>' || *c == '|')
      return false;
  size_t n = strlen(p);
  return n > 3 && strcasecmp(p + n - 3, ".ir") == 0;
}

// SD_MMC keeps the card handle protected; the IDF format call needs it. A pointer to the member,
// taken in a derived class, reads it without changing the library.
struct SdmmcCard : fs::SDMMCFS {
  static sdmmc_card_t *fs::SDMMCFS::*member() { return &SdmmcCard::_card; }
};

void setMountedStatus(const char *what) {
  unsigned mb = (unsigned)(SD_MMC.cardSize() / (1024 * 1024));
  snprintf(g_status, sizeof(g_status), "card %u MB, %s", mb, what);
}

void mkdirs(const char *path) {                  // create every folder of a file path
  std::string s(path);
  for (size_t i = 1; i < s.size(); i++)
    if (s[i] == '/') { std::string d = s.substr(0, i); if (!SD_MMC.exists(d.c_str())) SD_MMC.mkdir(d.c_str()); }
}
}  // namespace

void begin() {
  if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN) || !SD_MMC.begin("/sdcard", true /* 1-bit */)) {
    logln("SD: no card (or not readable) - no IR codes until a card with irdb.bin is inserted");
    snprintf(g_status, sizeof(g_status), "no card - insert one with irdb.bin and restart");
    return;
  }
  g_mounted = true;
  loadDatabase();
  loadUserFiles();
  char extra[48] = "";
  if (g_files) snprintf(extra, sizeof(extra), ", %d own remote file%s", g_files, g_files == 1 ? "" : "s");
  unsigned mb = (unsigned)(SD_MMC.cardSize() / (1024 * 1024));
  if (g_db && !g_db_note[0]) snprintf(g_status, sizeof(g_status), "card %u MB, irdb.bin loaded%s", mb, extra);
  else snprintf(g_status, sizeof(g_status), "card %u MB, %s%s", mb, g_db_note, extra);
}

bool mounted() { return g_mounted; }
bool databaseLoaded() { return g_db; }
const char *status() { return g_status; }
int userFiles() { return g_files; }

bool format(bool keep, char *msg, size_t mn) {
  uploadAbort();                                   // no file may be open while the card is formatted
  // keep: the database lives on the card, so it has to fit in PSRAM for the moment of the format
  uint8_t *image = nullptr;
  size_t image_len = 0;
  if (keep && g_dbf) {
    image = loadToPsram(g_db_path, &image_len, 64u * 1024 * 1024);
    if (!image) {
      snprintf(msg, mn, "Not formatted: the code database (%u MB) is too big to hold in memory while the card is "
               "formatted. Format without keeping it and upload irdb.bin again afterwards.", (unsigned)(g_db_len >> 20));
      return false;
    }
  }
  if (g_dbf) g_dbf.close();                        // (code reads fail until a database file is open again)
  if (!g_mounted) {
    // Not mounted at start-up: no card then, or one that isn't FAT. Mount it now, letting the IDF
    // format it if it has no usable file system (it is formatted again properly below).
    SD_MMC.end();
    if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN) ||
        !SD_MMC.begin("/sdcard", true /* 1-bit */, true /* format if mount fails */)) {
      snprintf(msg, mn, "no card found, or it can't be used (try another card)");
      if (image) heap_caps_free(image);
      return false;
    }
    g_mounted = true;
  }
  sdmmc_card_t *card = SD_MMC.*SdmmcCard::member();
  if (!card) { snprintf(msg, mn, "no card"); if (image) heap_caps_free(image); return false; }
  esp_vfs_fat_mount_config_t cfg = {};
  cfg.max_files = 5;
  cfg.allocation_unit_size = 32 * 1024;            // 32 KB clusters: quick to read the 6 MB database
  uint32_t t0 = millis();
  logln("SD: formatting the card...");
  esp_err_t e = esp_vfs_fat_sdcard_format_cfg("/sdcard", card, &cfg);
  if (e != ESP_OK) {
    snprintf(msg, mn, "format failed (%s) - the card may be faulty or write-protected", esp_err_to_name(e));
    snprintf(g_status, sizeof(g_status), "format failed - restart, or try another card");
    logf("SD: %s\n", msg);
    if (image) heap_caps_free(image);
    return false;
  }
  logf("SD: formatted in %lu ms\n", (unsigned long)(millis() - t0));
  SD_MMC.mkdir(SD_USER_DIR);
  g_files = 0;
  if (!keep) {
    if (image) heap_caps_free(image);
    snprintf(msg, mn, "Card formatted. It is empty: upload irdb.bin before the remote restarts.");
    setMountedStatus("formatted - empty: upload irdb.bin");
    return true;
  }
  if (!image) {
    snprintf(msg, mn, "Card formatted. There was no code database to put back: upload irdb.bin.");
    setMountedStatus("formatted - empty: upload irdb.bin");
    return true;
  }
  File f = SD_MMC.open(SD_DB_FILE, FILE_WRITE);
  size_t done = 0;
  if (f) {
    while (done < image_len) {
      size_t n = f.write(image + done, std::min<size_t>(32768, image_len - done));
      if (n == 0) break;
      done += n;
    }
    f.close();
  }
  heap_caps_free(image);
  if (done != image_len) {
    SD_MMC.remove(SD_DB_FILE);
    snprintf(msg, mn, "Card formatted, but writing irdb.bin back failed: upload it before the remote restarts.");
    setMountedStatus("formatted - irdb.bin missing: upload it");
    return true;
  }
  g_dbf = SD_MMC.open(SD_DB_FILE, FILE_READ);            // same bytes as before: carry on reading codes from it
  snprintf(g_db_path, sizeof(g_db_path), "%s", SD_DB_FILE);
  logf("SD: irdb.bin written back (%u KB) in %lu ms\n", (unsigned)(image_len / 1024), (unsigned long)(millis() - t0));
  snprintf(msg, mn, "Card formatted and irdb.bin put back (%u KB).", (unsigned)(image_len / 1024));
  setMountedStatus("formatted, irdb.bin put back");
  return true;
}

bool uploadStart(bool database, const char *relpath, char *err, size_t en) {
  uploadAbort();
  if (!g_mounted) { snprintf(err, en, "no SD card"); return false; }
  g_up_db = database;
  g_up_oui = !database && !strcmp(relpath, "oui.bin");
  if (database) {
    snprintf(g_up_path, sizeof(g_up_path), "%s", SD_DB_NEW);
  } else if (g_up_oui) {
    snprintf(g_up_path, sizeof(g_up_path), "/oui.new");
  } else {
    if (!safeRelPath(relpath)) { snprintf(err, en, "bad file name (must end in .ir, no .. or special characters)"); return false; }
    snprintf(g_up_path, sizeof(g_up_path), "%s/%s", SD_USER_DIR, relpath);
    mkdirs(g_up_path);
  }
  g_up = SD_MMC.open(g_up_path, FILE_WRITE);
  if (!g_up) { snprintf(err, en, "cannot write %s", g_up_path); return false; }
  g_up_len = 0; g_up_failed = false;
  return true;
}

bool uploadWrite(const uint8_t *data, size_t len) {
  if (!g_up || g_up_failed) return false;
  if (g_up.write(data, len) != len) { g_up_failed = true; return false; }
  g_up_len += len;
  if (!g_up_db && g_up_len > (g_up_oui ? 4u * 1024 * 1024 : SD_MAX_FILE_BYTES)) { g_up_failed = true; return false; }
  return true;
}

bool uploadFinish(char *msg, size_t mn) {
  if (!g_up) { snprintf(msg, mn, "no upload in progress"); return false; }
  g_up.close();
  if (g_up_failed) {
    SD_MMC.remove(g_up_path);
    snprintf(msg, mn, g_up_db ? "write error (card full?)" : "write error or file too big");
    return false;
  }
  if (g_up_oui) {                       // check the header before it replaces the current file
    File f = SD_MMC.open("/oui.new", FILE_READ);
    char hdr[4] = {0};
    bool ok = f && f.read((uint8_t *)hdr, 4) == 4 && !memcmp(hdr, "OUI1", 4);
    if (f) f.close();
    if (!ok) { SD_MMC.remove("/oui.new"); snprintf(msg, mn, "not accepted: not an oui.bin file"); return false; }
    SD_MMC.remove("/oui.bin");
    if (!SD_MMC.rename("/oui.new", "/oui.bin")) { snprintf(msg, mn, "rename failed"); return false; }
    snprintf(msg, mn, "oui.bin saved (%u KB)", (unsigned)(g_up_len / 1024));
    return true;
  }
  if (!g_up_db) {
    snprintf(msg, mn, "saved %s (%u bytes)", g_up_path, (unsigned)g_up_len);
    return true;
  }
  // Database: check before it replaces the current one. The full check (every table index) runs at
  // the next boot; if that fails, irdb.old is used.
  File f = SD_MMC.open(SD_DB_NEW, FILE_READ);
  uint8_t hdr[64];
  char err[96] = "cannot read it back";
  bool ok = false;
  if (f) {
    if (f.read(hdr, sizeof(hdr)) == sizeof(hdr)) ok = irdb::checkHeader(hdr, sizeof(hdr), err, sizeof(err));
    else snprintf(err, sizeof(err), "file too short");
    f.close();
  }
  if (!ok) {
    SD_MMC.remove(SD_DB_NEW);
    snprintf(msg, mn, "not accepted: %s", err);
    return false;
  }
  // The open database file is about to be renamed (or removed): close it, and carry on reading the
  // running database from wherever it ends up, until the restart that follows an upload.
  const bool was_main = g_dbf && !strcmp(g_db_path, SD_DB_FILE);
  if (g_dbf) g_dbf.close();
  if (SD_MMC.exists(SD_DB_FILE)) {
    SD_MMC.remove(SD_DB_OLD);
    SD_MMC.rename(SD_DB_FILE, SD_DB_OLD);
  }
  bool renamed = SD_MMC.rename(SD_DB_NEW, SD_DB_FILE);
  if (was_main) { g_dbf = SD_MMC.open(SD_DB_OLD, FILE_READ); snprintf(g_db_path, sizeof(g_db_path), "%s", SD_DB_OLD); }
  if (!renamed) { snprintf(msg, mn, "rename failed"); return false; }
  snprintf(msg, mn, "irdb.bin updated (%u KB); the previous one is kept as irdb.old", (unsigned)(g_up_len / 1024));
  return true;
}

void uploadAbort() {
  if (g_up) { g_up.close(); SD_MMC.remove(g_up_path); }
}

uint8_t *loadToPsram(const char *path, size_t *len, size_t max_len) {
  *len = 0;
  if (!g_mounted) return nullptr;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  size_t n = f.size();
  uint8_t *buf = n && n <= max_len ? (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  size_t got = 0;
  while (buf && got < n) {
    int r = f.read(buf + got, std::min<size_t>(32768, n - got));
    if (r <= 0) break;
    got += r;
  }
  f.close();
  if (buf && got != n) { heap_caps_free(buf); buf = nullptr; }
  if (buf) *len = n;
  return buf;
}

// ---- small settings files ----
namespace {
bool readWhole(const char *path, std::string &out, size_t max_len) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) return false;
  size_t len = f.size();
  if (len > max_len) { f.close(); return false; }
  out.assign(len, '\0');
  size_t got = len ? f.read((uint8_t *)&out[0], len) : 0;
  f.close();
  return got == len;
}
}  // namespace

bool readFile(const char *path, std::string &out, size_t max_len) {
  if (!g_mounted) return false;
  std::string tmp = std::string(path) + ".new";
  if (SD_MMC.exists(path)) {
    // A .new beside the file is a save that never got as far as the swap: the file is still current.
    if (SD_MMC.exists(tmp.c_str())) SD_MMC.remove(tmp.c_str());
  } else if (SD_MMC.exists(tmp.c_str())) {
    // The old file was removed but the checked new one never renamed: finish the swap.
    if (SD_MMC.rename(tmp.c_str(), path)) logf("SD: finished an interrupted save of %s\n", path);
    else return readWhole(tmp.c_str(), out, max_len);
  } else {
    return false;                               // not there (open() would log an error for it)
  }
  return readWhole(path, out, max_len);
}

bool fileExists(const char *path) {
  return g_mounted && (SD_MMC.exists(path) || SD_MMC.exists((std::string(path) + ".new").c_str()));
}

bool replaceFile(const char *path, const std::string &content, char *err, size_t en) {
  if (!g_mounted) { snprintf(err, en, "no SD card"); return false; }
  std::string tmp = std::string(path) + ".new";
  File f = SD_MMC.open(tmp.c_str(), FILE_WRITE);
  if (!f) { snprintf(err, en, "card not writable (write-protect switch?)"); return false; }
  size_t put = f.write((const uint8_t *)content.data(), content.size());
  f.close();
  std::string back;
  if (put != content.size() || !readWhole(tmp.c_str(), back, content.size() + 1) || back != content) {
    SD_MMC.remove(tmp.c_str());
    snprintf(err, en, "write failed (card full or faulty?)");
    return false;
  }
  if (SD_MMC.exists(path) && !SD_MMC.remove(path)) {
    SD_MMC.remove(tmp.c_str());
    snprintf(err, en, "could not replace %s", path);
    return false;
  }
  if (!SD_MMC.rename(tmp.c_str(), path)) {
    // The new file is complete and checked: readFile() picks it up from <path>.new next time.
    snprintf(err, en, "rename failed");
    return false;
  }
  return true;
}

bool saveUserRemote(const char *relpath, const std::string &content, char *err, size_t en) {
  if (!g_mounted) { snprintf(err, en, "no SD card"); return false; }
  std::string path = std::string(SD_USER_DIR) + "/" + relpath;
  mkdirs(path.c_str());
  return replaceFile(path.c_str(), content, err, en);
}
}  // namespace sdstore
