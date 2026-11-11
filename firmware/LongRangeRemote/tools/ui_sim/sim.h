#pragma once
#include <stdint.h>
struct Sim { uint8_t keys = 0; float pack_v = 5.0f; int pack_pct = 78, scr_pct = 64; bool empty_db = false, wifi_connected = false, scanning = false, standby = false, holding = false; int sends = 0, clicks = 0; };
extern Sim sim;
