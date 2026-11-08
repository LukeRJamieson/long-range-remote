#pragma once
#include <WiFi.h>
inline int esp_wifi_set_country_code(const char *, bool) { return 0; }
enum { ESP_OK = 0 };
enum wifi_interface_t { WIFI_IF_STA = 0 };
enum wifi_sae_pwe_method_t { WPA3_SAE_PWE_UNSPECIFIED, WPA3_SAE_PWE_HUNT_AND_PECK, WPA3_SAE_PWE_HASH_TO_ELEMENT, WPA3_SAE_PWE_BOTH };
struct wifi_config_t { struct { int sae_pwe_h2e; } sta; };
inline int esp_wifi_get_config(wifi_interface_t, wifi_config_t *c) { c->sta.sae_pwe_h2e = WiFi.sae_pwe; return 0; }
inline int esp_wifi_set_config(wifi_interface_t, wifi_config_t *c) { WiFi.sae_pwe = c->sta.sae_pwe_h2e; return 0; }
inline int esp_wifi_connect() { return WiFi.connect_now(); }
