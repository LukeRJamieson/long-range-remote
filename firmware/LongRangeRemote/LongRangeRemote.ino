// LongRangeRemote - universal IR remote firmware for the long-range remote
//
//  * All IR codes live on the SD card (/irdb.bin, loaded into PSRAM at boot), so they can be
//    updated without reflashing - copy a new file to the card or upload it from the web page.
//    The database from tools/build_db.py covers TVs, AV receivers, soundbars, projectors,
//    set-top / cable / satellite boxes, streaming boxes, Blu-ray/DVD, hi-fi, game consoles, fans,
//    air-cons, lights...: ~1,600 brands / ~6,800 remotes / ~233,000 codes from Flipper-IRDB (CC0)
//    + IRDB (probonopd/irdb). Codes in protocols without a native encoder (DirecTV, Dish,
//    Motorola/G.I. Cable, XMP, MCE, ...) are pre-rendered to raw timings with IrpTransmogrifier.
//  * Your own remotes: Flipper Zero .ir files under /ir on the card.
//  * 28 IR protocol encoders + raw signals, driven by the ESP32-S3 RMT peripheral with a hardware
//    carrier, straight from GPIO17 into the AOD424 LED switch and the 66-LED head.
//  * On-device UI on the 4.2" reflective LCD with the six keys under the screen: device keys,
//    Vol / Nav pads, channel-number entry, browse, code search.
//  * Phone web remote over Wi-Fi (own access point "LR-Remote", optional home Wi-Fi), with SD-card
//    uploads for new codes. The home network is kept in /wifi.txt on the card and can be changed
//    on the remote (Menu > Wi-Fi network).
//  * Idle sleep: Wi-Fi off + light sleep after IDLE_SLEEP_S; any key wakes it and is sent at once.
//  * Key sounds on the display board's speaker, with a volume / mute setting (Menu > Sound).
//
// Arduino IDE settings:
//   Board "ESP32S3 Dev Module", USB CDC On Boot: Enabled, Flash Size: 16MB,
//   Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)", PSRAM: "OPI PSRAM" (holds the codes).
// Libraries: U8g2 (Library Manager). Core: esp32 by Espressif >= 3.0.
#include <Arduino.h>
#include "config.h"
#include "hw.h"
#include "ir_db.h"
#include "ir_tx.h"
#include "power.h"
#include "sd_store.h"
#include "settings.h"
#include "sound.h"
#include "ui.h"
#include "web.h"
#include "log.h"

void setup() {
  // The LED supply switch first: GPIO2 low = LED head unpowered, whatever happens next.
  pinMode(LED_ARM_PIN, OUTPUT);
  digitalWrite(LED_ARM_PIN, LOW);
  logBegin();                                   // non-blocking: never waits for a Serial Monitor
  logln("\nLong-Range Remote starting");

  hw::begin();                                  // key GPIOs + battery ADC
  sdstore::begin();                             // all IR codes: /irdb.bin + your .ir files
  logf("IR database (%s): %d brands, %d remotes, %d codes\n", irdb::sourceName(), irdb::numBrands(),
       irdb::numProfiles(), irdb::numCodes());

  settings::begin();                            // after the card: favourites by name, wifi.txt
  sound::begin();                               // speaker: I2S + codec (volume from settings)
  if (!irtx::begin(IR_TX_PIN, IR_TX_ACTIVE_LOW, IR_TX_OPEN_DRAIN, IR_TX_DRIVE_STRENGTH))
    logln("IR transmitter init FAILED");
  else
    logf("IR output: GPIO%d -> AOD424, LED supply switched by GPIO%d\n", IR_TX_PIN, LED_ARM_PIN);
  ui::begin();
  web::begin();
  power::begin();
  logln("Ready. Phone: join Wi-Fi \"" WIFI_AP_SSID "\" (password \"" WIFI_AP_PASSWORD "\") and open http://192.168.4.1");
}

void loop() {
  web::loop();
  ui::loop();
  power::loop();
  sound::loop();
  delay(2);
}
