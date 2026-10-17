// ir_tx.h - IR transmitter: RMT peripheral driver + background send task.
//
// All sends are queued to one FreeRTOS task, so the web server and the
// on-device UI can both trigger codes safely. A key can be "tapped"
// (first frame + the protocol's minimum repeats) or "held" (repeat frames
// until released / keep-alive timeout), which is what volume keys need.
#pragma once
#include <stdint.h>
#include "ir_protocols.h"

namespace irtx {

struct LastSent {
  char     label[64];     // "Denon · Vol +"
  uint8_t  proto;
  uint16_t d, s, f;
  uint32_t carrier_hz;
  uint32_t frames;        // frames actually transmitted
  uint32_t at_ms;         // millis() of the key press
  uint32_t seq;           // increments on every send (UI refresh trigger)
};

// active_low: LED on when the pin is LOW (output inverted, idle HIGH).
// open_drain: the pin only pulls low or floats, never drives high. Combined with active_low the LED
//   is on while the pin pulls to GND (LED from 3V3 to the pin); without it the pin holds GND while
//   idle and lets go for each pulse (module input with its own pull-up).
// drive_strength: 0..3 (GPIO_DRIVE_CAP_0..3).
bool begin(int gpio, bool active_low, bool open_drain = false, int drive_strength = 3);

// The output stage actually running since begin(), e.g. "GPIO17 open-drain" ("not started" before).
const char *outputDesc();

// Database codes (index into irdb::code()).
void tap(int code_index);
void holdStart(int code_index);

// Arbitrary codes (manual entry / tests). `label` is shown on screen.
void tapParams(const IrCodeParams &p, const char *label);
void holdStartParams(const IrCodeParams &p, const char *label);

void holdKeepAlive();
void holdStop();

// LED power level 0..LED_POWER_LEVELS-1 (0 = full). Scales the carrier duty of the next send.
void setPower(int level);
int  power();

LastSent last();
bool isSending();

}  // namespace irtx
