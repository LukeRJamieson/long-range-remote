// Host test of wifi_file.cpp (the /wifi.txt parser / writer). Build & run from the LongRangeRemote folder:
//   g++ -std=gnu++17 -Wall -I. -o wifitest tools/wifi_file_test/test.cpp wifi_file.cpp && ./wifitest
#include "wifi_file.h"
#include <stdio.h>
#include <string>

static int fails = 0;
static void check(bool c, const std::string &what) {
  printf("  [%s] %s\n", c ? "ok" : "FAIL", what.c_str());
  if (!c) fails++;
}

static void roundTrip(const std::string &ssid, const std::string &pass) {
  wififile::Creds c = wififile::parse(wififile::format(ssid, pass));
  check(c.has_ssid && c.ssid == ssid && c.pass == pass, "round trip: [" + ssid + "] / [" + pass + "]");
}

int main() {
  puts("Reading files written by hand");
  {
    auto c = wififile::parse("ssid=Home\npassword=secret123\n");
    check(c.has_ssid && c.ssid == "Home" && c.pass == "secret123", "plain");
  }
  {
    auto c = wififile::parse("\xEF\xBB\xBF# my wifi\r\n  SSID = Home Net \r\n\r\nPassword = my pass word \r\n");
    check(c.ssid == "Home Net" && c.pass == "my pass word", "Notepad: BOM, CRLF, spaces around = and value, key case");
  }
  {
    auto c = wififile::parse("ssid=\"  Spaced  \"\npass=\" x y \"\n");
    check(c.ssid == "  Spaced  " && c.pass == " x y ", "quoted values keep their spaces; 'pass' key");
  }
  {
    auto c = wififile::parse("ssid=a=b\npsk=p=q#r\n");
    check(c.ssid == "a=b" && c.pass == "p=q#r", "only the first = splits; # inside a value is kept; 'psk' key");
  }
  {
    auto c = wififile::parse("# nothing here\npassword=abcdefgh\n");
    check(!c.has_ssid && c.ssid.empty(), "no ssid line is reported");
  }
  {
    auto c = wififile::parse("ssid=Home\npassword=");
    check(c.ssid == "Home" && c.pass.empty(), "empty password, no final newline");
  }
  {
    auto c = wififile::parse("ssid=Old\nssid=New\n");
    check(c.ssid == "New", "the last ssid line wins");
  }

  puts("Writing and reading back");
  roundTrip("Home", "secret123");
  roundTrip("", "");
  roundTrip("Cafe WiFi", "");
  roundTrip(" lead", "trail ");
  roundTrip("\"quoted\"", "\"abc");
  roundTrip("x\"", "\"");
  roundTrip("   ", "with \"inner\" quotes");
  roundTrip("#hash", "#starts with hash");
  roundTrip("Netz-\xC3\xA4\xC3\xB6\xC3\xBC", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");

  puts("Checks");
  check(!wififile::check("Home", ""), "open network allowed");
  check(!wififile::check("", ""), "no network allowed");
  check(!wififile::check("Home", "12345678"), "8 characters allowed");
  check(wififile::check("Home", "1234567") != nullptr, "7 characters refused");
  check(!wififile::check("Home", std::string(63, 'a')), "63 characters allowed");
  check(!wififile::check("Home", std::string(64, 'a')), "64 hex digits allowed");
  check(wififile::check("Home", std::string(64, 'z')) != nullptr, "64 non-hex characters refused");
  check(wififile::check("Home", std::string(65, 'a')) != nullptr, "65 characters refused");
  check(wififile::check(std::string(33, 's'), "") != nullptr, "33-byte name refused");
  check(wififile::check("a\nb", "") != nullptr, "newline in name refused");
  check(wififile::check("Home", "abc\x01" "defgh") != nullptr, "control character in password refused");

  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails != 0;
}
