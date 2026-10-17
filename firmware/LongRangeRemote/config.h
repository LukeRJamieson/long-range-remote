// config.h - Long-Range Remote universal remote: user configuration.
//
// Hardware: Waveshare ESP32-S3-RLCD-4.2 + the control board (16-way ribbon on the Waveshare 2x8 header) +
// the 66-LED head, which runs straight off 4 x AA NiMH. The display board and ESP32 run from the display
// board's own 18650. See ../../README.md.
#pragma once

#define HAS_DISPLAY          1

// ---------------------------------------------------------------------------
// IR output (control board): GPIO17 -> R6 47R -> Q2 AOD424 (logic-level MOSFET, fully on at 2.5 V), active
// high. The LEDs run straight off the AA pack. Their supply is switched by the ARM pin (GPIO2 -> Q6 2N3904 ->
// Q5 P-FET): the firmware raises it for each transmission and drops it afterwards, so the LEDs can only
// light while ARM *and* the carrier on GPIO17 are both high. The carrier starts once the supply is up
// (LED_ARM_SETTLE_MS), so the first frame goes out at full strength.
// IR_TX_DRIVE_STRENGTH 2 (~20 mA class): with R6 it sets Q2's switching edge (~0.3 us). A faster edge makes the
// ribbon to the LED head ring and reverse-bias the LEDs; see sim/control_lite_sim.py before changing it.
// ---------------------------------------------------------------------------
#define IR_TX_PIN            17
#define IR_TX_ACTIVE_LOW     0
#define IR_TX_OPEN_DRAIN     0
#define IR_TX_DRIVE_STRENGTH 2
#define LED_ARM_PIN          2
#define LED_ARM_SETTLE_MS    30    // Q5 turns on slowly - C8 1uF gate-source must first discharge through
                                   // R15, then C7 soft-starts the drain: VBAT is up in 5-10 ms (simulated)
// LED power levels: the carrier's duty cycle is scaled to this share, so the LED current (and the
// fundamental that the receiver sees) drops with it. Full / Half / Low = -0 / -4.6 / -10.8 dB.
// Whatever the code asks for (a .ir file can say 90 %), the carrier duty is capped at LED_DUTY_MAX_PCT:
// the strings run up to ~110 mA peak on fresh cells, so their average must stay well under 100 mA.
#define LED_DUTY_MAX_PCT     50
#define LED_POWER_LEVELS     3
#define LED_POWER_PCT        {100, 50, 25}
#define LED_POWER_NAMES      {"Full", "Half", "Low"}
// A held key keeps repeating until released. Web clients send keep-alives; if they stop
// (phone locks, Wi-Fi drops) transmission stops after this many ms.
#define IR_HOLD_TIMEOUT_MS   700
#define IR_HOLD_MAX_MS       15000   // absolute safety limit for one hold

// ---------------------------------------------------------------------------
// Keys: the six keys on the control board go straight to ESP32 GPIOs on the Waveshare 2x8 header
// (16-way ribbon, pin n = pin n). Each key shorts its pin to GND; the ESP32's internal pull-ups hold
// them high, so the board needs no expander and no key resistors.
//   UP     GPIO18  header pin 15 (the Waveshare board's own KEY button is on this pin too, so it works as UP)
//   DOWN   GPIO3   header pin 11
//   LEFT   GPIO44  header pin 12 (U0RXD - free: the board's USB-C is the ESP32's own USB)
//   RIGHT  GPIO43  header pin 10 (U0TXD, through 1k on the control board: the boot ROM drives it)
//   SELECT GPIO0   header pin 5   (also the BOOT button, so holding SELECT through a reset starts the
//                                  bootloader. GPIO1 reads the AA pack.)
//   (UP and SELECT are on these two pins because pin 15 is at the D-pad's end of the header and pin 5
//    at SELECT's, so neither key's track has to cross the others on the control board.)
//   BACK   GPIO14  header pin 16 (the display board's I2C SCL, held high by its own pull-up. The firmware
//                                  only uses that bus for ~30 ms at start-up, to set up the speaker's codec
//                                  (see Speaker below); SCL low with SDA idle is never an I2C start, so the
//                                  display board's chips ignore the key.)
// KEYBIT_* are the bit numbers in hw::keys() (unchanged from the PCF8574 version, so the UI is too).
// ---------------------------------------------------------------------------
#define KEY_UP_PIN           18
#define KEY_DOWN_PIN         3
#define KEY_LEFT_PIN         44
#define KEY_RIGHT_PIN        43
#define KEY_SELECT_PIN       0
#define KEY_BACK_PIN         14
#define KEY_DEBOUNCE_MS      15     // a change must be stable this long to count
#define KEYBIT_UP            1
#define KEYBIT_DOWN          2
#define KEYBIT_LEFT          3
#define KEYBIT_RIGHT         4
#define KEYBIT_SELECT        5
#define KEYBIT_BACK          6

// LED battery ("pack")
// 4 x AA NiMH, read through R12/R13 (470k / 470k) on GPIO1, from the pack side of Q5 (always live). Shown as a percentage from a NiMH
// per-cell curve (hw.cpp). Below PACK_LOW_V the screen asks for charged cells: the LEDs are down to
// ~65 mA peak by then (100 mA on fresh cells), so the range is shorter.
#define PACK_ADC_PIN         1
#define PACK_ADC_DIVIDER     2.0f
#define PACK_CELLS           4
#define PACK_LOW_V           4.6f
// Below PACK_CUTOFF_V (at rest) nothing is sent: the cells are flat (1.1 V each - going lower hurts NiMH).
#define PACK_CUTOFF_V        4.4f
#define PACK_FLAT_MSG        "AA cells flat - sending stopped, put in charged ones"
// Q5 (driven by ARM, GPIO2) connects the pack to the LEDs only while sending, but GPIO1 reads the pack
// itself, so it is valid any time. No cells in: GPIO1 reads ~0 V. Below PACK_OFF_V (at rest) the
// pack counts as missing: the header shows "AA out" and nothing is sent. A real NiMH pack at rest
// is >= 4.0 V.
#define PACK_OFF_V           3.5f
#define PACK_NAME            "AA"
#define PACK_LOW_MSG         "AA cells low - put in charged ones"
// The display board's own 18650: ADC on GPIO4 through the board's 1/3 divider (Waveshare's ESPHome
// config multiplies by 3). It runs the ESP32 and display, so the header shows it next to the AA pack.
#define SCREEN_BATT_ADC_PIN  4
#define SCREEN_BATT_DIVIDER  3.0f

// ---------------------------------------------------------------------------
// Waveshare board pins (fixed by its PCB)
// ---------------------------------------------------------------------------
#define LCD_SCK_PIN   11
#define LCD_MOSI_PIN  12
#define LCD_DC_PIN     5
#define LCD_CS_PIN    40
#define LCD_RST_PIN   41
#define BTN_BOOT_PIN   0     // "BOOT" button (under the lid): same pin as SELECT; hold at power-up for the bootloader
#define BTN_KEY_PIN   18     // "KEY" button (under the lid): same pin as UP, so it works as UP
// TF card slot, SDMMC 1-bit (Waveshare's own SD example uses these)
#define SD_CLK_PIN    38
#define SD_CMD_PIN    21
#define SD_D0_PIN     39

// ---------------------------------------------------------------------------
// Speaker (sound.h): the display board's ES8311 codec -> NS4150B class-D amplifier -> its speaker.
// The ESP32 sends 16 kHz / 16-bit I2S; the codec is set up once over the display board's I2C bus,
// whose clock (GPIO14) is also the BACK key. The firmware borrows it for ~30 ms at start-up only,
// never while BACK is held, and hands it straight back to the key (volume is applied to the samples,
// so changing it needs no I2C).
// ---------------------------------------------------------------------------
#define SOUND_ENABLED        1
#define SOUND_MCLK_PIN      16     // I2S_MCLK  (Waveshare GPIO table)
#define SOUND_BCLK_PIN       9     // I2S_SCLK
#define SOUND_LRCK_PIN      45     // I2S_LRCK
#define SOUND_DOUT_PIN       8     // I2S_DSDIN: ESP32 -> ES8311
#define SOUND_AMP_PIN       46     // PA_CTRL: NS4150B on when high (10k pull-down keeps it off)
#define SOUND_I2C_SDA_PIN   13     // display board I2C (ES8311, ES7210, RTC, IMU, SHTC3; 2.2k pull-ups)
#define SOUND_I2C_SCL_PIN   14     // = KEY_BACK_PIN
#define ES8311_ADDR       0x18     // CE pulled low
#define SOUND_VOLUME_MAX     10    // volume steps; each is 3 dB, 10 = loudest
#define SOUND_VOLUME_DEFAULT  5
#define SOUND_AMP_HOLD_MS  3000    // amplifier stays on this long after a sound (the next one is instant)
#define SOUND_STARTUP_CHIME   1    // short chime when the speaker is ready at start-up

// SD card layout (FAT32). ALL IR codes live on the card - nothing is compiled into the firmware:
//   /irdb.bin        the code database, made by tools/build_db.py (must match IRDB_SCHEMA in
//                    ir_keys.h). Loaded into PSRAM at boot. Replace it to update the codes, or
//                    upload a new one from the web page (Settings).
//   /irdb.old        previous database, kept by a web upload; used if irdb.bin won't load
//   /ir/...          your own remotes as Flipper ".ir" files, any folder depth. The folder holding a
//                    file is its brand ("/ir/Living room/TV.ir" -> brand "Living room", remote "TV");
//                    a Flipper-IRDB category folder on the way ("TVs", "ACs", ...) sets its type.
#define SD_DB_FILE        "/irdb.bin"
#define SD_DB_OLD         "/irdb.old"
#define SD_DB_NEW         "/irdb.new"
#define SD_USER_DIR       "/ir"
#define SD_MAX_USER_FILES 2000
#define SD_MAX_FILE_BYTES (256 * 1024)

// ---------------------------------------------------------------------------
// Wi-Fi / web remote
// ---------------------------------------------------------------------------
// The device always starts its own access point so a phone can connect directly.
// It also joins your home Wi-Fi (reachable at http://lrremote.local). That network is kept in
// /wifi.txt on the SD card: edit the file on a PC, or change it on the remote (Menu > Wi-Fi
// network), which saves it back to the card. The two lines below are only the defaults used to
// create wifi.txt when the card doesn't have one (and when there is no card at all).
// The name is case-sensitive and must be a 2.4 GHz network. Empty SSID = access point only.
#define HOME_WIFI_SSID      ""              // e.g. "MyNetwork"
#define HOME_WIFI_PASSWORD  ""              // e.g. "my-password"
#define SD_WIFI_FILE        "/wifi.txt"

// Wi-Fi country. Australian routers often use channels 12-13, which ESP32's default "world"
// setting doesn't scan actively - the remote would never find such a network. Use your 2-letter code.
#define WIFI_COUNTRY        "AU"

#define WIFI_AP_SSID      "LR-Remote"
#define WIFI_AP_PASSWORD  "longrange"      // min. 8 characters
#define MDNS_HOSTNAME     "lrremote"
// The remote never gives up on the home network: when an attempt fails it waits this long and
// tries again, forever (and the same if the connection drops later). An attempt in progress is
// never interrupted; only one that has produced no result at all for 60 s is reset.
#define WIFI_STA_RETRY_MS 5000

#define MAX_FAVOURITES    24

// ---------------------------------------------------------------------------
// Power: idle sleep (see power.h)
// ---------------------------------------------------------------------------
// After this many seconds with no key press, no IR being sent and no phone joined to the access
// point, Wi-Fi goes off and the ESP32 light-sleeps; any key wakes it and is acted on at once.
// The web remote over the HOME network can't wake it - join the "LR-Remote" access point (which
// keeps it awake) or press a key first. 0 = never sleep.
#define IDLE_SLEEP_S          90
#define WIFI_RESUME_DELAY_MS  1500   // after waking, Wi-Fi restarts this much later
