// wifi_file.cpp - see wifi_file.h
#include "wifi_file.h"
#include <ctype.h>

namespace wififile {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) a++;
  while (b > a && isSpace(s[b - 1])) b--;
  return s.substr(a, b - a);
}

std::string lower(std::string s) {
  for (char &c : s) c = (char)tolower((unsigned char)c);
  return s;
}

// A value as written in the file: surrounding spaces dropped, then one pair of outer quotes.
std::string unquote(const std::string &raw) {
  std::string v = trim(raw);
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
  return v;
}

// The reverse: quote when reading it back unquoted would change it.
std::string quoteIfNeeded(const std::string &v) {
  bool needs = !v.empty() && (v != trim(v) || v.front() == '"');
  return needs ? "\"" + v + "\"" : v;
}

}  // namespace

Creds parse(const std::string &text) {
  Creds c;
  size_t pos = 0;
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;       // UTF-8 byte-order mark
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(pos, nl - pos);
    pos = nl + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = lower(trim(line.substr(0, eq)));
    std::string val = unquote(line.substr(eq + 1));
    if (key == "ssid") { c.ssid = val; c.has_ssid = true; }
    else if (key == "password" || key == "pass" || key == "psk") c.pass = val;
  }
  return c;
}

std::string format(const std::string &ssid, const std::string &pass) {
  std::string s;
  s += "# Long-range remote: home Wi-Fi network (2.4 GHz only).\n";
  s += "# Read when the remote starts, and rewritten when you change it in Menu > Wi-Fi network.\n";
  s += "# The name is case-sensitive. Leave ssid empty to use only the remote's own LR-Remote network.\n";
  s += "# Leave password empty for an open network. To keep spaces at the start or end of a value,\n";
  s += "# put it in double quotes.\n";
  s += "ssid=" + quoteIfNeeded(ssid) + "\n";
  s += "password=" + quoteIfNeeded(pass) + "\n";
  return s;
}

const char *check(const std::string &ssid, const std::string &pass) {
  if (ssid.size() > 32) return "network name is longer than 32 characters";
  for (char ch : ssid)
    if ((unsigned char)ch < 0x20 || ch == 0x7F) return "network name has a control character in it";
  if (pass.empty()) return nullptr;                              // open network
  if (pass.size() == 64) {                                        // raw 256-bit key in hex
    for (char ch : pass) if (!isxdigit((unsigned char)ch)) return "a 64-character password must be hex digits only";
    return nullptr;
  }
  if (pass.size() < 8) return "password is shorter than 8 characters";
  if (pass.size() > 63) return "password is longer than 63 characters";
  for (char ch : pass)
    if ((unsigned char)ch < 0x20 || (unsigned char)ch > 0x7E) return "password has a character Wi-Fi doesn't allow";
  return nullptr;
}

}  // namespace wififile
