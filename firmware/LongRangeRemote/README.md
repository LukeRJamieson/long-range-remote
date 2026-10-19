# LongRangeRemote: universal remote firmware for the long-range remote

The long-range remote works TVs, AV receivers, soundbars, projectors, set-top / cable /
satellite boxes, streaming boxes, Blu-ray / DVD players, hi-fi, game consoles, fans,
air-conditioners, lights and more. The 66-LED head gives it far more range than a normal
remote. Pick the brand and model (or let code search find it) and the keys under the screen send
its codes.

It is based on the HifiRemote firmware, which is left unchanged in its own folder.

## What's in it

- **All IR codes are on the SD card.** Nothing is compiled into the firmware, so you update the
  codes by swapping one file, or by uploading it from your phone. You never have to reflash.
  - `irdb.bin` is the code database. It has about 1,640 brands, 6,760 remotes and 233,000
    codes, generated from Flipper-IRDB and IRDB (Simon Peter's probonopd/irdb).
  - IRDB code sets in protocols the firmware has no encoder for are rendered to exact pulse
    timings with IrpTransmogrifier when the file is built. All 5,118 such codes render, covering
    55 protocols including DirecTV, Dish, Motorola/G.I. Cable, Scientific Atlanta, TiVo, XMP,
    Windows MCE and Proton.
  - Your own remotes are Flipper Zero `.ir` files under `/ir`.
- **28 protocol encoders + raw signals,** sent by the ESP32-S3 RMT peripheral with a hardware
  carrier. The path is GPIO17 → R6 47 Ω → Q2 AOD424 (a logic-level MOSFET, no gate driver). The AA pack is connected to the LEDs (ARM,
  GPIO2) only while a code is being sent.
- **TV remote functions:**
  - power, volume, channel, last channel;
  - guide, info, menu, home, back;
  - arrows + OK, colour keys, Netflix / YouTube / apps;
  - inputs (HDMI 1–4, TV, AV), subtitles, audio language, teletext, aspect, picture mode;
  - record and playback;
  - **channel numbers**, typed with the D-pad and sent like you'd key them in.
- **Vol pad and Nav pad:** these turn the keys into the device's own keys, so you can aim
  and press without looking at the screen.
- **Phone web remote:** connect to the remote's own Wi-Fi "LR-Remote" (password `longrange`) and
  open http://192.168.4.1. It can also join your home Wi-Fi and is then at
  http://lrremote.local (see **Home Wi-Fi** below). The Settings tab uploads a new `irdb.bin` or
  your `.ir` files to the card.
- **Idle sleep:** after 90 s without use, Wi-Fi goes off and the ESP32 light-sleeps.
  - Any key wakes it in milliseconds (GPIO wake on all six key pins), and that key is sent
    straight away.
  - The reflective screen keeps showing its picture.
  - It stays awake while a phone is joined to its access point.
- **Batteries:** the header shows the AA pack (`AA nn%`, from a NiMH voltage curve) and the
  screen's own 18650 (`scr nn%`); the Status screen and the web page's Settings tab show both
  voltages.
  - There is no power switch: the ESP32 connects the pack to the LEDs (ARM, GPIO2) only while it
    sends, and the pack is read from before that gate, so the reading is valid any time.
  - Below 4.6 V it asks for charged cells. Below 4.4 V at rest (`PACK_CUTOFF_V`) it stops sending
    and says the cells are flat.
  - With no cells in it shows `AA out` and key presses send nothing: below 3.5 V at rest the pack
    counts as missing (`PACK_OFF_V`). The IR task re-reads the pack before every send and before
    every frame, and stops if the cells are pulled out.
  - Each send waits `LED_ARM_SETTLE_MS` (30 ms) for the power gate's soft start.
- **LED power:** Menu → *LED power* (SELECT cycles) or the web page's Settings tab sets Full / Half /
  Low (`LED_POWER_PCT` in config.h: 100 / 50 / 25 %). It scales the carrier's duty cycle, so the LED
  current falls in proportion (the fundamental by −4.6 / −10.8 dB). Stored in flash. Whatever a code
  or `.ir` file asks for, the duty is capped at 50 % (`LED_DUTY_MAX_PCT`).

## Building

1. Install the esp32 core ("esp32 by Espressif", 3.x) and the **U8g2** library (Library
   Manager).
2. Set the board options:

   | Setting | Value |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | USB CDC On Boot | Enabled |
   | Flash Size | 16MB (128Mb) |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
   | PSRAM | **OPI PSRAM** (the code database is loaded into it) |

   The firmware is about 1.2 MB.
3. Check `config.h` (sleep time; optionally default home Wi-Fi). The keys are wired straight to GPIOs (UP 18, DOWN 3,
   LEFT 44, RIGHT 43, SELECT 0, BACK 14; `KEY_*_PIN`). The Waveshare board's own buttons share two of them:
   KEY (GPIO18) acts as UP and BOOT (GPIO0) as SELECT.
4. Open `LongRangeRemote.ino` and upload.
5. **Prepare the SD card:** copy `../sd_card/irdb.bin` to the root of a FAT32 microSD card.
   Insert it in the display board's TF slot (reachable through the right wall of the housing).

arduino-cli:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" firmware/LongRangeRemote
```

Without a card, or without `irdb.bin`, the remote starts and tells you what's missing. The web
page shows the same message, with the upload button.

## Using it

| Where | Keys |
|---|---|
| Anywhere | **BACK goes one screen back.** (On the remote screen, the top level, it does nothing.) |
| Lists (menu, brands, models) | UP/DOWN move (hold = fast). RIGHT or SELECT chooses. BACK (or LEFT, or holding SELECT) goes back |
| Remote screen (grid of the device's keys) | Arrows move. **SELECT sends**, and holding it repeats (volume, channel) |
| **Vol pad** | UP/DOWN = volume, LEFT/RIGHT = channel −/+ (or preset / tune / track), SELECT = mute. BACK leaves |
| **Nav pad** | Arrows = the device's arrows, SELECT = OK, BACK = the device's own Back / Return (for disc and streaming menus). Hold BACK or SELECT to leave |
| **Ch 123** (channel number) | UP/DOWN pick the digit, RIGHT adds the next digit, LEFT deletes. SELECT sends the number. BACK leaves |
| Code search | RIGHT next code, LEFT previous code (each one is sent). SELECT sends again. **Hold SELECT = "that worked, use it"**. BACK stops |

- Pad and number tiles only appear when the device has those keys.
- Channel numbers go out 350 ms apart, because TVs ignore digits sent faster than a person types.

### Finding your device

- **Menu → Browse brands:** first letter → brand → model.
  - Model names come from Flipper-IRDB (real remotes).
  - IRDB entries are code sets: "TV 7,7" means device 7, sub-device 7.
- **Menu → Code search:** choose where to search (a brand, a group such as "Popular TV
  brands", or all), then the device type, then a test key (Power, Vol +, Mute, CH +, ...).
  Step through until the device reacts and hold SELECT.
  - Remotes that share a code are tried only once.
  - The most widely shared codes come first, so a TV usually responds within the first few
    tries.

Chosen devices are saved under Menu (your devices, `*` = active). They are stored by name, so
they survive power-off and database updates.

### Power

- **On / off:** the display board's own PWR button (short press on, long press off). The AA pack
  needs no switch: it is disconnected from the LEDs except while a code is sent. Day to day, just
  leave it on: it sleeps by itself.
- **A key held at power-up** is ignored until you let go. If one still reads pressed after 2 s,
  the Status screen names it (a stuck key or a short on the ribbon).
- **Sleep:** set the idle time with `IDLE_SLEEP_S` in `config.h` (0 = never sleep).

### Sound

The display board's speaker gives each key press a short click. Holding a key long enough to leave
a screen or pick a device gives a lower two-note sound, and a code that can't be sent (no AA cells
in, or flat) gives a low double buzz. A three-note chime at start-up shows the speaker works
(`SOUND_STARTUP_CHIME` in `config.h`).

- **Volume / mute:** Menu → *Sound*. With that row selected, LEFT / RIGHT make it quieter / louder
  (10 steps of 3 dB, with a click at each new level) and SELECT mutes or unmutes. Both are saved.
- **How it works:** ESP32 I2S (16 kHz) → the ES8311 codec → the NS4150B amplifier. The sounds are
  generated in the firmware (no sound files). The amplifier is switched off a minute after the last sound
  and during sleep (it draws a few mA while on, nothing while the remote sleeps).
- **The codec and the BACK key share a pin.** The ES8311 is set up once over the display board's I2C
  bus, whose clock is GPIO14 (BACK). The firmware borrows the pin for ~30 ms at start-up, then gives
  it back to the key; volume changes need no I2C. If BACK is held at start-up the set-up waits until
  it is released.
- If the Sound row shows *no codec found*, the codec didn't answer at I2C address 0x18; keys and IR
  work as normal.

## Home Wi-Fi

The remote always runs its own network, "LR-Remote". It can also join your home network, which
is kept in `wifi.txt` on the SD card. There are two ways to set it:

- **On the remote:** Menu → Wi-Fi network.
  - **Network** lists the 2.4 GHz networks nearby, strongest first. Pick yours and type the
    password, or choose "Other" to type the name of a hidden network.
  - The keyboard: arrows move, SELECT types, BACK deletes a character (hold BACK to leave without
    saving). The bottom-left key switches between lower case, upper case and symbols. "Done"
    saves.
  - **Password** edits the current password. **Forget this network** (SELECT twice) goes back to
    LR-Remote only.
  - Every change is written to `wifi.txt` straight away, and the remote reconnects. The screen
    shows the connection status and where the network is kept.
- **On a PC:** edit `wifi.txt` on the card (see `../sd_card/wifi.example.txt`):

  ```
  ssid=MyNetwork
  password=my wifi password
  ```

  Spaces around a value are ignored. Put the value in double quotes to keep spaces at the start
  or end of it. Lines starting with `#` are comments.

How it's stored:
- `wifi.txt` is read at power-up. If the card doesn't have one, the remote creates it from
  `HOME_WIFI_SSID` / `HOME_WIFI_PASSWORD` in `config.h` (empty by default). From then on the file
  wins: `config.h` is only the default for a new card.
- A save writes `wifi.txt.new`, reads it back to check it, then swaps it in. Pulling the card or
  losing power mid-save leaves the old file or the new one, never half of one.
- With no card, the `config.h` network is used. A change made in the menu then only lasts until
  the remote restarts, and the Wi-Fi screen says so.
- The password is stored as plain text on the card, as it is in `config.h`.
- The rules match Wi-Fi's: the name is up to 32 characters and case-sensitive; the password is
  8 to 63 characters (or 64 hex digits), or empty for an open network. Leaving the password empty
  takes a second press of Done, so it can't be wiped by accident.
- **The web remote can't wake it.** While it sleeps, Wi-Fi is off. Press a key, or keep a phone
  joined to "LR-Remote", which keeps it awake.

## SD card

FAT32 microSD in the display board's TF slot. It is read at power-up.

```
/irdb.bin                       the code database (required - from firmware/sd_card)
/irdb.old                       previous database, kept by a web upload, used if irdb.bin won't load
/ir/Living room/TV.ir           your own remote: brand "Living room", remote "TV"
/ir/TVs/Kogan/KALED32.ir        Flipper-IRDB folder names set the device type (here: TV)
/ir/AC/Bedroom.ir               ...as do this firmware's type names (TV, AVR, AC, SetTop, ...)
/wifi.txt                       your home Wi-Fi (created by the remote; see Home Wi-Fi)
```

### Updating the codes

There are two ways:
- **From a phone:** Settings → SD card: IR codes → choose the new `irdb.bin` → "Upload database &
  restart".
  - The file is checked before it replaces the current one. The current one is kept as
    `irdb.old`.
  - If the new file still fails its full check at the next start, the remote falls back to
    `irdb.old` and says so.
- **On a computer:** copy the new `irdb.bin` to the card.

**Size and version limits:**
- The database must fit in PSRAM. About 6.5 MB of the 8 MB is available for it, and today's file
  is 5.9 MB.
- It must be built by this firmware's `tools/build_db.py`. The file carries a schema hash of the
  key / protocol / category numbering, and a mismatching file is refused with a clear message.
  New codes and new button-name rules never change that hash. Adding a new *kind* of key does, and
  then you rebuild the firmware too.

### Formatting the card

Settings → **Format the SD card** erases the card and formats it FAT32 (32 KB clusters). Use it
for a new card or one the remote can't read: it also works on a card that didn't mount at
start-up, or one put in after it.
- **"Put the code database and home Wi-Fi back afterwards"** (on by default) writes the database
  the remote has in memory back to the card as `irdb.bin`, and `wifi.txt` with it. No upload is
  needed, and the remote starts normally next time.
- Your own `.ir` files are erased and can't be put back: keep copies.
- With the box unticked, the card is left empty. Upload `irdb.bin` before the remote restarts.
- It takes from a few seconds to about a minute, depending on the card.

### Your own remotes (/ir)

- Use any Flipper Zero `.ir` file, either parsed or raw signals. These include remotes you
  recorded with a Flipper Zero and files shared online.
- **Upload from the phone** (Settings, with an optional brand / folder name), or copy them to the
  card.
- The folder that holds a file becomes its brand, and the file name becomes the remote.
- They appear under Browse and in Code search like any other remote.
- Up to 2000 files, 256 KB each.

The remote has no IR receiver, so it can't record remotes itself. Record them with a Flipper
Zero, or a phone or PC tool that saves Flipper `.ir` files.

## Regenerating the database

You need:
- Python 3 and Java 11+.
- [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) and
  [irdb](https://github.com/probonopd/irdb), cloned. Pull them again later for new codes.
- [IrpTransmogrifier](https://github.com/bengtmartensson/IrpTransmogrifier/releases)
  1.2.14, from the release zip. Its source zip provides `IrpProtocols.xml`.

```sh
python3 tools/build_db.py --flipper Flipper-IRDB --irdb irdb \
  --irpt-jar IrpTransmogrifier-1.2.14-jar-with-dependencies.jar --irpt-xml IrpProtocols.xml \
  --keys ir_keys.h --bin ../sd_card/irdb.bin --report tools/db_report.txt
```

This takes about 6 minutes. Then copy or upload `irdb.bin`.
- `ir_keys.h` only needs to go back into the firmware if `KEYS`, `CATS` or the protocol list
  changed. The tests below tell you if it did: the schema check fails.
- `tools/irp_render/IrpRender.java` is the small batch renderer the script runs. It is compiled
  with `javac -cp <jar>`.

## Tests (on a PC)

```sh
g++ -O2 -std=c++17 -I. tools/test_all_codes.cpp ir_protocols.cpp ir_db.cpp flipper_ir.cpp -o /tmp/test_all_codes
/tmp/test_all_codes ../sd_card/irdb.bin
```

The test checks:
- every code encodes within the transmitter's limits;
- names round-trip;
- code search covers every remote exactly once (overall, per brand, per group and per type);
- the firmware's key-name matching agrees with the generator on all 145,000 Flipper button
  names;
- `.ir` files parse into the user database;
- damaged or foreign `irdb.bin` files are refused.

`tools/verify_*.py` cross-check the protocol conversions against IrpTransmogrifier.

Wi-Fi:

```sh
g++ -std=gnu++17 -I. tools/wifi_file_test/test.cpp wifi_file.cpp -o /tmp/wifitest && /tmp/wifitest
g++ -std=gnu++17 -Itools/wifi_sim/stub -I. tools/wifi_sim/sim.cpp wifi_mgr.cpp -o /tmp/wifisim && /tmp/wifisim
```

- `wifi_file_test`: `wifi.txt` files written by hand (Notepad's BOM and line endings, spaces,
  quotes) read correctly; every name / password survives a write and read back; the length and
  character rules.
- `wifi_sim`: the connection manager against a model of the ESP32 Wi-Fi driver. It covers routers
  that vanish, wrong passwords, a hung driver, and the menu's network list, which must never scan
  during a connection attempt.

## Files

| File | What it does |
|---|---|
| `LongRangeRemote.ino` | Start-up order: LED supply off → keys/batteries → SD (codes) → settings → sound → IR → UI → web → power |
| `config.h` | Pins, battery thresholds, sleep, Wi-Fi (defaults for a new `wifi.txt`), SD layout |
| `hw.*` | Keys on GPIO18/3/44/43/0/14 (UP/DOWN/LEFT/RIGHT/SELECT/BACK) (internal pull-ups, debounced), the AA pack voltage (GPIO1), the screen's 18650 (GPIO4), wake-on-key |
| `sd_store.*` | SD card: `irdb.bin` into PSRAM (falls back to `irdb.old`), `/ir` files, web uploads, safe rewrite of small files |
| `ir_db.*`, `ir_keys.h` | Database reader (SD image + your remotes) and the generated key numbering |
| `flipper_ir.*` | Flipper `.ir` parser and protocol conversion |
| `ir_protocols.*`, `ir_tx.*` | Encoders and the RMT transmit task (ARM handling, hold/repeat) |
| `ui.*`, `ST7305_U8g2.*` | On-device UI and display driver |
| `power.*` | Idle sleep: Wi-Fi off + light sleep, wake on any key |
| `sound.*` | Key sounds: I2S to the ES8311 codec, its one-off set-up over I2C (borrowing BACK's pin), amplifier on/off, volume / mute |
| `web.*`, `web_page.h`, `wifi_mgr.*` | Phone web remote and SD uploads; home Wi-Fi connection and the network list for the menu |
| `settings.*` | Saved devices, LED power level and sound volume / mute (NVS); the home Wi-Fi network (`wifi.txt`) |
| `wifi_file.*` | Reads and writes `wifi.txt`, and checks a name / password before it is saved |

## Licences

Code databases:
- **Flipper-IRDB:** CC0.
- **IRDB:** "Contains/accesses irdb by Simon Peter and contributors, used under permission. For
  licensing details and for information on how to contribute to the database, see
  https://github.com/probonopd/irdb".

IrpTransmogrifier (GPL-3.0) is used only at build time, to render codes. It is not part of the
firmware.

The ST7305 driver glue comes from Waveshare's examples (Apache-2.0).
