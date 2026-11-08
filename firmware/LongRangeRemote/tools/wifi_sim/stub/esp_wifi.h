#pragma once
#include <WiFi.h>
#include <string.h>
inline int esp_wifi_set_country_code(const char *, bool) { return 0; }
enum { ESP_OK = 0 }; typedef int esp_err_t;
enum wifi_interface_t { WIFI_IF_STA = 0 };
enum wifi_sae_pwe_method_t { WPA3_SAE_PWE_UNSPECIFIED, WPA3_SAE_PWE_HUNT_AND_PECK, WPA3_SAE_PWE_HASH_TO_ELEMENT, WPA3_SAE_PWE_BOTH };
struct wifi_config_t { struct { int sae_pwe_h2e; } sta; };
inline int esp_wifi_get_config(wifi_interface_t, wifi_config_t *c) { c->sta.sae_pwe_h2e = WiFi.sae_pwe; return 0; }
inline int esp_wifi_set_config(wifi_interface_t, wifi_config_t *c) { WiFi.sae_pwe = c->sta.sae_pwe_h2e; return 0; }
inline int esp_wifi_connect() { return WiFi.connect_now(); }
inline int esp_wifi_get_mac(wifi_interface_t, uint8_t m[6]) { memcpy(m, WiFi.mac, 6); return 0; }
inline int esp_wifi_set_mac(wifi_interface_t, const uint8_t m[6]) { if (WiFi.busy) return 1; memcpy(WiFi.mac, m, 6); WiFi.mac_changes++; return 0; }
