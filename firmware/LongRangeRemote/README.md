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
  - `irdb.bin` is the code database. It has about 1,890 brands, 10,670 remotes and 530,000
    codes, generated from Flipper-IRDB, IRDB (Simon Peter's probonopd/irdb), the LIRC remotes
    database, JP1 device upgrades and SmartIR.
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
- **Bluetooth scanner:** lists the Bluetooth LE devices nearby with their maker, type, address and
  signal strength, on the screen and on the web page (see **Bluetooth scanner** below).
- **Always on:** it never sleeps, so the web page always answers. Switch it off with the PWR button.
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
   | PSRAM | **OPI PSRAM** (the index of the code database is loaded into it) |

   The firmware is about 1.2 MB.
3. Check `config.h` (optionally default home Wi-Fi). The keys are wired straight to GPIOs (UP 18, DOWN 3,
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
| Anywhere | **BACK goes one screen back.** On the remote screen, the top level, it opens the **menu**. The bottom line of every screen shows what the keys do there (OK = SELECT) |
| Lists (menu, brands, models) | UP/DOWN move (hold = fast). RIGHT or SELECT chooses. BACK (or LEFT, or holding SELECT) goes back, to the row you came from |
| Remote screen (grid of the device's keys) | Arrows move. **SELECT sends**, and holding it repeats (volume, channel) |
| **Vol pad** | UP/DOWN = volume, LEFT/RIGHT = channel −/+ (or preset / tune / track), SELECT = mute. BACK leaves |
| **Nav pad** | Arrows = the device's arrows, SELECT = OK, BACK = the device's own Back / Return (for disc and streaming menus). Hold BACK or SELECT to leave |
| **Ch 123** (channel number) | UP/DOWN pick the digit, RIGHT adds the next digit, LEFT deletes. SELECT sends the number. BACK leaves |
| Code search | RIGHT next code, LEFT previous code (each one is sent). SELECT sends again. **Hold SELECT = "that worked, use it"**. BACK stops |

- Pad and number tiles only appear when the device has those keys. Tiles with a double border
  (Vol pad, Nav pad, Ch 123, All keys) open a screen; the others send a code.
- The remote screen remembers where you were when you come back from a pad or the menu.
- **Header:** the two batteries (AA = the LED pack, Scr = the screen's own cell), `IR` while a code
  goes out, and the network state: `AP` (only the remote's own network), `WiFi` (on the home
  network).
- Channel numbers go out 350 ms apart, because TVs ignore digits sent faster than a person types.

### Finding your device

- **Menu → Add a device: pick the brand:** first letter → brand → model.
  - Model names come from Flipper-IRDB (real remotes).
  - IRDB entries are code sets: "TV 7,7" means device 7, sub-device 7.
- **Menu → Add a device: code search:** choose where to search (a brand, a group such as "Popular TV
  brands", or all), then the device type, then a test key (Power, Vol +, Mute, CH +, ...).
  Step through until the device reacts and hold SELECT.
  - Remotes that share a code are tried only once.
  - The most widely shared codes come first, so a TV usually responds within the first few
    tries.
- **Menu → Add a device: not in the list (Discover):** for a device no remote in the database
  matches. It works for any device that uses one of the 18 protocol families the remote can sweep
  (NEC, NEC extended, Samsung, Sony, RC5, RC6, Panasonic, Pioneer, JVC, Denon, Sharp, ...).
  1. **Sweep.** Pick a protocol (or all of them, about 35 minutes; NEC alone is about 4). The
     remote tries every device address, each with the function numbers that the database's remotes
     most often use for Power, Vol + and Mute. Addresses that occur in the database go first.
     Aim at the device and press SELECT the moment it does anything.
  2. **Which address.** A reaction shows up late, so the remote steps a few addresses back.
     RIGHT tries the next one; when the device reacts again, hold SELECT.
  3. **Learn keys.** RIGHT / LEFT step through that address's functions, the likely ones first,
     and the screen says what each usually is on other remotes. When the device does something,
     hold SELECT and pick the key's name from the list.
  4. **Save.** BACK, then *Save as a device*. The keys are written to
     `/ir/Discovered/<protocol> <address>.ir` on the SD card (a Flipper `.ir` file), the remote
     restarts and the new device is the one in use. Discovering the same address again later
     starts with the keys already saved, so you can add more.
  - The same thing is on the web page's **Discover** tab, where naming keys is quicker, any
    function number can be sent directly, and a known protocol + address can be opened without a
    sweep. The old one-code-at-a-time manual entry is at the bottom of that tab.
  - A sweep sends unknown codes to everything in front of the remote. Point it only at the
    device you mean, and expect other IR gear in the beam to react too.
  - Extended NEC has 65,536 possible addresses; the sweep covers the ones the database knows
    (about 670). Sony 20-bit, Aiwa and the raw-only protocols are not swept.

Chosen devices are saved under Menu (your devices, `*` = active). They are stored by name, so
they survive power-off and database updates.

### Bluetooth scanner

**Menu → Bluetooth scanner** (or the web page's **Bluetooth** tab) lists the Bluetooth LE devices
the remote can hear: phones, earbuds, watches, trackers, TVs, speakers, keyboards, sensors...

- **The list:** strongest signal first, with signal bars and dBm. UP / DOWN move, OK (or RIGHT)
  opens a device, hold OK to sort by signal, name or newest. Devices not heard for 30 s move to
  the bottom as "gone". The web tab adds a text filter and a **Trackers only** switch.
- **Details:** type, brand, the detail (e.g. "AirPods Pro 2nd Gen", "Windows laptop", "Find My:
  away from its owner"), name, address and what kind it is, the MAC maker, when it was heard,
  services, the maker data in hex, and a signal meter that updates every second, so you can walk
  towards a device to find it.
- **How it knows:** every brand and type says where it came from:
  - *maker data*: the company ID in the advert, and for Apple and Microsoft the message after it
    (AirPods / Beats model, AirTag and other Find My devices, iBeacon, HomeKit, AirPlay; Windows
    PC, Xbox, Swift Pair);
  - *services*: Bluetooth SIG services (heart rate, keyboard / mouse, LE Audio...), the service
    numbers the SIG gives to companies (Tile, Google, Sonos...), Google Find Hub tags and
    Eddystone beacons;
  - *appearance*: the device's own "I am a watch / keyboard / ..." value;
  - *name*: words in its name ("[TV] Samsung", "WH-1000XM5", "Pixel 8");
  - *MAC prefix*: only for a fixed (public) address, from `oui.bin` on the SD card.
- **Limits:**
  - The ESP32-S3 has Bluetooth **LE** only. Devices that use nothing but classic Bluetooth
    (older headsets and car kits) don't appear.
  - Phones, earbuds and watches use a **random address that changes** every few minutes, so the
    same phone can be listed twice, and its MAC says nothing about the maker.
  - Many devices advertise very little until they are in pairing mode.
- **Radio time:** the radio scans only while the screen or the web tab is open (and 15 s after),
  and listens 60 % of the time, leaving the rest to Wi-Fi.
- **oui.bin** (MAC prefix → maker) is optional: copy `../sd_card/oui.bin` to the card, or upload
  it from the Bluetooth tab (it is used at once).

### Power

- **On / off:** the display board's own PWR button (short press on, long press off). The AA pack
  needs no switch: it is disconnected from the LEDs except while a code is sent. The remote never
  sleeps: switch it off when you're done, or leave it on USB.
- **A key held at power-up** is ignored until you let go. If one still reads pressed after 2 s,
  the Status screen names it (a stuck key or a short on the ribbon).

### Sound

The display board's speaker gives each key press a short click. Holding a key long enough to leave
a screen or pick a device gives a lower two-note sound, and a code that can't be sent (no AA cells
in, or flat) gives a low double buzz. A three-note chime at start-up shows the speaker works
(`SOUND_STARTUP_CHIME` in `config.h`).

- **Volume / mute:** Menu → *Sound*. With that row selected, LEFT / RIGHT make it quieter / louder
  (10 steps of 3 dB, with a click at each new level) and SELECT mutes or unmutes. Both are saved.
- **How it works:** ESP32 I2S (16 kHz) → the ES8311 codec → the NS4150B amplifier. The sounds are
  generated in the firmware (no sound files). The amplifier is switched off a minute after the last sound
  (it draws a few mA while on).
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

## SD card

FAT32 microSD in the display board's TF slot. It is read at power-up.

```
/irdb.bin                       the code database (required - from firmware/sd_card)
/irdb.old                       previous database, kept by a web upload, used if irdb.bin won't load
/ir/Living room/TV.ir           your own remote: brand "Living room", remote "TV"
/ir/TVs/Kogan/KALED32.ir        Flipper-IRDB folder names set the device type (here: TV)
/ir/AC/Bedroom.ir               ...as do this firmware's type names (TV, AVR, AC, SetTop, ...)
/wifi.txt                       your home Wi-Fi (created by the remote; see Home Wi-Fi)
/oui.bin                        MAC prefix -> maker for the Bluetooth scanner (optional, from firmware/sd_card)
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
- Size is no longer limited by memory. Only the index (brand and remote names) is loaded into
  PSRAM; the codes are read from the card when needed, so the file can be tens of megabytes.
  A file that size is quicker to copy with a card reader than to upload over Wi-Fi.
- While the card is out, database codes can't be sent (your on-screen remote shows them, but
  nothing goes out). Put the card back and it carries on.
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
- The code sources, cloned (pull them again later for new codes):
  [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB),
  [irdb](https://github.com/probonopd/irdb),
  [lirc-remotes](https://github.com/probonopd/lirc-remotes) (the LIRC database),
  [deviceupgrades](https://github.com/hifiremote/deviceupgrades) (JP1 device upgrades) and
  [SmartIR](https://github.com/smartHomeHub/SmartIR) (mostly air conditioners).
  `--lirc`, `--jp1`, `--smartir` and `--irdb` are each optional.
- [IrpTransmogrifier](https://github.com/bengtmartensson/IrpTransmogrifier/releases)
  1.2.14, from the release zip. Its source zip provides `IrpProtocols.xml`.

```sh
python3 tools/build_db.py --flipper Flipper-IRDB --irdb irdb \
  --lirc lirc-remotes --jp1 deviceupgrades --smartir SmartIR \
  --irpt-jar IrpTransmogrifier-1.2.14-jar-with-dependencies.jar --irpt-xml IrpProtocols.xml \
  --keys ir_keys.h --bin ../sd_card/irdb.bin --report tools/db_report.txt
```

This takes a minute or two. Then copy `irdb.bin` to the card (or upload it; at 44 MB a card
reader is quicker).

How the extra sources are read (`tools/more_sources.py`):
- **LIRC:** each remote's buttons, as protocol / device / function where the XML gives one, else
  from its Pronto timing (decoded back to a protocol when it is one the firmware encodes).
- **JP1:** RemoteMaster `.rmdu` and KeymapMaster `.txt` upgrades. The protocol "executors" for
  the common protocols (NEC family, RC-5, RC-6, Sony, Panasonic, JVC, Aiwa, Denon, Sharp,
  Pioneer and their combos) are translated; files using other executors are skipped and counted
  in `tools/db_report.txt` (about 1,650 of 5,300 files).
- **SmartIR:** every state of an air conditioner (mode, fan, temperature) is one code, so those
  remotes have hundreds or thousands of keys named like `Cool Auto 24`.
- A remote whose codes are all contained in another remote of the same brand is dropped, so the
  sources don't pile up duplicates (about 5,200 dropped).
- `ir_keys.h` only needs to go back into the firmware if `KEYS`, `CATS` or the protocol list
  changed. The tests below tell you if it did: the schema check fails.
- `tools/irp_render/IrpRender.java` is the small batch renderer the script runs. It is compiled
  with `javac -cp <jar>`.

## Tests (on a PC)

```sh
g++ -O2 -std=c++17 -I. tools/test_all_codes.cpp ir_protocols.cpp ir_db.cpp flipper_ir.cpp -o /tmp/test_all_codes
/tmp/test_all_codes ../sd_card/irdb.bin
```

`tools/discover_test/test.cpp` tests Discover the same way (its build line is at the top of the
file): the sweep, narrowing, naming, and that a saved file loads back as exactly the codes that
were sent, for every protocol family. `tools/ui_sim` draws the real screens on a PC.

The test checks:
- every code encodes within the transmitter's limits;
- names round-trip;
- code search covers every remote exactly once (overall, per brand, per group and per type);
- the firmware's key-name matching agrees with the generator on all 145,000 Flipper button
  names;
- `.ir` files parse into the user database;
- damaged or foreign `irdb.bin` files are refused.

`tools/verify_*.py` cross-check the protocol conversions against IrpTransmogrifier.

Bluetooth scanner:

```sh
g++ -std=c++17 -I. tools/ble_test/test.cpp ble_ident.cpp ble_scan.cpp -o /tmp/ble_test && /tmp/ble_test
tools/ui_sim/build.sh script_ble.h /tmp/ble_screens
cd tools/web_test && python3 mock.py ../../web_page.h & node run_ble.js
```

- `ble_test`: adverts built byte by byte (AirPods, Beats, iBeacon, AirTag, iPhone, HomeKit,
  Windows laptop, Swift Pair, Google Find Hub, Eddystone, Tile, a heart-rate strap, a Samsung TV,
  a public address) are identified correctly, truncated and garbage adverts are safe, and the
  device list keeps names, counts and signal across adverts and makes room when full.
- `script_ble.h` draws the list and details screens; `run_ble.js` checks the web tab on a phone
  screen (polling only while it is open, details staying open, filters).

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
| `LongRangeRemote.ino` | Start-up order: LED supply off → keys/batteries → SD (codes) → settings → sound → IR → UI → web |
| `config.h` | Pins, battery thresholds, Wi-Fi (defaults for a new `wifi.txt`), SD layout |
| `hw.*` | Keys on GPIO18/3/44/43/0/14 (UP/DOWN/LEFT/RIGHT/SELECT/BACK) (internal pull-ups, debounced), the AA pack voltage (GPIO1), the screen's 18650 (GPIO4) |
| `sd_store.*` | SD card: opens `irdb.bin` (falls back to `irdb.old`) and reads codes from it on demand, `/ir` files, web uploads, safe rewrite of small files |
| `ir_db.*`, `ir_keys.h` | Database reader (SD image + your remotes) and the generated key numbering |
| `flipper_ir.*` | Flipper `.ir` parser and protocol conversion |
| `ir_protocols.*`, `ir_tx.*` | Encoders and the RMT transmit task (ARM handling, hold/repeat) |
| `ui.*`, `ST7305_U8g2.*` | On-device UI and display driver |
| `sound.*` | Key sounds: I2S to the ES8311 codec, its one-off set-up over I2C (borrowing BACK's pin), amplifier on/off, volume / mute |
| `web.*`, `web_page.h`, `wifi_mgr.*` | Phone web remote and SD uploads; home Wi-Fi connection and the network list for the menu |
| `settings.*` | Saved devices, LED power level and sound volume / mute (NVS); the home Wi-Fi network (`wifi.txt`) |
| `wifi_file.*` | Reads and writes `wifi.txt`, and checks a name / password before it is saved |
| `ble_scan.*`, `ble_glue.cpp` | Bluetooth scanner: the list of devices heard (no Arduino code), and the radio on the core's BLE library |
| `ble_ident.*`, `ble_db.h` | Reads an advert and works out the brand / type; the company, service and appearance tables (generated by `tools/ble_db/make_ble_db.py`, which also makes `oui.bin`) |

## Licences

Code databases:
- **Flipper-IRDB:** CC0.
- **SmartIR:** MIT.
- **LIRC remotes** (probonopd/lirc-remotes) and **JP1 device upgrades**
  (hifiremote/deviceupgrades): community collections of codes for people's own remotes. Neither
  repository states a licence. Build without `--lirc` / `--jp1` if that matters for how you
  share the database file.
- **IRDB:** "Contains/accesses irdb by Simon Peter and contributors, used under permission. For
  licensing details and for information on how to contribute to the database, see
  https://github.com/probonopd/irdb".

IrpTransmogrifier (GPL-3.0) is used only at build time, to render codes. It is not part of the
firmware.

The ST7305 driver glue comes from Waveshare's examples (Apache-2.0).

Bluetooth scanner data (`ble_db.h`, `oui.bin`, made by `tools/ble_db/make_ble_db.py`):
- company IDs, service UUIDs and appearance values: the Bluetooth SIG's assigned numbers, as
  collected in NordicSemiconductor/bluetooth-numbers-database, and the company service UUIDs from
  Wireshark's `packet-bluetooth.c`;
- Apple message formats: Wireshark's Apple continuity dissector, and newer AirPods / Beats model
  numbers from the Flipper Zero `ble_spam` app; Microsoft: the [MS-CDP] beacon and Swift Pair
  documentation; Google Find Hub: Google's Find Hub Network Accessory Specification;
- `oui.bin`: the IEEE OUI registry as listed in Nmap's `nmap-mac-prefixes`. The script can build it
  from the IEEE's own `oui.csv` instead (`--oui-csv`).
