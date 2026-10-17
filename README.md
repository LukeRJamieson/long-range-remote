# Long-range universal remote (Lite, 4 × AA)

A hand-held universal IR remote with far more range than a normal one. A head of 66 narrow-beam
(±5°) SFH 4545 emitters sits at the front of a 107 × 52 × 170 mm block. Behind it are:

- the Waveshare **ESP32-S3-RLCD-4.2** display, at the front of the top deck;
- a D-pad, a SELECT key and a BACK key behind the display, under your thumbs, so your hands never
  cover the screen;
- a bay for **four AA NiMH cells** underneath, which power the LEDs only. You take them out and
  charge them in any NiMH charger. The display board and ESP32 run from the display board's own
  18650, charged through its USB-C.

Everything is menu driven from the screen, or from a phone over Wi-Fi.

There are two boards, both within JLCPCB's cheapest 100 × 100 mm tier:

| Board | Size | What's on it |
|---|---|---|
| LED head | 99.8 × 44.8 mm, R3 corners | 66 × SFH 4545 (14 × 5 grid, corners left for the M3 holes) in 33 strings of 2, one 24 Ω 1206 resistor per string |
| Control | 82 × 52 mm | D-pad + SELECT + BACK (six 6 mm keys, straight to ESP32 GPIOs), the ESP32-switched power gate for the AA pack, the LED switch (a logic-level MOSFET driven straight from a GPIO), the pack-voltage divider, display and LED-head connectors |

KiCad 7 format; it opens in KiCad 7, 8 or 9.

## The system

```
 FRONT                                   top deck (lid)                                     BACK
 ┌──────────────┐  10-way ribbon  ┌────────────────────────┐ 16-way ribbon ┌──────────────────────┐
 │  LED HEAD    │◄════════════════│ CONTROL BOARD  82 x 52 │══════════════►│ Waveshare            │
 │ 99.8 x 44.8  │ J1 ↔ J5         │ D-pad, SELECT, BACK    │ J2 ↔ header   │ ESP32-S3-RLCD-4.2    │
 │ 66×SFH 4545  │ odd = LED_PWR   │ power gate (back)      │               │ (own 18650, USB-C)   │
 │ 33 × 24 Ω    │ even = LED_RTN  │ LED switch (back)      │               └──────────────────────┘
 └──────────────┘                 └────────────────────────┘
                                    ▲ J1 (XH): 4 × AA NiMH holder, under the display
```

## Design notes

| | |
|---|---|
| Strings | 33 × (2 LEDs + 24 Ω) straight off the AA pack |
| LED current per string | 100 mA on fresh cells (5.4 V), 75–83 mA for most of the discharge (4.8–5.0 V), ~58 mA nearly flat (4.4 V) |
| LED current, total | ≤ 3.3 A peak, ~0.87 A average while sending (C1/C2 supply the peaks) |
| Power | 4 × AA NiMH in a Keystone 2478 holder → J1 → P-FET power gate driven by the ESP32 (no switch). Display: its own 18650 |
| LED switching | One low-side AOD424 (DPAK, fully on at 2.5 V) driven straight from GPIO17 through R6 47 Ω; the power gate (ARM) powers the LEDs |
| Battery level | Pack ÷ 2 on GPIO1, and the screen's 18650 on GPIO4 |
| Parts cost | $75.08 AUD ex GST + US$6.21 at Digi-Key/Mouser, $7.15 at Core Electronics (plus the display board and AA cells) |

- **Why 24 Ω:** NiMH cells straight off the charger sit at about 1.35 V each under this load. 24 Ω
  holds the strings to the SFH 4545's 100 mA rating then; for the long flat middle of the
  discharge (1.2–1.25 V a cell) they get 75–83 mA.
- **Runtime:** with ~2000 mAh cells, about 2.3 hours of *continuous* transmitting, which is tens of
  thousands of key presses. Idle, the pack feeds only the 5 µA pack-sense divider (the LEDs are
  disconnected), far less than the cells' own self-discharge. How long the screen
  side lasts depends on the display board's 18650 (it light-sleeps after 90 s idle).
- **No power switch.** The ESP32 connects the pack to the LEDs only while it sends: ARM (GPIO2) →
  R16 10k → Q6 2N3904 pulls Q5's gate to GND through R15 10k. Q5 is an IPD80P03P4L-07 P-MOSFET (DPAK,
  12 mΩ) between J1 and VBAT; R14 100k holds it off whenever ARM is low or the ESP32 is in reset or
  asleep, so the pack and the LEDs are disconnected and nothing can light.
  - C7 100 nF from gate to drain is a soft start: the drain rises ~2 V/ms, so C1/C2 charge at ~2 A.
  - C8 1 µF from gate to source keeps Q5 off when cells go in. Without it, C7 would pull the gate low
    as the pack connects and jump-charge C1/C2 at 10–20 A.
  - Together they make Q5 slow to turn on, so the firmware waits 30 ms after raising ARM.
  - The pack is read at J1 (R12/R13 470k), before Q5, so the header shows `AA nn%` any time and
    `AA out` with no cells in.
  - To turn the screen off, use the display board's own PWR button (long press).
- **LED power in firmware.** Menu → *LED power* or the web page's Settings tab: Full / Half / Low =
  100 / 50 / 25 % of the carrier's on-time (`LED_POWER_PCT`). LED current falls in proportion, the
  signal the receiver sees by −4.6 / −10.8 dB (range ~×0.6 / ×0.3 on the same receiver noise floor,
  roughly; check it on your TV). Handy for close range and for saving the cells.
- **Use NiMH, not alkalines.** Four fresh alkalines read 6.0–6.4 V, which would push the strings
  well past 100 mA. Low-self-discharge cells (eneloop-type) keep their charge in a drawer.
- **When to swap cells:** the header shows `AA nn%` (or `AA out`) and the screen's own battery
  (`scr nn%`). Below 4.6 V it says "AA cells low". Below **4.4 V at rest it stops sending** ("AA cells
  flat"): that is 1.1 V a cell, flat for NiMH. Q2's gate drive comes from the ESP32's 3.3 V, so the LED
  switch works the same right down to that point.
- **Carrier duty is capped at 50 %** (`LED_DUTY_MAX_PCT`), whatever a code or `.ir` file asks for: on
  fresh cells the strings peak at ~110 mA, so the average must stay well under the SFH 4545's 100 mA
  DC rating.
- **Check J1's polarity when you crimp the holder's plug** (red = pin 1, BATT). There is no
  reverse-polarity protection: a reversed pack would drive Q5's body diode and the electrolytics
  backwards.
- **Safety interlock:** the LEDs only light while ARM *and* IR_DRV are high. ARM (through Q6 and Q5)
  powers the LEDs, and IR_DRV switches Q2. The firmware starts the carrier `LED_ARM_SETTLE_MS` (30 ms)
  after ARM, once VBAT is up, and checks the pack before every frame. A held key stops after 15 s. It is still a strong IR source, so don't look into the
  head up close.
- **The ±5° beam needs a little aiming,** but less than you'd think. At 3 m the spot is about 50 cm
  across, and indoors, walls and ceiling reflect enough to reach a TV's sensor off-axis.
- **Standby:** the firmware sleeps after 90 s idle (Wi-Fi off, ESP32 light sleep) and wakes on any
  key. That key is sent at once. The reflective screen keeps its picture while asleep.

## Files

| Path | What it is |
|---|---|
| `docs/Long-Range_Remote_Lite_User_Guide.pdf` | **User guide**: setup, using the remote, batteries, the web page, home Wi-Fi, building the housing, troubleshooting |
| `led_head_lite/led_head_lite.kicad_pro` | LED head project (schematic + PCB) |
| `control_lite/control_lite.kicad_pro` | Control board project (schematic + routed PCB) |
| `gerbers/*_jlcpcb.zip` | Gerber + drill zips, ready to upload to JLCPCB |
| `bom/lite_digikey_bom.csv` | BOM with refs, prices and stock |
| `bom/lite_digikey_upload.csv` | Digi-Key BOM-manager upload |
| `bom/lite_core_electronics.csv` | Display ribbon and header (Core Electronics) |
| `firmware/LongRangeRemote/` | **Universal remote firmware**: TVs, AV receivers, set-top boxes, soundbars, projectors, hi-fi, air-cons..., phone web remote, idle sleep. See its README |
| `firmware/sd_card/irdb.bin` | **The IR code database (~6,800 remotes). Copy it to the SD card**: all codes live on the card, not in the firmware |
| `firmware/sd_card/wifi.example.txt` | Example of `wifi.txt`, the home Wi-Fi file on the card. The remote creates and updates `wifi.txt` itself (Menu → Wi-Fi network); this is for setting it on a PC |
| `firmware/bringup/bringup.ino` | Bring-up firmware: keys, NEC transmit, LED arming, battery voltage |
| `previews/*_front.png`, `*_back.png`, `*_schematic.png` | Both sides of each board and the schematics |
| `previews/*_3d_*.png` | 3D renders of both boards with their components (front, back, three-quarter views) |
| `pdf/` | Printable PDFs: `*_schematic.pdf`, and `*_pcb.pdf` with top/bottom copper, top/bottom assembly and a 1:1 page to print at 100 % for checking fit |
| `housing/` | 3D-printable housing (Prusa MINI): STEP for Fusion 360, print-ready STLs, parametric CadQuery source. See `housing/README.md` |
| `tools/` | Generators (see "Regenerating"). `tools/bin/` is where Freerouting goes if you re-autoroute (not included: 64 MB) |
| `sim/control_lite_sim.py` | ngspice simulations of the control board + LED head: cell insertion, power-gate turn-on/off, 38 kHz drive, switching transients over the ribbon, worst cases (needs ngspice) |

The `_lite` in the board and file names is historical: this is the only version.

## Boards

### LED head (99.8 × 44.8 mm, R3 corners)

- **Front:** 66 × SFH 4545 on a 14 × 5 grid (6.8 mm along a row, 7.0 mm between rows), rows of
  12 / 14 / 14 / 14 / 12. The four corner positions are left empty for the M3 holes at (±44.2, ±14.0).
- **Back:** 33 × 24 Ω resistors, plus J1.
  - The resistors are 1206 SMD with hand-solder pads (Panasonic ERJ-P08J240V, 0.66 W,
    pulse-withstanding), in the bands between the LED rows, each right behind its string's last LED.
  - Each sees up to 0.24 W, but only while a carrier pulse is on.
  - Solder them before the LEDs, while the back is still flat.
- **J1** is a plain 2 × 5 pin header on the back, along the bottom edge, centred. Odd pins are
  LED_PWR and even pins LED_RTN, so a reversed plug only puts about 3 V of reverse bias on each LED
  (under the 5 V rating) and nothing lights. Mark pin 1 on the socket anyway.
- **Copper:** the F.Cu pour is LED_PWR (it feeds every string's first anode); the B.Cu pour is
  LED_RTN. No trace turns at a right angle.

### Control board (82 × 52 mm)

**Front (facing up through the lid): the six keys and nothing else.**
- SW1–SW4: 6 mm D-pad (Omron B3F-1000) on the left; SW5 SELECT, the same switch, on the right (the
  housing gives it a round cap); SW6 BACK at the D-pad's lower right (a square cap).

**Back (everything else, hanging into the tray):** the ribbon connectors, the LED switch (Q2, a DPAK
soldered on the back, right under J5), the reservoir capacitors, the AA plug, and the power gate (Q5,
also a DPAK, with Q6, R14, R15, R16, C7 and C8) behind SELECT's right-hand side.

```
 J1 BATT (4 x AA) ──┬── R12/R13 470k ÷ 2 ── GPIO1 (pack voltage, always readable)
                    └── Q5 IPD80P03P4L-07 (P-FET) ── VBAT ──┬── C1, C2 470 µF, C3 100 nF
        gate: R14 100k to BATT, C8 1 µF to BATT,             ├── J5 odd pins ──► LED strings
              C7 100 nF to VBAT (soft start);
              Q6 2N3904 pulls it to GND via R15 when ARM (GPIO2) is high
                                                   LED_RTN ◄── J5 even pins
 IR_DRV (GPIO17, 38 kHz) ── R6 47 Ω ── Q2 AOD424 (low side, logic level) ── GND
```

- **Pull-downs:** R4 (100 k) keeps Q6 off while the ESP32 boots, and R7 (100 k) holds IR_DRV, and
  through R6 Q2's gate, low.
- **Q2 needs no gate driver:** the AOD424 is specified at 2.5 V gate drive (5.7 mΩ max), so the 3.3 V
  GPIO turns it fully on. R6 47 Ω plus the GPIO's own resistance (drive strength 2) slows its switching
  edge on purpose: a fast edge makes the ribbon ring and reverse-biases the LEDs past their 5 V rating.
  With 47 Ω there is none, the GPIO peaks at ~36 mA, and Q2 dissipates under 0.1 W (`sim/`).
- **Keys:** each key shorts its own ESP32 GPIO to GND (see the J2 table). The ESP32's internal
  pull-ups (about 45 kΩ) hold the pins high, so there are no key resistors and no expander.
  - R11 (1 kΩ) sits in series with RIGHT, on GPIO43. That pin is UART0 TX, which the ESP32's boot
    ROM drives for a moment at power-up; the resistor limits a key held then to 3.3 mA.
  - BACK is on GPIO14, the display board's I2C clock (SCL), the only free header pin that isn't USB.
    The display board's own pull-up holds it high. The firmware only uses that I2C bus for ~30 ms at
    start-up (to set up the speaker's codec), and SCL pulled low while SDA stays high is never an I2C
    start, so the display board's chips ignore the key.
  - The firmware debounces in software and wakes from light sleep on any of the six pins.
- Power tracks (BATT, VBAT, LED_RTN) are 1.5 mm, with GND poured on both layers. No track turns at a
  right angle: corners the autorouter leaves are cut to 45° when the board is generated.

## Connections

### Ribbons (straight-through: pin n = pin n)

| Cable | Ends | Pinout |
|---|---|---|
| 10-way (Adafruit 370, 6") | LED head J1 ↔ control J5 | odd pins LED_PWR, even pins LED_RTN (5 conductors each way) |
| 16-way (Adafruit 2 × 8, 12", **cut to ~70 mm**) | control J2 ↔ Waveshare header | see below |

`tools/check_system.py` checks both ribbons pin by pin, and that the firmware's pin numbers
(`config.h`) match the GPIO each J2 net lands on.

**Shorten the 16-way ribbon before fitting it.** Both of its ends are in the lid (the display header
and J2 are about 50 mm apart along the ribbon's route), and there is no room for 30 cm of ribbon.
Cut it so ~70 mm remains between socket centres, and press one of the CNC Tech 3030-16-0102-00
sockets (Digi-Key LKC16H-ND, in the BOM) onto the cut end in a vice, with a scrap of wood each side.
Copy the original socket at the other end: the red stripe at the same end and the polarising key
on the same face of the ribbon, so it stays straight-through (pin n = pin n). Check with a
multimeter that pin 1 goes to pin 1 and pin 16 to pin 16 before plugging it in.

**Routing (housing):** the two ribbons share the ~3 mm gap between the battery bay's roof and the
display's 18650 cell (it hangs below the display board in an open cradle). Lay them side by side, flat:
the 10-way on the left half of the roof (towards J5), the 16-way on the right half (towards J2).

### Control J2 ↔ Waveshare 2 × 8 header

| J2 pin | Waveshare | Used for | | J2 pin | Waveshare | Used for |
|---|---|---|---|---|---|---|
| 1 | 3V3 | — (not used) | | 2 | VBUS | — (not used) |
| 3 | GND | GND | | 4 | GND | GND |
| 5 | **GPIO0** | **SELECT** key (also BOOT) | | 6 | GPIO19 | — (USB) |
| 7 | **GPIO1** | **AA pack ÷ 2** (ADC) | | 8 | GPIO20 | — (USB) |
| 9 | **GPIO2** | **ARM** | | 10 | **U0TXD (GPIO43)** | **RIGHT** key, through R11 1 k |
| 11 | **GPIO3** | **DOWN** key | | 12 | **U0RXD (GPIO44)** | **LEFT** key |
| 13 | **GPIO17** | **IR_DRV** (RMT, 38 kHz carrier) | | 14 | SDA (GPIO13) | — (not used) |
| 15 | **GPIO18** | **UP** key | | 16 | **SCL (GPIO14)** | **BACK** key (display board's I2C pull-up) |

The keys use pins the Waveshare board brings out but doesn't need:
- **GPIO0** is the BOOT strap: holding SELECT through a reset starts the bootloader.
- **GPIO43 / GPIO44 (UART0):** the board's USB-C goes to the ESP32-S3's own USB, so the serial
  monitor and flashing don't use UART0.
- **GPIO18** is also the Waveshare board's own KEY button (active low). It is in parallel with
  UP, so that button works as UP too.
- UP is on pin 15 and SELECT on pin 5 because pin 15 is at the D-pad's end of the header and pin 5
  at SELECT's: neither key's track has to cross the others on the control board.
- GPIO3 is a strapping pin, but its strap function is unused by default.
- **GPIO14** is the display board's I2C SCL. The firmware only uses that bus once, for ~30 ms at
  start-up, to set up the speaker's ES8311 codec, then gives the pin back to BACK (see Keys above).
  If you add I2C code later, do the same: borrow the pin, never while BACK is held, and hand it back.

**Waveshare end of the 16-way ribbon:**
- The Waveshare header is female, so push two 8-pin pieces of Pololu double-sided header into the
  ribbon socket to make it a plug.
- Put the red stripe (pin 1) on 3V3. Looking at the display board from the back with its 18650 at the
  bottom, **pin 1 (3V3) is the bottom-left pin** (checked on a real board). The bottom row, nearest the
  18650 holder, is the odd pins, reading 3V3, GND, GP0 … GP18 left to right; the top row is the even
  pins, VBUS, GND, GP19 … SCL. The socket isn't keyed, and a ribbon plugged in the wrong way
  round puts 3.3 V on the BACK key's line (pressing BACK would then short the display's 3.3 V supply).
  To check, plug the ribbon into the display only, power the display from USB, and measure 3.3 V between
  pins 1 and 3 of the ribbon's free socket (the control board end) before plugging that end in.

## Firmware

**`firmware/LongRangeRemote/` is the firmware to use.** It is a universal remote:
- **all IR codes on the SD card** (`firmware/sd_card/irdb.bin`): about 1,640 brands, 6,760
  remotes and 233,000 codes from Flipper-IRDB + IRDB. Update them by swapping the file or
  uploading it from your phone, without reflashing;
- set-top-box protocols such as DirecTV, Dish, Motorola and TiVo, pre-rendered with
  IrpTransmogrifier;
- TV functions including channel-number entry, colour keys, guide and HDMI inputs;
- a BACK key that goes one screen back from anywhere;
- Vol / Nav pads that turn the keys into the device's own keys;
- code search ordered by how common each code is;
- your own Flipper `.ir` files from the SD card;
- a phone web remote;
- LED power levels (Full / Half / Low);
- key sounds on the display board's speaker, with volume and mute (Menu → Sound);
- idle sleep with wake on any key.

Build it with Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" and **OPI PSRAM**, which holds
the codes. Then put `irdb.bin` on a FAT32 microSD card. See `firmware/LongRangeRemote/README.md`.

### Bring-up firmware

`firmware/bringup/bringup.ino` is a minimal test of the hardware. It is for Arduino-ESP32 core 3.x
and compiles cleanly for the ESP32-S3 with all warnings on.

| Key | Action |
|---|---|
| Up / Down | Step through the code list |
| Left / Right | Step through the device list |
| SELECT | Sends the selected NEC code (holding it sends NEC repeat frames) and prints the pack voltage, with a warning below 4.6 V |
| BACK | Back to the first code of the first device (and shows GPIO14 reads) |

The Serial prints in `printSelection()` and `handleButtons()` mark where your on-screen menu hooks in.

Each transmission follows this sequence:

1. Check the pack (nothing is sent with no cells in).
2. Raise ARM.
3. Wait 30 ms for the power gate.
4. Send with the RMT peripheral (38 kHz carrier, 33 % duty, idle low).
5. Drop ARM.

The code table is placeholder NEC codes. Capture your own devices' codes and replace it.

Never hold IR_DRV high, and don't look into the head at close range.

## Bill of materials

Prices were checked **3–4 Oct 2026**; on 12 Oct the main lines (LEDs, 24R resistors, AOD424, IPD80P03P4L-07,
470 µF, 100 nF, 1 µF, 2N3904, 10-way header, the new IDC socket) were rechecked as in stock at Digi-Key AU. Digi-Key Australia prices are **AUD ex GST**; Core Electronics
prices are **AUD inc GST**.

| | Cost |
|---|---|
| **Digi-Key**, everything except the parts below | **$75.08 AUD ex GST** |
| 33 × ERJ-P08J240V, IPD80P03P4L-07, 2N3904, Keystone 2478 holder | **US$6.21**, priced from Digi-Key US or Mouser US; the upload file lists them by manufacturer part number |
| JST XHP-2 plug + SXH-001T-P0.6 crimps for the AA holder's leads | in the upload file by part number; prices not checked (you also need an XH crimp tool) |
| **Core Electronics** (16-way ribbon, header) | **$7.15 AUD inc GST** |

**Not included:** the Waveshare ESP32-S3-RLCD-4.2, four AA NiMH cells and a charger, an
**unprotected** 18650 for the display board (it has its own protection: S8261 + 8205; protected cells are a
few mm longer and may not fit its holder), the PCBs, and the housing hardware (see `housing/README.md`). The LED head's 2 × 5 header is
two 5-pin pieces of the Core Electronics header strip.

## Ordering the PCBs (JLCPCB)

Upload each `gerbers/*_jlcpcb.zip` with:

- **Board:** 2 layers, 1.6 mm, 1 oz copper, HASL.
- **Board sizes:** 99.8 × 44.8 mm (LED head) and 82 × 52 mm (control).
- Each zip has the 7 layers (copper, mask and silkscreen both sides, outline) and separate plated
  (`-PTH.drl`) and non-plated (`-NPTH.drl`, the four M3 holes) drill files. Smallest features:
  0.4 mm via drill, 0.35 mm annular ring, 0.2 mm clearance, silkscreen 1.0 mm text / 0.15 mm lines,
  all inside JLCPCB's standard 2-layer process.
- JLCPCB prints its order number on the silkscreen unless you choose "Remove Mark" (small fee) or
  "Specify a location"; neither board has a marked spot, so remove it or accept where they put it.

## Before ordering / building

1. **Run DRC in your own KiCad.** Expected: 0 errors, 0 unconnected, 0 warnings on both boards.
   DRC only checks rules, so `tools/build_all.sh` also runs `tools/check_layout.py`, which fails the
   build on legal-but-poor layout: pads within 1.5 mm of the board edge, a two-pin part more than
   12 mm from the rest of its net, and tracks more than 1.5 x (and 8 mm) longer than their pads'
   spread. The few deliberate exceptions are listed in the script, each with its reason.
2. **Check the LEDs before soldering 66 of them.** Confirm the SFH 4545's 2.54 mm lead pitch and
   that cathode = pin 1 (the flat side on the silkscreen) against a real part.
3. **Check the 2N3904 (Q6) pin order** against your part: the footprint is E-B-C (pads 1-2-3), as on onsemi's 2N3904BU.
   Fit it with its flat side matching the silkscreen.
4. **R6 is 47 Ω and GPIO17 stays at drive strength 2.** Together they slow Q2's switching edge on
   purpose: a much faster edge makes the ribbon ring and reverse-bias the LEDs past their 5 V rating at
   every carrier cycle (see `sim/`).
   Q2 and Q5 are DPAKs on the back: solder the tab with a hot iron (or hot air) before the leads.
5. **Crimp the AA holder's plug with red (+) on pin 1** (BATT). The board has no reverse-polarity protection:
   a reversed plug puts −5 V on the 470 µF capacitors.
6. **First power-up:**
   - Leave the LED head unplugged and put the cells in.
   - Check that VBAT (J5 pin 1) is 0 V, and only rises to the pack voltage, within about 30 ms, while
     ARM is high (press a key).
   - Then plug the head in and send a code.

## Regenerating

You need:

- **KiCad 7+:** the `pcbnew` Python module plus `kicad-cli`.
- **Python 3:** `cairosvg` and `Pillow` (for the previews), `reportlab` and `pypdf` (for the PDFs).
- **Freerouting, only to re-autoroute:** Java 17+ and [Freerouting](https://github.com/freerouting/freerouting/releases)
  2.1.0 saved as `tools/bin/freerouting.jar`.
- **3D renders only:** `cadquery` (OpenCascade) and `vtk`, plus KiCad's 3D models for the parts used.
  `tools/render_3d.py --fetch MODELS_DIR` downloads just those from KiCad's packages3D repository.

```sh
tools/build_all.sh            # rebuild everything, re-using the saved control-board route (*.ses)
tools/build_all.sh --reroute  # also re-run the autorouter (after moving parts)
tools/render_3d.py MODELS_DIR # 3D renders of both boards into previews/
```

The generators rebuild each board from scratch. Once you start editing in KiCad, treat the KiCad
files as the source of truth.
