// wifi_file.h - the home Wi-Fi file on the SD card (/wifi.txt): parse, write and check.
//
// The file is plain text so it can be edited on a PC as well as from the remote's Wi-Fi menu:
//
//   # comment lines start with #
//   ssid=MyNetwork
//   password=my wifi password
//
// Spaces around a value are ignored. To keep spaces at the start or end of a password, put the
// value in double quotes: password="  spaces kept  ". Nothing inside the quotes is special.
// A UTF-8 byte-order mark (Notepad) and Windows line endings are accepted.
//
// Pure C++ (no Arduino or SD code) so it can be tested on a PC: see tools/wifi_file_test.
#pragma once
#include <string>

namespace wififile {

struct Creds {
  std::string ssid;
  std::string pass;
  bool has_ssid = false;      // the file had an ssid= line
};

// Parse the file's text. Unknown keys and lines without '=' are ignored. Keys are not
// case-sensitive; "pass" and "psk" are accepted for "password".
Creds parse(const std::string &text);

// The file's text for these credentials (with the explanatory comment header). parse() of the
// result gives back exactly the same ssid and password.
std::string format(const std::string &ssid, const std::string &pass);

// nullptr if the pair can be used, otherwise why not (one short line for the screen):
//   ssid: 0..32 bytes (empty = no home network), no control characters
//   password: empty (open network), 8..63 printable ASCII characters, or 64 hex digits
const char *check(const std::string &ssid, const std::string &pass);

}  // namespace wififile
