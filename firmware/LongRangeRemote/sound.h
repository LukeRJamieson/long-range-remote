// sound.h - key sounds on the display board's speaker.
//
// Hardware (Waveshare ESP32-S3-RLCD-4.2): ESP32 I2S -> ES8311 codec -> NS4150B class-D amplifier ->
// speaker. Pins in config.h (Speaker).
//
// The codec needs setting up over I2C once. That bus's clock is GPIO14, which is also the BACK key,
// so begin() borrows the pin for ~30 ms and gives it straight back to the key. If BACK is held at that
// moment, the set-up waits (loop() retries) - nothing else on the bus is ever touched.
// Volume is applied to the samples (3 dB a step), so changing it needs no I2C.
//
// The sounds are synthesised (no files). All calls are non-blocking and safe from any task: a sound
// task plays them, and a newer sound cuts off the one playing. The amplifier is switched off when the
// remote has been quiet for SOUND_AMP_HOLD_MS, and before light sleep.
#pragma once
#include <stdint.h>

namespace sound {

enum Cue : uint8_t {
  CLICK,      // a key press
  LONG,       // a key held long enough for its "hold" action (leave a screen, use this code, ...)
  ERROR,      // a code couldn't be sent (no AA cells in, or flat)
  HELLO,      // start-up chime: the speaker works
};

void begin();                // I2S + codec set-up (after hw::begin and settings::begin)
void loop();                 // from loop(): retries the codec set-up if BACK was held at start-up
void play(Cue c);            // plays nothing while muted, at volume 0 or without a working codec

void setVolume(int level);   // 0..SOUND_VOLUME_MAX (settings.h saves it)
int  volume();
void setMuted(bool m);
bool muted();

bool ready();                // codec found and set up
const char *status();        // one line for the screens, e.g. "no codec found"

// Light sleep: stop the I2S clocks and switch the amplifier off; resume after.
void prepareSleep();
void afterSleep();

}  // namespace sound
