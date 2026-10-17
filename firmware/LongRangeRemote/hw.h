// hw.h - control-board keys (straight to ESP32 GPIOs) and battery voltages.
#pragma once
#include <stdint.h>

namespace hw {
void begin();                  // key pins (internal pull-ups), ADC
// Currently pressed keys as a bitmask (bit KEYBIT_* set = pressed), debounced (KEY_DEBOUNCE_MS).
// Cheap: six GPIO reads.
uint8_t keys();
// A key reading pressed since power-up for over 2 s is reported as stuck (a short on the ribbon or
// the board); the UI's status screen shows it. 0 = none.
uint8_t stuckKeys();
// Light sleep: wake when any key pin goes low. prepareSleep() arms GPIO wake-up on all six pins;
// afterSleep() disarms it and takes the key that woke us without waiting for the debounce.
void prepareSleep();
void afterSleep();
// Make a key pin a key input again after something else borrowed it (the speaker's codec set-up
// uses BACK's pin, GPIO14, as the display board's I2C clock for a moment at start-up).
void reclaimKeyPin(int pin);
// LED pack voltage in volts, refreshed every 2 s (not while IR is being sent, when it sags):
//   the AA pack on GPIO1 (0 when no cells are in).
float packVolts();
int packPercent();             // 0..100, -1 if unknown
// True if AA cells are in (a fresh reading, not the 2 s cached one - the IR task checks it before
// every send and before every frame).
bool packOn();
// The display board's own 18650, volts (0 if none fitted / not readable), and percent (-1 if none).
float screenVolts();
int screenPercent();
}
