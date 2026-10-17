#!/usr/bin/env python3
"""
build_db.py - Generate the universal IR code database for the Long-Range Remote firmware
(derived from the HifiRemote generator).

Sources (both open, community maintained):
  * Flipper-IRDB  https://github.com/Lucaslhm/Flipper-IRDB   (.ir files, parsed + raw)
  * IRDB          https://github.com/probonopd/irdb          (.csv files, IRP protocol/D/S/F)

Every device category is imported: TVs, AV receivers, soundbars, projectors, set-top / cable /
satellite boxes, streaming boxes, Blu-ray / DVD, VCRs, hi-fi (amps, CD, tuners, tape, MiniDisc),
consoles, monitors, fans / heaters / air purifiers, air conditioners, lighting, car audio...

Outputs:
  --bin   irdb.bin      : the database as one binary file. Copy it to the root of the SD card (or
                          upload it from the web page, Settings); the firmware loads it into PSRAM
                          at boot. Nothing is compiled into the firmware, so the codes can be
                          updated without reflashing.
  --keys  ir_keys.h     : key / category / protocol numbering, compiled into the firmware. Its
                          IRDB_SCHEMA hash is stored in irdb.bin too; the firmware refuses a file
                          with a different schema. It only changes when KEYS, CATS or the protocol
                          list change - then rebuild the firmware as well.
  --out   ir_db_data.h  : optional, the same image as a C array (not used by this firmware)

Every code is converted into the firmware's own protocol set (see ir_protocols.h):
    struct { uint8 proto; uint8 key; uint32 name; uint16 D; uint16 S; uint16 F; }
Protocol-family checksums (NEC inverses, Kaseikyo xor, ...) are resolved here
so the firmware only has to serialise bits.

Usage:
    git clone --depth 1 https://github.com/Lucaslhm/Flipper-IRDB.git
    git clone --depth 1 https://github.com/probonopd/irdb.git
    python3 tools/build_db.py --flipper Flipper-IRDB --irdb irdb --keys ir_keys.h --bin irdb.bin \
        --irpt-jar IrpTransmogrifier-1.2.14-jar-with-dependencies.jar --irpt-xml IrpProtocols.xml
    (omit --irdb for a database built only from the CC0-licensed Flipper-IRDB)
"""
import argparse
import csv
import os
import re
import struct
import sys
from collections import OrderedDict, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raw_decode  # noqa: E402  (decodes raw captures back to protocol codes)

# ---------------------------------------------------------------------------
# Firmware protocol IDs - MUST match enum IrProto in ir_protocols.h
# ---------------------------------------------------------------------------
PROTOS = [
    "RAW",        # 0  D = index into raw table
    "NEC",        # 1  D = addr16 (byte0 | byte1<<8), F = byte2 | byte3<<8, ditto repeats, 38.4k
    "NEC2",       # 2  as NEC but repeats whole frame
    "NECX",       # 3  Samsung/NECx: 4.5ms header, D/F as NEC, NECx1 repeat
    "PIONEER",    # 4  NEC2 framing at 40 kHz
    "AIWA",       # 5  NEC42: D = 13-bit address, F = 8-bit command
    "KASEIKYO",   # 6  48-bit: D = vendor16, S = byte2, F = byte3 | byte4<<8 ; byte5 = b2^b3^b4
    "SONY12",     # 7  D = 5-bit addr, F = 7-bit cmd
    "SONY15",     # 8  D = 8-bit addr
    "SONY20",     # 9  D = 13-bit addr (dev | subdev<<5)
    "RC5",        # 10 D = 5-bit system, F = 7-bit command (bit6 -> inverted field bit = RC5X)
    "RC5X",       # 11 Marantz/Philips 20-bit extended RC5: D system, S extension(7b), F cmd(6b)
    "RC6",        # 12 RC6 mode 0: D 8, F 8
    "DENON",      # 13 Denon/Sharp 15-bit, D 5, F 8, sends frame + inverted frame
    "DENON1",     # 14 Denon first frame only
    "DENON2",     # 15 Denon inverted frame only
    "SHARP",      # 16 Sharp 15-bit (expansion bits 1/2)
    "JVC",        # 17 D 8, F 8, header only on first frame
    "JVC2",       # 18 JVC frames without header
    "MITSUBISHI", # 19 D 8, F 8
    "RECS80",     # 20 D 3, F 6 (toggle)
    "BOSE",       # 21 F 8
    "RCA",        # 22 D 4, F 8 @ 58 kHz
    "RCA38",      # 23 RCA framing at 38.7 kHz
    "TEACK",      # 24 Teac-K (Kaseikyo-framed, own checksum) D 4, S 8, F 8
    "AKAI",       # 25 D 3, F 7
    "EMERSON",    # 26 D 6, F 6
    "RCA38OLD",   # 27 old RCA-38
]
P = {n: i for i, n in enumerate(PROTOS)}

# ---------------------------------------------------------------------------
# Canonical keys - MUST match ir_keys.h (generated below, keep order stable)
# (id name, short label for UI, regex on the "compact" button name)
# ---------------------------------------------------------------------------
KEYS = [
    ("NONE", "", None),
    ("POWER", "Power", r"(POWER|PWR|ONOFF|ONSTANDBY|STANDBY|STANDBYON|POWERTOGGLE|POWERSTANDBY|POWERONOFF|ONOFFSTANDBY|SYSTEMPOWER|MAINPOWER|POWERMAIN|SYSTEMONOFF|STBY)"),
    ("POWER_ON", "On", r"(POWERON|PWRON|ON|SYSTEMON|DISCRETEON|POWERONDISCRETE)"),
    ("POWER_OFF", "Off", r"(POWEROFF|PWROFF|OFF|SYSTEMOFF|DISCRETEOFF|POWEROFFDISCRETE|ALLOFF)"),
    ("VOL_UP", "Vol +", r"(MAIN|MASTER)?(VOL|VOLUME)(UP|PLUS|INC|INCREASE|UPP)|VUP"),
    ("VOL_DOWN", "Vol -", r"(MAIN|MASTER)?(VOL|VOLUME)(DOWN|DN|MINUS|DEC|DECREASE|DWN|DOWNN)|VDN|VDOWN"),
    ("MUTE", "Mute", r"(MUTE|MUTING|AUDIOMUTE|MUTEONOFF|MUTETOGGLE|MUTEON)"),
    ("INPUT", "Input", r"(INPUT|SOURCE|SRC|INPUTSELECT|INPUTSEL|FUNCTION|FUNC|INPUTSELECTOR|SOURCESELECT|INPUTNEXT|SOURCENEXT|INPUTUP|SOURCEUP|SELECTOR|INPUTPLUS|SOURCEPLUS|TVAV|AVTV|AV|TVVIDEO|TVVCR|EXTERNAL|EXT)"),
    ("PLAY", "Play", r"PLAY"),
    ("PAUSE", "Pause", r"PAUSE"),
    ("PLAY_PAUSE", "Play/Pause", r"(PLAYPAUSE|PLAYPA|PAUSEPLAY|PLAYANDPAUSE)"),
    ("STOP", "Stop", r"STOP"),
    ("PREV", "Prev", r"(PREV|PREVIOUS|SKIPBACK|SKIPREV|SKIPBACKWARD|SKIPPREV|TRACKPREV|PREVTRACK|SKIPDOWN|TRACKDOWN|BACKSKIP|SKIPMINUS|TRACKMINUS)"),
    ("NEXT", "Next", r"(NEXT|SKIPFORWARD|SKIPFWD|SKIPNEXT|SKIP|TRACKNEXT|NEXTTRACK|SKIPUP|TRACKUP|TRACKPLUS|SKIPPLUS)"),
    ("REW", "Rew", r"(REW|REWIND|REVERSE|SEARCHREV|SEARCHBACK|REVIEW|FR|SEARCHMINUS|RSRCH|RSCAN|SCANREV|SCANBACK|REVSCAN)"),
    ("FFWD", "FFwd", r"(FF|FWD|FASTFORWARD|FORWARD|FFWD|SEARCHFWD|SEARCHFORWARD|CUE|SEARCHPLUS|FSRCH|FSCAN|SCANFWD|SCANFORWARD|FWDSCAN)"),
    ("EJECT", "Eject", r"(EJECT|OPEN|OPENCLOSE)"),
    ("REPEAT", "Repeat", r"(REPEAT|RPT)"),
    ("SHUFFLE", "Shuffle", r"(SHUFFLE|RANDOM)"),
    ("UP", "Up", r"(CURSOR)?(UP|UPARROW|ARROWUP)"),
    ("DOWN", "Down", r"(CURSOR)?(DOWN|DN|DOWNARROW|ARROWDOWN|DNARROW|ARROWDN)"),
    ("LEFT", "Left", r"(CURSOR)?(LEFT|LEFTARROW|ARROWLEFT)"),
    ("RIGHT", "Right", r"(CURSOR)?(RIGHT|RIGHTARROW|ARROWRIGHT)"),
    ("OK", "OK", r"(OK|ENTER|ENT|SELECT|CURSORENTER|CURSOROK)"),
    ("MENU", "Menu", r"(MENU|SETUPMENU|MAINMENU)"),
    ("BACK", "Back", r"(BACK|RETURN|EXIT|CANCEL)"),
    ("HOME", "Home", r"HOME"),
    ("SETUP", "Setup", r"(SETUP|SETTINGS|OPTION|OPTIONS)"),
    ("INFO", "Info", r"(INFO|STATUS|OSD)"),
    ("DIMMER", "Dimmer", r"(DIMMER|DIM|DISPLAYDIMMER|DISPLAY|DISP)"),
    ("SLEEP", "Sleep", r"(SLEEP|SLEEPTIMER)"),
    ("SOUND_MODE", "Mode", r"(SURROUND|SURR|SOUNDMODE|MODE|DSP|SOUNDFIELD|SFIELD|PROGRAM|LISTENINGMODE|EQ|SOUND|SURROUNDMODE|AUDIOMODE|SMODE)"),
    ("BASS_UP", "Bass +", r"BASS(UP|PLUS)"),
    ("BASS_DOWN", "Bass -", r"BASS(DOWN|DN|MINUS)"),
    ("TREBLE_UP", "Treb +", r"TREBLE(UP|PLUS)"),
    ("TREBLE_DOWN", "Treb -", r"TREBLE(DOWN|DN|MINUS)"),
    ("SUB_UP", "Sub +", r"(SUB|SUBWOOFER|SW|WOOFER|SUBWOOFERLEVEL)(UP|PLUS)"),
    ("SUB_DOWN", "Sub -", r"(SUB|SUBWOOFER|SW|WOOFER|SUBWOOFERLEVEL)(DOWN|DN|MINUS)"),
    ("TUNE_UP", "Tune +", r"(TUNING|TUNE)(UP|PLUS)"),
    ("TUNE_DOWN", "Tune -", r"(TUNING|TUNE)(DOWN|DN|MINUS)"),
    ("PRESET_UP", "Pre +", r"PRESET(UP|PLUS|NEXT)"),
    ("PRESET_DOWN", "Pre -", r"PRESET(DOWN|DN|MINUS|PREV)"),
    ("IN_CD", "CD", r"(CD|CDINPUT|INPUTCD)"),
    ("IN_TUNER", "Tuner", r"(TUNER|RADIO|FM|FMAM|AMFM|TUNERINPUT)"),
    ("IN_PHONO", "Phono", r"(PHONO|TURNTABLE)"),
    ("IN_AUX", "Aux", r"(AUX|AUX1|AUXIN|LINE|LINEIN|ANALOG|ANALOGIN)"),
    ("IN_TV", "TV", r"(TV|TVAUDIO|TVSAT|TVCBL|ARC|HDMIARC|TVARC|TVEARC)"),
    ("IN_BD", "BD/DVD", r"(BD|BDDVD|DVD|BLURAY|DVDBD)"),
    ("IN_GAME", "Game", r"(GAME|GAMES|GAME1)"),
    ("IN_OPTICAL", "Optical", r"(OPTICAL|OPT|OPTICAL1|DIGITAL|DIGITALIN|TOSLINK)"),
    ("IN_COAX", "Coax", r"(COAX|COAXIAL|COAX1)"),
    ("IN_USB", "USB", r"USB"),
    ("IN_BT", "BT", r"(BT|BLUETOOTH)"),
    ("IN_NET", "Net", r"(NET|NETWORK|STREAMING|NETRADIO|WIFI|HEOS|MUSICCAST|SPOTIFY)"),
    ("IN_TAPE", "Tape", r"(TAPE|TAPE1|TAPEMON)"),
    ("IN_HDMI", "HDMI", r"(HDMI|HDMI1)(DVI|ARC|MHL|CEC)?"),
    # --- TV / video keys (Long-Range Remote) ---
    ("CH_UP", "CH +", r"(CH|CHAN|CHANNEL|PROG|PR|P)(UP|PLUS|NEXT|INC)|CHNEXT"),
    ("CH_DOWN", "CH -", r"(CH|CHAN|CHANNEL|PROG|PR|P)(DOWN|DN|MINUS|PREV|DEC)|CHPREV"),
    ("PREV_CH", "Last ch", r"(PREVCH|PRECH|PREVIOUSCHANNEL|LAST|LASTCH|LASTCHANNEL|RECALL|JUMP|FLASHBACK|QVIEW|SWAP|CHRETURN)"),
    ("GUIDE", "Guide", r"(GUIDE|EPG|TVGUIDE|PROGRAMGUIDE)"),
    ("LIST", "List", r"(LIST|CHLIST|CHANNELLIST)"),
    ("FAV", "Fav", r"(FAV|FAVS|FAVORITE|FAVOURITE|FAVORITES|FAVOURITES|FAVCH|FAVCHANNEL)"),
    ("RECORD", "Rec", r"(REC|RECORD)"),
    ("SUBTITLE", "Subtitle", r"(SUBTITLE|SUBTITLES|SUBT|CC|CAPTION|CAPTIONS|CLOSEDCAPTION)"),
    ("AUDIO_LANG", "Audio", r"(AUDIO|LANG|LANGUAGE|AUDIOLANG|AUDIOLANGUAGE|MTS|SAP|DUAL|STEREOSAP)"),
    ("TEXT", "Text", r"(TEXT|TELETEXT|TTX|TXT)"),
    ("RED", "Red", r"(RED|KEYRED|REDKEY)"),
    ("GREEN", "Green", r"(GREEN|KEYGREEN|GREENKEY)"),
    ("YELLOW", "Yellow", r"(YELLOW|KEYYELLOW|YELLOWKEY)"),
    ("BLUE", "Blue", r"(BLUE|KEYBLUE|BLUEKEY)"),
    ("APPS", "Apps", r"(APPS|APP|SMART|SMARTHUB|SMARTTV|MYAPPS|INTERNET|NETTV|VIERACAST|VIERACONNECT|PORTAL)"),
    ("NETFLIX", "Netflix", r"NETFLIX"),
    ("YOUTUBE", "YouTube", r"YOUTUBE"),
    ("ASPECT", "Aspect", r"(ASPECT|WIDE|PICTURESIZE|PSIZE|RATIO|FORMAT|SCREENSIZE|SCREENFORMAT|ARC169)"),
    ("PIC_MODE", "Picture", r"(PICTURE|PIC|PICTUREMODE|PMODE|VIDEOMODE|PICMODE|PSM)"),
    ("TOP_MENU", "Disc menu", r"(TOPMENU|TITLE|TITLEMENU|DISCMENU|POPUPMENU|POPUP)"),
    ("IN_HDMI2", "HDMI 2", r"HDMI2(DVI|ARC|MHL|CEC)?"),
    ("IN_HDMI3", "HDMI 3", r"HDMI3(DVI|ARC|MHL|CEC)?"),
    ("IN_HDMI4", "HDMI 4", r"HDMI4(DVI|ARC|MHL|CEC)?"),
    ("SPEED_UP", "Speed +", r"(SPEED|FAN|FANSPEED|WIND)(UP|PLUS)"),
    ("SPEED_DOWN", "Speed -", r"(SPEED|FAN|FANSPEED|WIND)(DOWN|DN|MINUS)"),
    ("TEMP_UP", "Temp +", r"(TEMP|TEMPERATURE)(UP|PLUS)"),
    ("TEMP_DOWN", "Temp -", r"(TEMP|TEMPERATURE)(DOWN|DN|MINUS)"),
    ("OSCILLATE", "Swing", r"(OSC|OSCILLATE|OSCILLATION|SWING|ROTATE|ROTATION)"),
    ("TIMER", "Timer", r"(TIMER|TIME)"),
] + [("D%d" % d, str(d), r"(NUM|NUMBER|DIGIT|NUMERIC)?%d" % d) for d in range(10)]

KEY_ID = {k[0]: i for i, k in enumerate(KEYS)}
KEY_RX = [(i, re.compile(r"^(KEY)?(" + k[2] + r")$")) for i, k in enumerate(KEYS) if k[2]]
ZONE_RX = re.compile(r"\b(ZONE|ZN|Z2|Z3|ZONE2|ZONE3|2ND|MULTI|DECK ?B|B DECK|REMOTE ?2|ROOM|REC OUT|RECOUT)\b")


def compact_name(name):
    """Normalise a button name: 'Vol +' -> 'VOLUP', 'KEY_VOLUMEDOWN' -> 'KEYVOLUMEDOWN'."""
    s = name.upper().strip()
    s = s.replace("−", "-")
    s = re.sub(r"\s*\+\s*$", " UP", s)          # trailing "+" means up
    s = re.sub(r"(?<=[A-Z0-9 ])\s*-\s*$", " DOWN", s)   # trailing "-" means down
    s = re.sub(r"\s*\+\s*$", "", s)
    return re.sub(r"[^A-Z0-9]", "", s), re.sub(r"[^A-Z0-9]+", " ", s).strip()


def canon_key(name):
    c, spaced = compact_name(name)
    if not c or ZONE_RX.search(spaced):
        return 0
    for i, rx in KEY_RX:
        if rx.match(c):
            return i
    # "POWER ON ASSERT" style discrete codes, "INPUT CD" / "SOURCE TUNER" style input selects
    c2 = re.sub(r"(ASSERT|DISCRETE)$", "", c)
    c2 = re.sub(r"^(INPUT|SOURCE|SRC|FUNC|FUNCTION|SELECT|SEL)(?=[A-Z0-9]{2,})", "", c2)
    if c2 != c:
        for i, rx in KEY_RX:
            if rx.match(c2):
                return i
    return 0


# ---------------------------------------------------------------------------
# Brand / category normalisation
# ---------------------------------------------------------------------------
BRAND_FIX = {
    "harmankardon": "Harman Kardon", "cambridgeaudio": "Cambridge Audio", "bowersandwilkins": "Bowers & Wilkins",
    "bangolufsen": "Bang & Olufsen", "bang olufsen": "Bang & Olufsen", "qacoustics": "Q Acoustics",
    "fosiaudio": "Fosi Audio", "fossiaudio": "Fosi Audio", "mcmcustomaudio": "MCM Custom Audio",
    "dollartecbt5ampboard": "DollarTec", "polkaudio": "Polk Audio", "dutchoriginals": "Dutch Originals",
    "genericunknownbrand": "Generic", "brandunknown": "Generic", "unknown": "Generic", "kantoyu": "Kanto",
    "doukaudio": "Douk Audio", "comoaudio": "Como Audio", "ibizasound": "Ibiza Sound",
    "jbl": "JBL", "jvc": "JVC", "nad": "NAD", "kef": "KEF", "lg": "LG", "tcl": "TCL", "rca": "RCA",
    "maranz": "Marantz", "harmanhardon": "Harman Kardon", "classe": "Classe Audio", "classeaudio": "Classe Audio",
    "bnkcomponents": "B&K Components", "bkcomponents": "B&K Components", "sirius": "Sirius XM",
    "advancedacoustics": "Advance Acoustic", "advanceacoustic": "Advance Acoustic", "goldstar": "GoldStar",
    "fosgateaudionics": "Fosgate", "onkyointegra": "Integra", "alpine": "Alpine", "audiosonic": "Audiosonic",
    "universum": "Universum", "mediatech": "Media-Tech", "audioaccess": "Audio Access", "audiocontrol": "AudioControl",
    "thetacasablanca": "Theta Digital", "mission": "Mission", "bush": "Bush",
    "teac": "TEAC", "scientificatlanta": "Scientific Atlanta", "generalinstruments": "General Instrument",
    "generalinstrument": "General Instrument", "ilive": "iLive", "siriusxm": "Sirius XM", "taotronics": "TaoTronics", "smsl": "SMSL", "asr": "ASR", "gpx": "GPX", "aeg": "AEG", "qnap": "QNAP",
}

# Brands that make only video / set-top / projector gear. The universal build imports them all; the
# set is still used to classify their ambiguous "Receiver" / "Tuner" folders as set-top boxes.
NON_HIFI_BRANDS = {
    # projectors / video processors / conferencing / lighting / HDMI switching (seen in the Converted library)
    "epson", "optoma", "infocus", "infocus systems", "runco", "vidikron", "barco", "picturetel", "polycom",
    "viewsonic", "umax", "msi", "packard bell", "lutron", "litetouch", "cable matters", "ligawo", "nview",
    "hermstedt", "lift-tech", "sima", "faroudja", "thule", "dyno jet", "electrohome", "seleco", "videologic",
    "crestron", "colorado vnet", "intellinet controls", "promethean", "khl", "xms", "terris", "novaplex",
    "directv", "dish network", "dishnetwork", "hughes", "scientific atlanta", "scientificatlanta", "motorola",
    "humax", "foxtel", "tivo", "comcast", "sky", "echostar", "general instrument", "generalinstrument",
    "pace", "cisco", "arris", "dreambox", "digital stream", "digitalstream", "zenith", "gi", "jerrold",
    "hauppauge", "microsoft", "apple", "roku", "sagem", "thomson", "technisat", "topfield", "vu+",
    "xbox", "nvidia", "tivo", "voom", "starchoice", "bell expressvu", "expressvu", "ultimate tv",
}

# irdb device-type folder -> UI category: hi-fi folders by name, everything else by keyword
# (irdb_category below). Nothing is skipped.
IRDB_TYPES = {
    "Receiver": "AVR", "Surround Receiver": "AVR", "Multi-Zone Receiver": "AVR", "6 Zone Receiver": "AVR",
    "Surround Processor": "AVR", "Processor": "AVR", "DSP": "AVR", "Unknown_receiver": "AVR",
    "Unknown_yamaha-amp": "Amp", "Unknown_harmankardon": "AVR", "Unknown_hifi": "Audio", "Unknown_Audio": "Audio",
    "Amplifier": "Amp", "Integrated Amplifier": "Amp", "Pre-Amplifier": "Amp", "Volume Control": "Amp",
    "Subwoofer": "Amp", "Zone Controller": "Amp", "Distributed Audio": "Amp", "audio": "Audio",
    "CD Player": "CD", "CD Jukebox": "CD", "CD Changer": "CD", "CD-R": "CD", "Unknown_CD": "CD", "Unknown_cd": "CD",
    "Tuner": "Tuner", "Satellite Radio": "Tuner",
    "Cassette Tape": "Tape", "Tape Deck": "Tape", "DAT": "Tape",
    "Mini-Disc": "MD", "Mini System": "System", "Music System": "System", "Music Center": "System",
    "Lifestyle": "System", "System": "System", "3-2-1": "System", "Unknown_Wave": "System",
    "Unknown_WAVERADIO": "System", "Music Server": "Audio", "Unknown_boombox": "System",
}
FLIPPER_DIRS = {
    "TVs": "TV", "Universal_TV_Remotes": "TV", "Audio_and_Video_Receivers": "AVR", "SoundBars": "Soundbar",
    "Speakers": "Speaker", "CD_Players": "CD", "MiniDisc": "MD", "Projectors": "Projector",
    "Cable_Boxes": "SetTop", "DVB-T": "SetTop", "TV_Tuner": "SetTop", "Streaming_Devices": "Streaming",
    "Blu-Ray": "Disc", "DVD_Players": "Disc", "Laserdisc": "Disc", "VCR": "VCR", "Consoles": "Console",
    "Monitors": "Monitor", "Touchscreen_Displays": "Monitor", "Digital_Signs": "Monitor",
    "Fans": "Climate", "Heaters": "Climate", "Air_Purifiers": "Climate", "Humidifiers": "Climate",
    "Fireplaces": "Climate", "ACs": "AC", "LED_Lighting": "Light", "Car_Multimedia": "Car", "Head_Units": "Car",
    "Cameras": "Camera", "CCTV": "Camera",
}
FLIPPER_SKIP_DIRS = {"_Converted_"}            # loaded separately (load_converted)
CATS = ["TV", "AVR", "Soundbar", "Projector", "SetTop", "Streaming", "Disc", "VCR", "Amp", "CD", "Tuner",
        "Tape", "MD", "System", "Speaker", "Audio", "Console", "Monitor", "Climate", "AC", "Light", "Car",
        "Camera", "Other"]


def irdb_category(typ):
    """irdb folder name -> category, or None when only the buttons can tell (Unknown_* folders)."""
    if typ in IRDB_TYPES:
        return IRDB_TYPES[typ]
    t = typ.lower().replace("unknown_", "")
    rules = [
        (r"air ?con|a/?c\b|aircon|climate|heat|fan\b|fans|purif|humidif", "Climate"),
        (r"light|lamp|dimmer", "Light"),
        (r"projector|dlp|beamer|\bvpl", "Projector"),
        (r"blu.?ray|\bbd\b|bdp|dvd|laser ?disc|\bld\b|disc", "Disc"),
        (r"vcr|vhs|video ?cassette|s-vhs|betamax|\bvtr", "VCR"),
        (r"sat|cable|dss|dvr|pvr|tivo|dvb|set ?top|stb|hdtv tuner|digital recorder|iptv|freeview|foxtel", "SetTop"),
        (r"roku|apple ?tv|fire ?tv|stream|media player|chromecast|shield", "Streaming"),
        (r"xbox|playstation|ps[2-5]|console|game", "Console"),
        (r"monitor|display|signage", "Monitor"),
        (r"\btv|television|plasma|lcd|led tv|oled|hdtv", "TV"),
        (r"karaoke|ipod|mp3|jukebox|radio", "Audio"),
        (r"car|head ?unit", "Car"),
        (r"camera|camcorder|cctv", "Camera"),
    ]
    for rx, cat in rules:
        if re.search(rx, t):
            return cat
    return None


def strip_brand(name, brand):
    """'Denon AVR-X1600H' -> 'AVR-X1600H' (the brand is shown separately)."""
    for b in (brand, brand.replace(" ", ""), brand.replace(" ", "_")):
        if name.lower().startswith(b.lower()) and len(name) > len(b) + 1:
            rest = name[len(b):].lstrip(" _-")
            if rest:
                return rest
    return name


def brand_key(b):
    return re.sub(r"[^a-z0-9]", "", b.lower())


def pretty_brand(raw):
    k = brand_key(raw)
    if k in BRAND_FIX:
        return BRAND_FIX[k]
    return raw.replace("_", " ").strip()


# ---------------------------------------------------------------------------
# Code conversion helpers
# ---------------------------------------------------------------------------
def inv8(x):
    return (~x) & 0xFF


def nec_code(proto, d, s, f, e=None):
    if s is None or s < 0:
        s = inv8(d)
    if e is None:
        e = inv8(f)
    return (P[proto], (d & 0xFF) | ((s & 0xFF) << 8), 0, (f & 0xFF) | ((e & 0xFF) << 8))


def kaseikyo_code(vendor, b2, b3, b4):
    return (P["KASEIKYO"], vendor & 0xFFFF, b2 & 0xFF, (b3 & 0xFF) | ((b4 & 0xFF) << 8))


def irdb_convert(proto, d, s, f):
    """Convert an irdb (IRP) protocol + D/S/F to firmware (proto, D, S, F) or None."""
    p = proto.strip()
    pl = p.lower()
    if pl in ("nec1", "nec", "nec1-f16", "nec-f16"):
        return nec_code("NEC", d, s, f)
    if pl in ("nec2", "nec2-f16"):
        return nec_code("NEC2", d, s, f)
    if pl in ("nec1-y1", "nec1-y2", "nec1-y3"):
        mask = {"nec1-y1": 0x7F, "nec1-y2": 0xFE, "nec1-y3": 0x7E}[pl]
        return nec_code("NEC", d, s, f, (f ^ mask) & 0xFF)
    if pl in ("necx1", "necx2"):
        return nec_code("NECX", d, s, f)
    if pl == "pioneer":
        return nec_code("PIONEER", d, s, f)
    if pl == "aiwa":
        s = 0 if s < 0 else s
        return (P["AIWA"], (d & 0xFF) | ((s & 0x1F) << 8), 0, f & 0xFF)
    if pl == "panasonic":
        return kaseikyo_code(0x2002, d, s if s >= 0 else 0, f)
    if pl == "denon-k":
        s = 0 if s < 0 else s
        return kaseikyo_code(0x3254, (d & 0xF) << 4, (s & 0xF) | ((f & 0xF) << 4), (f >> 4) & 0xFF)
    if pl == "jvc-48":
        return kaseikyo_code(0x0103, d, s if s >= 0 else 0, f)
    if pl == "teac-k":
        return (P["TEACK"], d & 0xF, (s if s >= 0 else 0) & 0xFF, f & 0xFF)
    if pl == "sony12":
        return (P["SONY12"], d & 0x1F, 0, f & 0x7F)
    if pl == "sony15":
        return (P["SONY15"], d & 0xFF, 0, f & 0x7F)
    if pl == "sony20":
        return (P["SONY20"], (d & 0x1F) | (((s if s >= 0 else 0) & 0xFF) << 5), 0, f & 0x7F)
    if pl == "rc5":
        return (P["RC5"], d & 0x1F, 0, f & 0x7F)
    if pl == "rc5x":
        return (P["RC5X"], d & 0x1F, (s if s >= 0 else 0) & 0x7F, f & 0x3F)
    if pl == "rc6":
        return (P["RC6"], d & 0xFF, 0, f & 0xFF)
    if pl in ("denon", "denon{1}", "denon{2}", "sharp"):
        name = {"denon": "DENON", "denon{1}": "DENON1", "denon{2}": "DENON2", "sharp": "SHARP"}[pl]
        return (P[name], d & 0x1F, 0, f & 0xFF)
    if pl == "jvc":
        return (P["JVC"], d & 0xFF, 0, f & 0xFF)
    if pl == "jvc{2}":
        return (P["JVC2"], d & 0xFF, 0, f & 0xFF)
    if pl == "mitsubishi":
        return (P["MITSUBISHI"], d & 0xFF, 0, f & 0xFF)
    if pl == "recs80":
        return (P["RECS80"], d & 0x7, 0, f & 0x3F)
    if pl == "bose":
        return (P["BOSE"], 0, 0, f & 0xFF)
    if pl == "rca":
        return (P["RCA"], d & 0xF, 0, f & 0xFF)
    if pl == "rca-38":
        return (P["RCA38"], d & 0xF, 0, f & 0xFF)
    if pl == "rca-38(old)":
        return (P["RCA38OLD"], d & 0xF, 0, f & 0xFF)
    if pl == "akai":
        return (P["AKAI"], d & 0x7, 0, f & 0x7F)
    if pl == "emerson":
        return (P["EMERSON"], d & 0x3F, 0, f & 0x3F)
    return None


def le_hex(s):
    b = [int(x, 16) for x in s.split()]
    return sum(v << (8 * i) for i, v in enumerate(b))


def flipper_convert(proto, addr, cmd):
    if proto == "NEC":
        a, c = addr & 0xFF, cmd & 0xFF
        return (P["NEC"], a | (inv8(a) << 8), 0, c | (inv8(c) << 8))
    if proto == "NECext":
        return (P["NEC"], addr & 0xFFFF, 0, cmd & 0xFFFF)
    if proto == "NEC42":
        return (P["AIWA"], addr & 0x1FFF, 0, cmd & 0xFF)
    if proto == "Samsung32":
        a, c = addr & 0xFF, cmd & 0xFF
        return (P["NECX"], a | (a << 8), 0, c | (inv8(c) << 8))
    if proto == "Pioneer":
        a, c = addr & 0xFF, cmd & 0xFF
        return (P["PIONEER"], a | (inv8(a) << 8), 0, c | (inv8(c) << 8))
    if proto == "Kaseikyo":
        ident = (addr >> 24) & 3
        vendor = (addr >> 8) & 0xFFFF
        g1, g2 = (addr >> 4) & 0xF, addr & 0xF
        vp = (vendor & 0xFF) ^ (vendor >> 8)
        vp = (vp & 0xF) ^ (vp >> 4)
        b2 = (vp & 0xF) | (g1 << 4)
        b3 = g2 | ((cmd & 0xF) << 4)
        b4 = ((ident << 6) | (cmd >> 4)) & 0xFF
        return kaseikyo_code(vendor, b2, b3, b4)
    if proto == "RC5":
        return (P["RC5"], addr & 0x1F, 0, cmd & 0x3F)
    if proto == "RC5X":
        return (P["RC5"], addr & 0x1F, 0, (cmd & 0x3F) | 0x40)
    if proto == "RC6":
        return (P["RC6"], addr & 0xFF, 0, cmd & 0xFF)
    if proto == "SIRC":
        return (P["SONY12"], addr & 0x1F, 0, cmd & 0x7F)
    if proto == "SIRC15":
        return (P["SONY15"], addr & 0xFF, 0, cmd & 0x7F)
    if proto == "SIRC20":
        return (P["SONY20"], addr & 0x1FFF, 0, cmd & 0x7F)
    if proto == "RCA":
        return (P["RCA"], addr & 0xF, 0, cmd & 0xFF)
    return None


def parse_flipper(path):
    """Yield dicts for every signal in a Flipper .ir file."""
    cur = {}
    with open(path, encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("#") or not line:
                if cur.get("name"):
                    yield cur
                cur = {}
                continue
            if ":" not in line:
                continue
            k, v = line.split(":", 1)
            k, v = k.strip(), v.strip()
            if k == "name" and cur.get("name"):
                yield cur
                cur = {}
            if k in ("Filetype", "Version"):
                continue
            cur[k] = v
    if cur.get("name"):
        yield cur


# ---------------------------------------------------------------------------
# Main build
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Hi-fi classifier for sources without a device category (IRDB "Unknown_<remote>"
# folders, Flipper-IRDB "_Converted_" library). Decides from button names + model name.
# ---------------------------------------------------------------------------
AUDIO_WORDS = re.compile(
    r"\b(TUNER|TUNING|PHONO|TAPE\w*|CASSETTE\w*|DECK\w*|CD|CDR|MD|DAT|SURR\w*|SPEAKERS?|SPK\w*|BASS\w*|"
    r"TREBLE\w*|LOUDNESS|BALANCE\w*|TONE\w*|DSP|SOUNDFIELD|LISTEN\w*|STEREO|MONO|FM|AM|FMAM|BAND|"
    r"PRESET\w*|AUX\w*|PURE|DIMMER|DISC\w*|INTRO|TRACK\w*|TRK|AMP|AMPLIFIER|"
    r"SUBWOOFER|CENTER|REAR|EQ|ACR\d?|RDS)\b")
VIDEO_WORDS = re.compile(
    r"\b(CH|CHAN\w*|EPG|GUIDE|TELETEXT|TXT|TTX|SUBTITLES?|SUB T|ANGLE|ZOOM|ASPECT|PIP|TITLE|CHAPTER|FAV\w*|"
    r"TIMESHIFT|NETFLIX|YOUTUBE|PICTURE|PIC|PR|PROG|RECORDINGS|LIVE|REVEAL|HOLD|TEXT|TV GUIDE|INFO BAR|"
    r"VIDEO MODE|HDMI CEC|SMART|APPS?|RED|GREEN|YELLOW|BLUE|KEYSTONE|FREEZE|BLANK|LAMP|FOCUS|"
    r"COMPUTER|PC|VGA|RGB|PATTERN|PROJECTOR|AV MUTE|SHUTTER|CAMERA|PRESENTATION)\b")
VOLUME_WORDS = re.compile(r"\b(VOL\w*|VOLUME\w*|MUTE|MUTING)\b")
MODEL_NEG = re.compile(
    r"(^|[^A-Z])(DVD|DVP|BDP|BD|BLU|BLURAY|TV|TVS|LCD|LED|PLASMA|VCR|VHS|SLV|SAT|STB|DVR|PVR|PROJ\w*|VPL|CAM\w*|"
    r"HDD|PS3|PS4|XBOX|ROKU|CABLE|DTV|MONITOR|KDL|KV|BRAVIA|LD|LASERDISC|SVO|BVW|DSR|HDR|DECODER|SET.?TOP)([^A-Z]|$)")
MODEL_POS = re.compile(
    r"(RECEIVER|AMP|AMPLIFIER|TUNER|CD|TAPE|DECK|HIFI|HI.FI|STEREO|AUDIO|AVR|AVC|STR|PMA|DRA|DCD|CDP|"
    r"MINI|MICRO|SPEAKER|SOUNDBAR|SUBWOOFER|PREAMP|INTEGRATED)")


def _norm_words(n):
    return re.sub(r"[^A-Z0-9]+", " ", n.upper()).strip()


# video gear identified by model-number prefix (Panasonic PT projectors, PV/NV VCRs, Pioneer CLD
# LaserDisc, DMR/DMP recorders, TH/TC-P/TX-P/TX-L TVs) or by function words
MODEL_NEG_PREFIX = re.compile(r"^(AW ?HE|AW ?UE|PT ?L|PT ?AE|PT ?AX|PV ?\d|NV ?\d|CLD|DVL|DMR|DMP|TH ?\d|TC ?P|TX ?P|TX ?L|VP ?\d)")
MODEL_NEG_WORDS = re.compile(r"\b(DOUBLER|SCALER|VIDEO PROCESSOR|VIDEO SWITCH\w*|CAMERA|CAMCORDER)\b")


def looks_hifi(names, model, hifi_brand):
    """True if a remote (button names + model/file name) looks like hi-fi gear."""
    a = v = vol = 0
    for n in names:
        w = _norm_words(n)
        a += bool(AUDIO_WORDS.search(w))
        v += bool(VIDEO_WORDS.search(w))
        vol += bool(VOLUME_WORDS.search(w))
    m = _norm_words(model)
    if MODEL_NEG_PREFIX.search(m) or MODEL_NEG_WORDS.search(m):
        return False
    if MODEL_NEG.search(m) and not (a >= 6 and a > 3 * v):
        return False
    if a >= 3 and a >= 2 * v:
        return True
    if MODEL_POS.search(m) and a >= 1 and v <= 1:
        return True
    # small hi-fi remotes (e.g. Power / CD / Vol+ / Vol-) from brands we already know as hi-fi
    return hifi_brand and v == 0 and a >= 1 and vol >= 1


def guess_category(names, model):
    words = [_norm_words(n) for n in names]
    text = " ".join(words) + " " + _norm_words(model)
    has = lambda rx: re.search(rx, text) is not None
    count = lambda rx: sum(1 for w in words if re.search(rx, w))
    if has(r"\b(SURR\w*|DSP|DOLBY|DTS|AVR|AVC|RECEIVER|ZONE ?2|STR|RX V|TX NR|TX SR|VSX)\b"):
        return "AVR"
    tape = count(r"\b(CASSETTE\w*|DECK\w*|TAPE\w*|DOLBY B|DOLBY C)\b")
    cd = count(r"\b(CD|CDP|DISC\w*|TRK|TRACK\w*|INTRO|SKIP\w*)\b") + 3 * bool(re.search(r"\b(CD|CDP|CDX)", _norm_words(model)))
    tuner = has(r"\b(TUNER|FM|PRESET\w*|TUNING)\b")
    amp = has(r"\b(PHONO|AUX\w*|LINE)\b") or (tuner and cd)
    vol = has(r"\b(VOL\w*|VOLUME)\b")
    if vol and (amp or tuner):
        return "Amp"
    if tape > cd and tape >= 2:
        return "Tape"
    if cd >= 2:
        return "CD"
    if tuner:
        return "Tuner"
    if has(r"\b(MD|MINIDISC)\b"):
        return "MD"
    return "Audio"


TV_WORDS = re.compile(r"\b(CH|CHAN\w*|CHANNEL\w*|TELETEXT|TTX|TXT|TEXT|PIP|PRE ?CH|LAST|GUIDE|EPG|TV|"
                      r"NETFLIX|YOUTUBE|SMART\w*|APPS?|ASPECT|PICTURE|PIC|SLEEP|RED|GREEN|YELLOW|BLUE)\b")
DISC_WORDS = re.compile(r"\b(TITLE|CHAPTER|SUBTITLES?|ANGLE|EJECT|OPEN|DISC ?MENU|TOP ?MENU|POP ?UP|A ?B|SLOW|"
                        r"STEP|SKIP\w*|RESUME|DVD|BD)\b")
SETTOP_WORDS = re.compile(r"\b(GUIDE|EPG|RECORD|REC|LIVE|DVR|PVR|RECORDINGS|LIST|ON ?DEMAND|VOD|"
                          r"PAGE ?(UP|DOWN|\+|-)|DAY ?(\+|-)|INTERACTIVE|SKY|BOX ?OFFICE)\b")
PROJ_WORDS = re.compile(r"\b(KEYSTONE|LAMP|FOCUS|ZOOM|LENS|FREEZE|BLANK|AV ?MUTE|SHUTTER|COMPUTER|VGA|PC|PATTERN)\b")
CLIMATE_WORDS = re.compile(r"\b(TEMP\w*|FAN|SPEED|SWING|OSC\w*|COOL|HEAT|DRY|TURBO|ECO|WIND|MODE ?FAN)\b")
LIGHT_WORDS = re.compile(r"\b(BRIGHT\w*|DIM\w*|WARM|COLD|RGB|FLASH|STROBE|FADE|SMOOTH|WHITE|COLOR|COLOUR)\b")


SETTOP_BRANDS = {brand_key(x) for x in (
    "humax", "scientific atlanta", "scientific-atlanta", "pace", "cisco", "foxtel", "tivo", "directv", "dish network",
    "motorola", "arris", "general instrument", "general instruments", "telesystem", "opticum", "technisat", "topfield",
    "beyonwiz", "cis box", "sky", "virgin media", "comcast", "xfinity", "kaon", "fetch", "telstra", "dreambox",
    "vu+", "kathrein", "echostar", "hughes", "jerrold", "strong", "amino", "canalsat", "freesat", "freeview",
    "2wire", "accessmedia", "u-verse", "uverse", "verizon", "fios", "cox", "charter", "spectrum", "time warner",
    "rogers", "shaw", "videotron", "bell", "sagemcom", "zinwell", "sky q", "now tv", "youview", "starhub", "astro")}


def settop_override(cat, brand):
    """A remote guessed as a TV from a set-top-box maker is almost always the box's own remote."""
    return "SetTop" if cat == "TV" and brand_key(brand) in SETTOP_BRANDS else cat


def guess_universal(names, model, hifi_brand=False):
    """Category for a remote with no device type: hi-fi rules first, then video / appliance words."""
    if looks_hifi(names, model, hifi_brand):
        return guess_category(names, model)
    words = [_norm_words(n) for n in names]
    m = _norm_words(model)
    count = lambda rx: sum(1 for w in words if rx.search(w))
    scores = {
        "TV": count(TV_WORDS) + 4 * bool(re.search(r"(^|[^A-Z])(TV|LCD|LED|OLED|PLASMA|KDL|BRAVIA|UE|UN|LN|TX ?L|TC ?P)([^A-Z]|$)", m)),
        "Disc": count(DISC_WORDS) + 4 * bool(re.search(r"(^|[^A-Z])(DVD|DVP|BDP|BD|BLU|BLURAY)([^A-Z]|$)", m)),
        "SetTop": count(SETTOP_WORDS) + 4 * bool(re.search(r"(^|[^A-Z])(SAT|STB|DVR|PVR|CABLE|DTV|DECODER)([^A-Z]|$)", m)),
        "Projector": count(PROJ_WORDS) + 4 * bool(re.search(r"(^|[^A-Z])(PROJ\w*|VPL|PT ?L|PT ?AE|EH|EB)([^A-Z]|$)", m)),
        "VCR": 4 * bool(re.search(r"(^|[^A-Z])(VCR|VHS|SLV|PV|NV|HR)([^A-Z]|$)", m)),
        "Climate": count(CLIMATE_WORDS),
        "Light": count(LIGHT_WORDS),
    }
    best = max(scores.items(), key=lambda x: x[1])
    return best[0] if best[1] >= 2 else "Other"


def signal_key(code, db):
    """Identity of a transmitted signal (for de-duplication). NEC1/NEC2 send the same frame."""
    pr, d, s, f = code
    if PROTOS[pr] == "RAW":
        return ("RAW", db.raw[d][0], tuple(db.raw[d][2]))
    if PROTOS[pr] == "NEC2":
        pr = P["NEC"]
    return (pr, d, s, f)


class DB:
    def __init__(self):
        self.brands = OrderedDict()   # key -> {"name":..., "profiles":[...]}
        self.raw = []                 # (freq, duty%, [durations])
        self.skipped = defaultdict(int)
        self.proto_count = defaultdict(int)
        self.used_irdb = False
        self.raw_decoded = 0
        self.raw_samples = 0
        self.raw_base = 0      # raw samples used before the _Converted_ library is loaded
        self.dropped_dupes = 0
        self.irp_render = None   # (java, jar, class dir) when IrpTransmogrifier rendering is enabled

    def raw_code(self, data, freq, duty, allow_raw=True):
        """Decode a raw capture to a protocol code if possible, else store it raw.
        Returns a firmware code tuple, or None (undecodable and allow_raw=False)."""
        r = raw_decode.decode(data, freq)
        if r:
            self.raw_decoded += 1
            name, d, s, f = r
            return (P[name], d, s, f)
        if not allow_raw:
            return None
        if len(data) % 2:
            data = data + [0]
        self.raw.append((freq, int(round(duty * 100)), data))
        self.raw_samples += len(data)
        return (P["RAW"], len(self.raw) - 1, 0, 0)

    def brand(self, raw_name):
        k = brand_key(pretty_brand(raw_name))   # merge spellings from both sources
        if k not in self.brands:
            self.brands[k] = {"name": pretty_brand(raw_name), "profiles": []}
        return self.brands[k]

    def add_profile(self, brand_raw, name, cat, src, codes):
        # de-duplicate identical (name, code) pairs, keep order
        seen, out = set(), []
        for c in codes:
            sig = (c["name"].upper(), c["code"])
            if sig in seen:
                continue
            seen.add(sig)
            out.append(c)
        if not out:
            return
        # canonical keys: first code wins a key slot, duplicates keep key 0
        used = set()
        for c in out:
            k = canon_key(c["name"])
            if k and k not in used:
                used.add(k)
                c["key"] = k
            else:
                c["key"] = 0
        self.brand(brand_raw)["profiles"].append({"name": name, "cat": cat, "src": src, "codes": out})


def load_flipper(db, root):
    dirs = sorted(d for d in os.listdir(root)
                  if os.path.isdir(os.path.join(root, d)) and not d.startswith(".") and d not in FLIPPER_SKIP_DIRS)
    for d in dirs:
        cat = FLIPPER_DIRS.get(d, "Other")
        base = os.path.join(root, d)
        for dirpath, _, files in os.walk(base):
            for fn in sorted(files):
                if not fn.lower().endswith(".ir"):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, fn), base).split(os.sep)
                brand = rel[0] if len(rel) > 1 else "Generic"
                codes = []
                for sig in parse_flipper(os.path.join(dirpath, fn)):
                    name = sig.get("name", "").strip()[:31]
                    if sig.get("type") == "parsed":
                        try:
                            conv = flipper_convert(sig["protocol"], le_hex(sig["address"]), le_hex(sig["command"]))
                        except (KeyError, ValueError):
                            conv = None
                        if conv is None:
                            db.skipped["flipper:" + sig.get("protocol", "?")] += 1
                            continue
                        codes.append({"name": name, "code": conv})
                        db.proto_count[PROTOS[conv[0]]] += 1
                    elif sig.get("type") == "raw":
                        try:
                            data = [abs(int(x)) for x in sig["data"].split()]
                            freq = int(float(sig.get("frequency", "38000")))
                            duty = float(sig.get("duty_cycle", "0.33"))
                        except (KeyError, ValueError):
                            db.skipped["flipper:raw-bad"] += 1
                            continue
                        if len(data) < 4 or len(data) > 2000:
                            db.skipped["flipper:raw-size"] += 1
                            continue
                        code = db.raw_code(data, freq, duty)
                        codes.append({"name": name, "code": code})
                        db.proto_count[PROTOS[code[0]]] += 1
                pname = os.path.splitext(fn)[0].replace("_", " ")
                if len(rel) > 2:
                    pname = " ".join(x.replace("_", " ") for x in rel[1:-1]) + " " + pname
                pname = strip_brand(pname, pretty_brand(brand))
                db.add_profile(brand, pname[:47], cat, "F", codes)


def load_irdb(db, root):
    db.used_irdb = True
    base = os.path.join(root, "codes")
    non_hifi = {brand_key(x) for x in NON_HIFI_BRANDS}
    for brand in sorted(os.listdir(base)):
        bdir = os.path.join(base, brand)
        if not os.path.isdir(bdir):
            continue
        types = sorted(os.listdir(bdir))
        hifi_brand = any(t in IRDB_TYPES for t in types) or brand_key(pretty_brand(brand)) in db.brands
        video_brand = brand_key(brand) in non_hifi
        for typ in types:
            cat = irdb_category(typ)
            if video_brand and cat in ("AVR", "Tuner"):      # DirecTV "Receiver", Foxtel "Tuner" ...
                cat = "SetTop"
            tdir = os.path.join(bdir, typ)
            # folders that don't name a device type: classify them by their buttons
            classify = cat is None
            if not os.path.isdir(tdir):
                continue
            for fn in sorted(os.listdir(tdir)):
                if not fn.endswith(".csv"):
                    continue
                with open(os.path.join(tdir, fn), encoding="utf-8", errors="ignore") as fh:
                    rows = list(csv.DictReader(fh))
                fcat = cat
                if classify:
                    names = [(r.get("functionname") or "") for r in rows]
                    fcat = settop_override(guess_universal(names, typ.replace("Unknown_", ""), hifi_brand and not video_brand), brand)
                codes, protos = [], set()
                for row in rows:
                    try:
                        d, s, f = int(row["device"]), int(row["subdevice"]), int(row["function"])
                    except (KeyError, ValueError, TypeError):
                        db.skipped["irdb:bad-row"] += 1
                        continue
                    conv = irdb_convert(row["protocol"], d, s, f)
                    if conv is None and db.irp_render:
                        conv = ("IRP", row["protocol"].strip(), d, s, f)   # rendered to raw later
                    if conv is None:
                        db.skipped["irdb:" + row["protocol"]] += 1
                        continue
                    name = (row.get("functionname") or "").strip()[:31] or ("F%d" % f)
                    codes.append({"name": name, "code": conv})
                    protos.add(row["protocol"])
                    if conv[0] != "IRP":
                        db.proto_count[PROTOS[conv[0]]] += 1
                if not codes:
                    continue
                if typ.startswith("Unknown_"):     # the folder name is the remote's model number
                    pname = strip_brand(typ[8:].replace("_", " "), pretty_brand(brand))
                else:
                    dev = os.path.splitext(fn)[0].split(",")
                    ident = dev[0] + ("." + dev[1] if len(dev) > 1 and dev[1] != "-1" else "")
                    bases = []
                    for pr in sorted(protos, key=str.lower):
                        pbase = re.sub(r"\{.*?\}", "", pr)
                        if pbase.lower() not in [x.lower() for x in bases]:
                            bases.append(pbase)
                    pname = "%s [%s %s]" % (typ.replace("Unknown_", ""), "/".join(bases)[:16], ident)
                db.add_profile(brand, pname[:47], fcat, "I", codes)


def load_converted(db, root, raw_budget):
    """Flipper-IRDB's _Converted_ library (IR Plus / Pronto exports): organised only by brand,
    so each file is classified. Raw captures are decoded; undecodable ones are kept raw while the
    raw budget (in samples) lasts. _Converted_/CSV is skipped - it is a copy of IRDB."""
    base = os.path.join(root, "_Converted_")
    if not os.path.isdir(base):
        return
    non_hifi = {brand_key(x) for x in NON_HIFI_BRANDS}
    for sub in ("IR_Plus", "Pronto", "Other"):
        sdir = os.path.join(base, sub)
        if not os.path.isdir(sdir):
            continue
        for dirpath, _, files in os.walk(sdir):
            for fn in sorted(files):
                if not fn.lower().endswith(".ir"):
                    continue
                rel = os.path.relpath(dirpath, sdir).split(os.sep)
                brand = rel[-1] if rel and rel[0] != "." else os.path.splitext(fn)[0]
                if len(brand) <= 1:
                    continue
                bname = pretty_brand(brand.title() if brand.isupper() and len(brand) > 4 else brand)
                model = os.path.splitext(fn)[0]
                sigs = list(parse_flipper(os.path.join(dirpath, fn)))
                names = [x.get("name", "") for x in sigs]
                codes = []
                for sig in sigs:
                    name = sig.get("name", "").strip()[:31]
                    if sig.get("type") == "parsed":
                        try:
                            conv = flipper_convert(sig["protocol"], le_hex(sig["address"]), le_hex(sig["command"]))
                        except (KeyError, ValueError):
                            conv = None
                    else:
                        try:
                            data = [abs(int(x)) for x in sig["data"].split()]
                            freq = int(float(sig.get("frequency", "38000")))
                            duty = float(sig.get("duty_cycle", "0.33"))
                        except (KeyError, ValueError):
                            data = []
                        conv = None
                        if 4 <= len(data) <= 2000:
                            conv = db.raw_code(data, freq, duty, allow_raw=db.raw_samples + len(data) <= db.raw_base + raw_budget)
                    if conv is None:
                        db.skipped["converted:undecodable/unsupported"] += 1
                        continue
                    codes.append({"name": name, "code": conv})
                    db.proto_count[PROTOS[conv[0]]] += 1
                if len(codes) >= 3:
                    pname = strip_brand(model.replace("_", " "), bname)
                    cat = guess_universal(names, model, brand_key(bname) in db.brands and brand_key(brand) not in non_hifi)
                    db.add_profile(bname, pname[:47], settop_override(cat, bname), "C", codes)


def irp_aliases(xml_path):
    """alt_name -> protocol name from IrpTransmogrifier's IrpProtocols.xml (e.g. DirecTV -> DirecTV_P3)."""
    out = {}
    if not xml_path or not os.path.exists(xml_path):
        return out
    txt = open(xml_path, encoding="utf-8").read()
    for m in re.finditer(r'<irp:protocol name="([^"]+)"(.*?)</irp:protocol>', txt, re.S):
        for a in re.findall(r'<irp:parameter name="alt_name">([^<]+)</irp:parameter>', m.group(2)):
            out[a.strip().lower()] = m.group(1)
    return out


def render_irp_placeholders(db, xml_path=None):
    """Render every ("IRP", protocol, D, S, F) placeholder to raw timings with IrpTransmogrifier
    (one JVM for the whole batch), then store it like a raw capture: raw_decode() turns it back into
    a native code when the firmware has an encoder for the result, otherwise it stays raw."""
    import subprocess
    java, jar, cdir = db.irp_render
    alias = irp_aliases(xml_path)
    reqs, keys = [], {}
    for b in db.brands.values():
        for p in b["profiles"]:
            for c in p["codes"]:
                if c["code"][0] == "IRP":
                    _, proto, d, s, f = c["code"]
                    name, extra = proto, ""
                    m = re.match(r"^(\S+)\s+(\w+)\s*=\s*(\d+)$", proto)       # "Tivo unit=3"
                    if m:
                        name, extra = m.group(1), {"unit": "U"}.get(m.group(2).lower(), m.group(2)) + "=" + m.group(3)
                    name = alias.get(name.lower(), name)
                    k = (name, d, s, f, extra)
                    if k not in keys:
                        keys[k] = len(reqs)
                        reqs.append(k)
    if not reqs:
        return
    inp = "".join("%s\t%d\t%d\t%d\t%s\n" % r for r in reqs)
    res = subprocess.run([java, "-cp", jar + os.pathsep + cdir, "IrpRender"], input=inp, capture_output=True,
                         text=True, check=True).stdout.splitlines()
    assert len(res) == len(reqs), (len(res), len(reqs))
    rendered = {}
    for r, line in zip(reqs, res):
        f = line.split("\t")
        if f[0] != "OK":
            # parameter left out by IRDB (-1) but required by the protocol: retry once with 0
            rendered[r] = None
            continue
        freq = int(f[1]) or 38000
        duty = float(f[2]) if f[2] else 0.33
        intro = [int(x) for x in f[3].split(",") if x] if f[3] else []
        rep_ = [int(x) for x in f[4].split(",") if x] if f[4] else []
        data = intro or rep_
        rendered[r] = (data, freq, duty) if 4 <= len(data) <= 2000 else None
    # retry missing-parameter failures with the parameter set to 0
    retry = [r for r in reqs if rendered[r] is None and (r[1] < 0 or r[2] < 0)]
    if retry:
        fixed = [(n, max(d, 0), max(s, 0), f, e) for n, d, s, f, e in retry]
        inp = "".join("%s\t%d\t%d\t%d\t%s\n" % r for r in fixed)
        res = subprocess.run([java, "-cp", jar + os.pathsep + cdir, "IrpRender"], input=inp, capture_output=True,
                             text=True, check=True).stdout.splitlines()
        for r, line in zip(retry, res):
            f = line.split("\t")
            if f[0] == "OK":
                data = [int(x) for x in f[3].split(",") if x] or [int(x) for x in f[4].split(",") if x]
                if 4 <= len(data) <= 2000:
                    rendered[r] = (data, int(f[1]) or 38000, float(f[2]) if f[2] else 0.33)
    ok = sum(1 for v in rendered.values() if v)
    print("IrpTransmogrifier: rendered %d of %d distinct codes in %d protocols without a native encoder" % (
        ok, len(reqs), len({r[0] for r in reqs})))
    cache = {}
    for b in db.brands.values():
        for p in b["profiles"]:
            out = []
            for c in p["codes"]:
                if c["code"][0] != "IRP":
                    out.append(c)
                    continue
                _, proto, d, s, f = c["code"]
                name, extra = proto, ""
                m = re.match(r"^(\S+)\s+(\w+)\s*=\s*(\d+)$", proto)
                if m:
                    name, extra = m.group(1), {"unit": "U"}.get(m.group(2).lower(), m.group(2)) + "=" + m.group(3)
                k = (alias.get(name.lower(), name), d, s, f, extra)
                r = rendered.get(k)
                if not r:
                    db.skipped["irdb:" + proto] += 1
                    continue
                if k not in cache:
                    cache[k] = db.raw_code(*r)
                code = cache[k]
                db.proto_count[PROTOS[code[0]]] += 1
                db.proto_count["(via IrpTransmogrifier) " + name] += 1
                out.append(dict(c, code=code))
            p["codes"] = out
        b["profiles"] = [p for p in b["profiles"] if p["codes"]]


def dedupe_profiles(db):
    """Drop a remote whose signals are all contained in another remote of the same brand
    (keeps the larger one; on a tie keeps the one loaded first, i.e. model-named over IRDB)."""
    for b in db.brands.values():
        profs = b["profiles"]
        sets = [frozenset(signal_key(c["code"], db) for c in p["codes"]) for p in profs]
        keep = []
        for i, p in enumerate(profs):
            covered = False
            for j in range(len(profs)):
                if i == j or not sets[i] <= sets[j]:
                    continue
                if len(sets[j]) > len(sets[i]) or j < i:     # strictly larger, or equal and earlier
                    covered = True
                    break
            if covered:
                db.dropped_dupes += 1
            else:
                keep.append(p)
        b["profiles"] = keep


# ---------------------------------------------------------------------------
# Consensus "Generic" profiles: some manufacturers follow a shared standard
# (Philips RC5 system addresses, Sony SIRC device numbers). For each group we
# take every key the database agrees on (>= 60 % of votes, >= 3 votes, >= 2
# brands) - handy for brands that aren't in either database.
# ---------------------------------------------------------------------------
CONSENSUS = [
    ("RC5", 16, "Amp", "RC5 amplifier / pre-amp (system 16)"),
    ("RC5", 17, "Tuner", "RC5 tuner (system 17)"),
    ("RC5", 18, "Tape", "RC5 cassette deck (system 18)"),
    ("RC5", 20, "CD", "RC5 CD player (system 20)"),
    ("SONY12", 16, "Amp", "Sony amplifier / receiver (SIRC12 dev 16)"),
    ("SONY12", 17, "CD", "Sony CD player (SIRC12 dev 17)"),
    ("SONY12", 13, "Tuner", "Sony tuner (SIRC12 dev 13)"),
]


def add_consensus_profiles(db):
    votes = defaultdict(lambda: defaultdict(lambda: defaultdict(set)))   # group -> key -> F -> {brand}
    counts = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))
    for b in db.brands.values():
        for p in b["profiles"]:
            for c in p["codes"]:
                pr, d, _, f = c["code"]
                if c["key"]:
                    g = (PROTOS[pr], d)
                    votes[g][c["key"]][f].add(b["name"])
                    counts[g][c["key"]][f] += 1
    for proto, dev, cat, title in CONSENSUS:
        g = (proto, dev)
        codes = []
        for key in range(1, len(KEYS)):
            if key not in counts[g]:
                continue
            tot = sum(counts[g][key].values())
            f, n = max(counts[g][key].items(), key=lambda x: x[1])
            if tot >= 3 and n / tot >= 0.6 and len(votes[g][key][f]) >= 2:
                codes.append({"name": KEYS[key][1], "code": (P[proto], dev, 0, f)})
        if len(codes) >= 3:
            db.add_profile("Generic", title, cat, "I", codes)


# ---------------------------------------------------------------------------
# Brand groups for Code search ("try every Japanese brand", ...). A brand may be in several
# groups. Names must match the brand names in the database (unknown names are reported).
# Origin = where the brand was founded / is headquartered.
# ---------------------------------------------------------------------------
BRAND_GROUPS = [
    ("Popular TV brands", [
        "Samsung", "LG", "Sony", "TCL", "Hisense", "Panasonic", "Philips", "Sharp", "Toshiba", "Vizio",
        "Kogan", "Bauhn", "Hitachi", "Grundig", "Telefunken", "Insignia", "Element", "Westinghouse", "Sanyo",
        "Akai", "JVC", "Polaroid", "Onn", "Xiaomi", "Skyworth", "Haier", "Metz", "Loewe", "Sharp Roku", "Funai",
        "Magnavox", "Emerson", "RCA", "Teac", "Soniq", "Linsar", "Veon", "Blaupunkt", "Thomson", "Medion"]),
    ("Set-top / streaming", [
        "Foxtel", "Fetch", "Humax", "Sky", "Virgin Media", "TiVo", "DirecTV", "Dish Network", "Comcast",
        "Xfinity", "Roku", "Apple", "Amazon", "Google", "Nvidia", "Xiaomi", "Telstra", "Optus", "Strong",
        "Technisat", "Topfield", "Kaon", "Arris", "Motorola", "Pace", "Cisco", "Scientific Atlanta"]),
    ("Most popular hi-fi", [   # mainstream hi-fi / AV brands most likely to be in a living room
        "Sony", "Yamaha", "Denon", "Marantz", "Onkyo", "Pioneer", "Integra", "Technics", "Panasonic",
        "JVC", "Kenwood", "Philips", "Harman Kardon", "JBL", "Bose", "NAD", "Cambridge Audio", "Arcam",
        "Rotel", "Samsung", "LG", "Sharp", "TEAC", "Sonos", "Klipsch", "Edifier"]),
    ("Japanese", [       # Marantz: US-founded, Japanese company since the 1990s; Rotel: founded in Japan
        "Aiwa", "Akai", "Alpine", "Clarion", "Denon", "Hitachi", "Integra", "JVC", "Kenwood", "Kyocera",
        "Luxman", "Marantz", "Maxell", "Mitsubishi", "Nakamichi", "NEC", "Nikko", "Onkyo", "Panasonic",
        "Pioneer", "Rotel", "Sansui", "Sanyo", "Sharp", "Sony", "Tascam", "TEAC", "Technics", "Toshiba",
        "Yamaha"]),
    ("British", [        # NAD: founded in London
        "Arcam", "Bowers & Wilkins", "Bush", "Cambridge Audio", "Goodmans", "KEF", "Linn", "Majority",
        "Meridian", "Mission", "Myryad", "NAD", "Naim", "Orbitsound", "Q Acoustics", "Rega", "Revo",
        "Roberts", "Ruark", "Steljes", "TAG McLaren"]),
    ("American & Canadian", [
        "Adcom", "Anthem", "Aragon", "Atlantic Technology", "Audio Research", "AudioControl", "Audioengine",
        "Ayre", "B&K Components", "Bogen", "Bose", "California Audio Labs", "Carver", "Cary Audio Design",
        "Citation", "Classe Audio", "Counterpoint", "Craig", "Definitive Technology", "Elan", "Emotiva",
        "Escient", "Fisher", "Fluance", "Fosgate", "GPX", "Harman Kardon", "JBL", "Kanto", "Kinergetics Research",
        "Klipsch", "Krell", "Lexicon", "Madrigal", "Magnum Dynalab", "Mark Levinson", "McIntosh", "Onn",
        "Parasound", "Polk Audio", "Proceed", "PS Audio", "Pyle", "RCA", "Russound", "Sirius XM", "Sonance",
        "Sonos", "Sunfire", "Theta Digital", "Velodyne", "Vizio", "Xantech"]),
    ("European", [
        "Advance Acoustic", "AEG", "Audio Refinement", "Auna", "Bang & Olufsen", "Canton", "Copland", "Denver",
        "Dutch Originals", "Dynaudio", "Elac", "Electrocompaniet", "Geneva", "Goldmund", "Grundig", "Gryphon",
        "Irradio", "Metronome", "Micromega", "Philips", "Primare", "Revox", "RME", "SABA", "Sennheiser",
        "Siemens", "T+A", "Terratec", "Teufel", "Tevion", "Universum", "X4TECH", "XORO"]),
    ("Korean", ["Samsung", "LG", "GoldStar"]),
    ("Chinese", ["Edifier", "Xiaomi", "TCL", "Hisense", "SMSL", "Fosi Audio", "Douk Audio", "Microlab"]),
    ("Australian", ["Kogan", "Bauhn"]),
    ("High-end / audiophile", [
        "Aesthetix", "Anthem", "Arcam", "Audio Research", "Ayre", "B.A.T", "Cary Audio Design", "Classe Audio",
        "Copland", "Electrocompaniet", "Goldmund", "Gryphon", "Krell", "Lexicon", "Linn", "Luxman", "Madrigal",
        "Mark Levinson", "McIntosh", "Meridian", "Metronome", "Micromega", "Naim", "Parasound", "Primare",
        "Proceed", "PS Audio", "Rega", "T+A", "Theta Digital"]),
    ("Standard codes (Generic)", ["Generic"]),
]


def expand_regex(rx, limit=20000):
    """All strings a finite regex matches (for the firmware's key-name table), or None if it is
    unbounded / too large."""
    try:
        import re._parser as sp
        import re._constants as sc
    except ImportError:                     # Python < 3.11
        import sre_parse as sp
        import sre_constants as sc

    def seq(items):
        out = {""}
        for op, av in items:
            alts = node(op, av)
            if alts is None:
                return None
            out = {a + b for a in out for b in alts}
            if len(out) > limit:
                return None
        return out

    def node(op, av):
        if op == sc.LITERAL:
            return {chr(av)}
        if op == sc.IN:
            chars = set()
            for o, a in av:
                if o == sc.LITERAL:
                    chars.add(chr(a))
                elif o == sc.RANGE:
                    chars |= {chr(c) for c in range(a[0], a[1] + 1)}
                else:
                    return None
            return chars
        if op == sc.BRANCH:
            out = set()
            for b in av[1]:
                r = seq(b)
                if r is None:
                    return None
                out |= r
            return out
        if op == sc.SUBPATTERN:
            return seq(av[-1])
        if op in (sc.MAX_REPEAT, sc.MIN_REPEAT):
            lo, hi, sub = av
            if hi == sc.MAXREPEAT or hi > 4:
                return None
            base = seq(sub)
            if base is None:
                return None
            out = set()
            cur = {""}
            for n in range(hi + 1):
                if n >= lo:
                    out |= cur
                cur = {a + b for a in cur for b in base}
                if len(cur) > limit:
                    return None
            return out
        if op == sc.AT:
            return {""}
        return None

    return seq(sp.parse(rx))


def key_name_table():
    """(compact name, key id) for every name canon_key() maps directly, sorted - the firmware's
    canonKey() does a binary search over it (plus the same ASSERT/INPUT-prefix fallback)."""
    table = {}
    for i, k in enumerate(KEYS):
        if not k[2]:
            continue
        names = expand_regex(r"(KEY)?(" + k[2] + r")")
        assert names is not None, "key regex not finite: " + k[0]
        for n in names:
            if n and n not in table:          # first key in KEYS order wins, like canon_key()
                table[n] = i
    for n, i in table.items():                # must agree with the regex matcher itself
        assert canon_key(n) == i or ZONE_RX.search(n), (n, i, canon_key(n))
    return sorted(table.items())


def c_str(s):
    s = s.encode("ascii", "replace").decode()
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("?", "\\?") + '"'


def build_tables(db, cats=None):
    """Brand/profile/code/raw tables (the rows written to the header or the .bin).
    cats: set of category names to keep (None = all)."""
    pool, pool_idx = [], {}

    def sidx(s):
        s = s.encode("ascii", "replace").decode()
        if s not in pool_idx:
            pool_idx[s] = sum(len(x) + 1 for x in pool)
            pool.append(s)
        return pool_idx[s]

    brands = sorted(db.brands.values(), key=lambda b: b["name"].lower())
    brands = [dict(b, profiles=[p for p in b["profiles"] if cats is None or p["cat"] in cats]) for b in brands]
    brands = [b for b in brands if b["profiles"]]
    brand_rows, prof_rows, code_rows = [], [], []
    for bi, b in enumerate(brands):
        first_prof = len(prof_rows)
        for p in sorted(b["profiles"], key=lambda p: (p["src"] == "I", CATS.index(p["cat"]), p["name"].lower())):
            first_code = len(code_rows)
            for c in p["codes"]:
                pr, d, s_, f = c["code"]
                code_rows.append((sidx(c["name"]), d, s_, f, pr, c["key"]))
            prof_rows.append((bi, sidx(p["name"]), CATS.index(p["cat"]), 1 if p["src"] == "I" else 0,
                              first_code, len(code_rows) - first_code))
        brand_rows.append((sidx(b["name"]), first_prof, len(prof_rows) - first_prof))
    assert len(prof_rows) < 65536, "IrBrand.first_prof is 16-bit"
    brand_index = {b["name"]: i for i, b in enumerate(brands)}
    group_rows, group_brands = [], []
    for gname, members in BRAND_GROUPS:
        idx = sorted({brand_index[m] for m in members if m in brand_index})
        if idx:
            group_rows.append((sidx(gname), len(group_brands), len(idx)))
            group_brands += idx
    used = sorted({r[1] for r in code_rows if r[4] == P["RAW"]})
    remap = {old: new for new, old in enumerate(used)}
    code_rows = [(r[0], remap[r[1]], r[2], r[3], r[4], r[5]) if r[4] == P["RAW"] else r for r in code_rows]
    raw_rows, raw_bytes, raw_table = [], bytearray(), []
    for freq, duty, data in (db.raw[i] for i in used):
        data = [min(v, 65535) for v in data]
        clusters = []
        for v in sorted(set(data)):
            if clusters and v and clusters[-1][0] and v <= max(clusters[-1][0] * 1.12, clusters[-1][0] + 40):
                clusters[-1].append(v)
            else:
                clusters.append([v])
        while len(clusters) > 255:
            i = min(range(len(clusters) - 1), key=lambda k: clusters[k + 1][0] - clusters[k][-1])
            clusters[i] += clusters.pop(i + 1)
        centers = [int(round(sum(c) / len(c))) for c in clusters]
        index = {v: ci for ci, c in enumerate(clusters) for v in c}
        table_off = len(raw_table)
        raw_table += centers
        data_off = len(raw_bytes)
        idx = [index[v] for v in data]
        if len(centers) <= 16:
            for i in range(0, len(idx), 2):
                raw_bytes.append(idx[i] | ((idx[i + 1] if i + 1 < len(idx) else 0) << 4))
        else:
            raw_bytes += bytes(idx)
        raw_rows.append((data_off, table_off, freq, len(data), len(centers), duty))
    return dict(pool=pool, brands=brand_rows, groups=group_rows, group_brands=group_brands, profiles=prof_rows,
                codes=code_rows, raw=raw_rows, raw_bytes=raw_bytes, raw_table=raw_table)


BIN_MAGIC, BIN_VERSION = b"IRDB", 2


def schema_hash():
    """Identifies the protocol / key / category numbering. The firmware only accepts a database file
    built with the same numbering (a newer generator with more keys needs a matching firmware)."""
    import zlib
    txt = "|".join(PROTOS) + "#" + "|".join(k[0] for k in KEYS) + "#" + "|".join(CATS)
    return zlib.crc32(txt.encode()) & 0xFFFFFFFF


def bin_image(t, used_irdb):
    """The database as one binary image (little-endian, structs laid out exactly as the firmware's
    IrBrand / IrGroup / IrProfile / IrCode / IrRawSig).
    Header: "IRDB", u32 version, u32 schema hash, u32 flags (bit 0: contains IRDB), u32 section count,
    then (u32 offset, u32 count) per section; every section starts 4-byte aligned.
    Sections: strings, brands, groups, group brands, profiles, codes, raw signals, raw data, raw table."""
    sections = []
    strs = b"".join(s.encode("ascii", "replace") + b"\0" for s in t["pool"])
    sections.append((strs, len(strs)))
    sections.append((b"".join(struct.pack("<IHH", *r) for r in t["brands"]), len(t["brands"])))
    sections.append((b"".join(struct.pack("<IHH", *r) for r in t["groups"]), len(t["groups"])))
    sections.append((b"".join(struct.pack("<H", x) for x in t["group_brands"]), len(t["group_brands"])))
    sections.append((b"".join(struct.pack("<H2xIBB2xIH2x", *r) for r in t["profiles"]), len(t["profiles"])))
    sections.append((b"".join(struct.pack("<IHHHBB", *r) for r in t["codes"]), len(t["codes"])))
    sections.append((b"".join(struct.pack("<IIIHBB", *r) for r in t["raw"]), len(t["raw"])))
    sections.append((bytes(t["raw_bytes"]), len(t["raw_bytes"])))
    sections.append((b"".join(struct.pack("<H", x) for x in t["raw_table"]), len(t["raw_table"])))
    hdr_len = 4 + 4 + 4 + 4 + 4 + len(sections) * 8
    out = bytearray()
    offs = []
    pos = (hdr_len + 3) & ~3
    body = bytearray()
    for data, n in sections:
        offs.append((pos + len(body), n))
        body += data
        while len(body) % 4:
            body += b"\0"
    out += BIN_MAGIC + struct.pack("<IIII", BIN_VERSION, schema_hash(), 1 if used_irdb else 0, len(sections))
    for o, n in offs:
        out += struct.pack("<II", o, n)
    while len(out) < pos:
        out += b"\0"
    out += body
    return bytes(out)


def write_bin(t, path, used_irdb):
    img = bin_image(t, used_irdb)
    with open(path, "wb") as f:
        f.write(img)
    print("wrote %s: %d bytes (brands=%d profiles=%d codes=%d raw=%d)" % (
        path, len(img), len(t["brands"]), len(t["profiles"]), len(t["codes"]), len(t["raw"])))
    return img


def write_builtin(img, t, out_path, used_irdb):
    """ir_db_data.h: the binary image as one string literal (compiles much faster than a brace list),
    4-byte aligned so the firmware can use the tables in place, straight from flash."""
    with open(out_path, "w") as o:
        o.write("// AUTO-GENERATED by tools/build_db.py - do not edit by hand.\n")
        o.write("// Sources: Flipper-IRDB (github.com/Lucaslhm/Flipper-IRDB), CC0 1.0\n")
        if used_irdb:
            o.write("// Contains/accesses irdb by Simon Peter and contributors, used under permission.\n")
            o.write("// For licensing details and for information on how to contribute to the database, see\n")
            o.write("// https://github.com/probonopd/irdb\n")
        o.write("// Brands: %d  Remotes: %d  Codes: %d  Raw signals: %d  Image: %d bytes\n" % (
            len(t["brands"]), len(t["profiles"]), len(t["codes"]), len(t["raw"]), len(img)))
        o.write("#pragma once\n#include <stdint.h>\n\n")
        o.write("#define IRDB_BUILTIN_SIZE %d\n" % len(img))
        o.write("alignas(4) static const char IRDB_BUILTIN[IRDB_BUILTIN_SIZE + 1] =\n")
        line = []
        n = 0
        for byte in img:
            c = chr(byte)
            if 32 <= byte < 127 and c not in '"\\?':
                line.append(c)
            else:
                line.append("\\%03o" % byte)
            n += 1
            if n % 96 == 0:
                o.write('  "' + "".join(line) + '"\n')
                line = []
        o.write('  "' + "".join(line) + '";\n')
    print("wrote %s (%d bytes of database)" % (out_path, len(img)))


def write_header(db, out_path, keys_path, cats=None):
    # --- string pool (deduplicated) ---
    pool, pool_idx = [], {}

    def sidx(s):
        s = s.encode("ascii", "replace").decode()
        if s not in pool_idx:
            pool_idx[s] = sum(len(x) + 1 for x in pool)
            pool.append(s)
        return pool_idx[s]

    brands = sorted(db.brands.values(), key=lambda b: b["name"].lower())
    brands = [dict(b, profiles=[p for p in b["profiles"] if cats is None or p["cat"] in cats]) for b in brands]
    brands = [b for b in brands if b["profiles"]]
    brand_rows, prof_rows, code_rows = [], [], []
    for bi, b in enumerate(brands):
        first_prof = len(prof_rows)
        # Flipper (model-named) profiles first, then irdb code sets
        for p in sorted(b["profiles"], key=lambda p: (p["src"] == "I", CATS.index(p["cat"]), p["name"].lower())):
            first_code = len(code_rows)
            for c in p["codes"]:
                pr, d, s, f = c["code"]
                code_rows.append((sidx(c["name"]), d, s, f, pr, c["key"]))
            prof_rows.append((bi, sidx(p["name"]), CATS.index(p["cat"]), 1 if p["src"] == "I" else 0,
                              first_code, len(code_rows) - first_code))
        brand_rows.append((sidx(b["name"]), first_prof, len(prof_rows) - first_prof))

    # expected names, for tools/test_all_codes.cpp to verify the string pool round-trips
    rev = {v: k for k, v in pool_idx.items()}
    exp_dir = os.path.join(os.path.dirname(os.path.abspath(keys_path)), "tools")
    if os.path.isdir(exp_dir):
        with open(os.path.join(exp_dir, "db_expect.txt"), "w") as ex:
            for i, r in enumerate(brand_rows):
                ex.write("B %d %s\n" % (i, rev[r[0]]))
            for i, r in enumerate(prof_rows):
                ex.write("P %d %s\n" % (i, rev[r[1]]))
            for i in range(0, len(code_rows), 7):
                ex.write("C %d %s\n" % (i, rev[code_rows[i][0]]))
        expect_path = os.path.join(exp_dir, "db_expect.txt")
    else:
        expect_path = None
    # Raw captures, compressed: each signal gets a small table of its distinct durations
    # (clustered within ~12 %), samples are 4-bit indices (2 per byte) when <= 16 clusters,
    # otherwise 8-bit indices.
    # brand groups -> indices into the (sorted) brand table
    brand_index = {b["name"]: i for i, b in enumerate(brands)}
    group_rows, group_brands = [], []
    for gname, members in BRAND_GROUPS:
        idx = []
        for m in members:
            if m in brand_index:
                idx.append(brand_index[m])
            else:
                print("note: brand group '%s': no brand named '%s' in the database" % (gname, m))
        idx = sorted(set(idx))
        if idx:
            group_rows.append((sidx(gname), len(group_brands), len(idx)))
            group_brands += idx

    # keep only raw captures still referenced (remotes dropped as duplicates leave orphans)
    used = sorted({r[1] for r in code_rows if r[4] == P["RAW"]})
    remap = {old: new for new, old in enumerate(used)}
    code_rows = [(r[0], remap[r[1]], r[2], r[3], r[4], r[5]) if r[4] == P["RAW"] else r for r in code_rows]
    raw_rows, raw_bytes, raw_table, raw_expect = [], [], [], []
    max_err = 0.0
    for freq, duty, data in (db.raw[i] for i in used):
        data = [min(v, 65535) for v in data]
        uniq = sorted(set(data))
        clusters = []                       # list of lists of values
        for v in uniq:
            # same cluster if within 12 % (or 40 us for short pulses) of the cluster's smallest
            # member; 0 (padding) always stays exact
            if clusters and v and clusters[-1][0] and v <= max(clusters[-1][0] * 1.12, clusters[-1][0] + 40):
                clusters[-1].append(v)
            else:
                clusters.append([v])
        while len(clusters) > 255:          # pathological capture: merge closest neighbours
            i = min(range(len(clusters) - 1), key=lambda k: clusters[k + 1][0] - clusters[k][-1])
            clusters[i] += clusters.pop(i + 1)
        centers = [int(round(sum(c) / len(c))) for c in clusters]
        index = {}
        for ci, c in enumerate(clusters):
            for v in c:
                index[v] = ci
        for v in data:
            err = abs(centers[index[v]] - v)
            max_err = max(max_err, err / v if v >= 300 else err / 300.0)   # relative, or vs. 300 us
        table_off = len(raw_table)
        raw_table += centers
        data_off = len(raw_bytes)
        idx = [index[v] for v in data]
        if len(centers) <= 16:
            for i in range(0, len(idx), 2):
                lo = idx[i]
                hi = idx[i + 1] if i + 1 < len(idx) else 0
                raw_bytes.append(lo | (hi << 4))
        else:
            raw_bytes += idx
        raw_rows.append((data_off, table_off, freq, len(data), len(centers), duty))
        raw_expect.append((len(data), sum(centers[k] for k in idx), centers[idx[0]] if idx else 0))
    if expect_path:
        with open(expect_path, "a") as ex:
            for i, (n, total, first) in enumerate(raw_expect):
                ex.write("R %d %d %d %d\n" % (i, n, total, first))
    print("raw: %d signals, %d samples -> %d index bytes + %d table entries, max quantisation error %.1f %% (of the pulse, or of 300 us for shorter pulses)" % (
        len(raw_rows), sum(len(db.raw[i][2]) for i in used), len(raw_bytes), len(raw_table), 100 * max_err))

    with open(out_path or os.devnull, "w") as o:
        o.write("// AUTO-GENERATED by tools/build_db.py - do not edit by hand.\n")
        o.write("// Sources: Flipper-IRDB (github.com/Lucaslhm/Flipper-IRDB), CC0 1.0\n")
        if db.used_irdb:
            o.write("// Contains/accesses irdb by Simon Peter and contributors, used under permission.\n")
            o.write("// For licensing details and for information on how to contribute to the database, see\n")
            o.write("// https://github.com/probonopd/irdb\n")
        o.write("// Brands: %d  Profiles: %d  Codes: %d  Raw signals: %d\n" % (
            len(brand_rows), len(prof_rows), len(code_rows), len(raw_rows)))
        o.write("#pragma once\n#include <stdint.h>\n#include \"ir_db.h\"\n\n")
        o.write("#define IRDB_INCLUDES_IRDB %d\n" % (1 if db.used_irdb else 0))
        o.write("#define IRDB_NUM_GROUPS %d\n" % len(group_rows))
        o.write("#define IRDB_NUM_BRANDS %d\n#define IRDB_NUM_PROFILES %d\n#define IRDB_NUM_CODES %d\n#define IRDB_NUM_RAW %d\n\n" % (
            len(brand_rows), len(prof_rows), len(code_rows), len(raw_rows)))
        # string pool as one big char array
        o.write("static const char IRDB_STR[] = \n")
        line = ""
        for s in pool:
            # "\\000" (full 3-digit octal) - a bare "\\0" followed by a digit would form a different escape
            piece = c_str(s)[1:-1] + "\\000"
            if len(line) + len(piece) > 110:
                o.write('  "%s"\n' % line)
                line = ""
            line += piece
        o.write('  "%s";\n\n' % line)
        o.write("static const IrBrand IRDB_BRANDS[IRDB_NUM_BRANDS] = {\n")
        for r in brand_rows:
            o.write("  {%d,%d,%d},\n" % r)
        o.write("};\n\n// Brand groups for Code search: {name, first index into IRDB_GROUP_BRANDS, count}\n")
        o.write("static const IrGroup IRDB_GROUPS[IRDB_NUM_GROUPS] = {\n")
        for r in group_rows:
            o.write("  {%d,%d,%d},\n" % r)
        o.write("};\n\nstatic const uint16_t IRDB_GROUP_BRANDS[%d] = {\n  " % max(1, len(group_brands)))
        o.write(",".join(str(x) for x in (group_brands or [0])) + "\n")
        o.write("};\n\nstatic const IrProfile IRDB_PROFILES[IRDB_NUM_PROFILES] = {\n")
        for r in prof_rows:
            o.write("  {%d,%d,%d,%d,%d,%d},\n" % r)
        o.write("};\n\nstatic const IrCode IRDB_CODES[IRDB_NUM_CODES] = {\n")
        for i in range(0, len(code_rows), 4):
            o.write("  " + "".join("{%d,%d,%d,%d,%d,%d}," % r for r in code_rows[i:i + 4]) + "\n")
        o.write("};\n\nstatic const IrRawSig IRDB_RAW[IRDB_NUM_RAW ? IRDB_NUM_RAW : 1] = {\n")
        for r in raw_rows or [(0, 0, 0, 0, 0, 0)]:
            o.write("  {%d,%d,%d,%d,%d,%d},\n" % r)
        o.write("};\n\nstatic const uint8_t IRDB_RAW_DATA[%d] = {\n" % max(1, len(raw_bytes)))
        for i in range(0, max(1, len(raw_bytes)), 24):
            o.write("  " + ",".join(str(v) for v in (raw_bytes[i:i + 24] or [0])) + ",\n")
        o.write("};\n\nstatic const uint16_t IRDB_RAW_TABLE[%d] = {\n" % max(1, len(raw_table)))
        for i in range(0, max(1, len(raw_table)), 16):
            o.write("  " + ",".join(str(v) for v in (raw_table[i:i + 16] or [0])) + ",\n")
        o.write("};\n")

    with open(keys_path, "w") as o:
        o.write("// AUTO-GENERATED by tools/build_db.py - canonical remote keys.\n#pragma once\n\n")
        o.write("enum IrKey : uint8_t {\n")
        for i, k in enumerate(KEYS):
            o.write("  KEY_%s = %d,\n" % (k[0], i))
        o.write("  KEY__COUNT = %d\n};\n\n" % len(KEYS))
        o.write("static const char *const IR_KEY_LABELS[KEY__COUNT] = {\n")
        o.write("  " + ", ".join(c_str(k[1]) for k in KEYS) + "\n};\n\n")
        o.write("static const char *const IR_KEY_IDS[KEY__COUNT] = {\n")
        o.write("  " + ", ".join(c_str(k[0]) for k in KEYS) + "\n};\n\n")
        o.write("static const char *const IR_CAT_NAMES[] = {" + ", ".join(c_str(c) for c in CATS) + "};\n")
        o.write("#define IR_NUM_CATS %d\n" % len(CATS))
        tbl = key_name_table()
        o.write("\n// Button-name -> key table (compact upper-case names, sorted) for irdb::canonKey()\n")
        o.write("struct IrKeyName { const char *name; uint8_t key; };\n")
        o.write("static const IrKeyName IR_KEY_NAMES[] = {\n")
        for i in range(0, len(tbl), 6):
            o.write("  " + "".join("{%s,%d}," % (c_str(n), k) for n, k in tbl[i:i + 6]) + "\n")
        o.write("};\n#define IR_NUM_KEY_NAMES %d\n" % len(tbl))
        o.write("\n// Numbering of protocols / keys / categories: a database file is only accepted when it matches\n")
        o.write("#define IRDB_SCHEMA 0x%08Xu\n" % schema_hash())
        o.write("#define IRDB_BIN_VERSION %d\n" % BIN_VERSION)

    size = len(code_rows) * 12 + len(prof_rows) * 16 + len(brand_rows) * 8 + sum(len(x) + 1 for x in pool) \
        + len(raw_rows) * 16 + len(raw_bytes) + len(raw_table) * 2
    print("brands=%d profiles=%d codes=%d raw=%d strings=%d  ~%d KB flash" % (
        len(brand_rows), len(prof_rows), len(code_rows), len(raw_rows), len(pool), size // 1024))
    return brands


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--flipper", required=True)
    ap.add_argument("--irdb", default=None,
                    help="path to probonopd/irdb (optional; omit for a Flipper-IRDB-only, CC0 database)")
    ap.add_argument("--no-converted", action="store_true", help="skip Flipper-IRDB's _Converted_ library")
    ap.add_argument("--raw-budget", type=int, default=3000000,
                    help="max raw samples (2 bytes each) kept from the _Converted_ library (default 250000)")
    ap.add_argument("--out", default=None, help="optional: also write the image as a C header")
    ap.add_argument("--keys", default="ir_keys.h")
    ap.add_argument("--report", default=None, help="optional text report of brands/protocols")
    ap.add_argument("--bin", default="irdb.bin", help="the database file for the SD card (default irdb.bin)")
    ap.add_argument("--irpt-jar", default=None,
                    help="IrpTransmogrifier-*-jar-with-dependencies.jar: render IRDB codes in protocols without a "
                         "native encoder (MCE, DirecTV, Dish, G.I. Cable, XMP, TiVo, ...) to raw signals")
    ap.add_argument("--irpt-xml", default=None, help="IrpProtocols.xml (for protocol alias names, e.g. DirecTV)")
    ap.add_argument("--builtin-cats", default=None,
                    help="comma-separated categories for --out (default: all). "
                         "Choices: " + ",".join(CATS))
    a = ap.parse_args()
    db = DB()
    load_flipper(db, a.flipper)
    db.raw_base = db.raw_samples
    if not a.no_converted:
        load_converted(db, a.flipper, a.raw_budget)
    if a.irpt_jar:
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "irp_render")
        db.irp_render = ("java", os.path.abspath(a.irpt_jar), here)
    if a.irdb:
        load_irdb(db, a.irdb)
    if db.irp_render:
        render_irp_placeholders(db, a.irpt_xml)
    dedupe_profiles(db)
    add_consensus_profiles(db)
    print("raw captures decoded to protocols: %d, kept raw: %d, duplicate remotes dropped: %d" % (
        db.raw_decoded, len(db.raw), db.dropped_dupes))
    cats = set(a.builtin_cats.split(",")) if a.builtin_cats else None
    if cats:
        bad = cats - set(CATS)
        assert not bad, "unknown categories: %s" % bad
    brands = write_header(db, None, a.keys, cats)          # keys + db_expect.txt for the host tests
    if a.out:
        t_builtin = build_tables(db, cats)
        write_builtin(bin_image(t_builtin, db.used_irdb), t_builtin, a.out, db.used_irdb)
    if a.bin:
        write_bin(build_tables(db), a.bin, db.used_irdb)
    print("protocol usage:", dict(sorted(db.proto_count.items(), key=lambda x: -x[1])))
    print("skipped:", dict(sorted(db.skipped.items(), key=lambda x: -x[1])))
    if a.report:
        with open(a.report, "w") as r:
            r.write("Protocol usage (codes):\n")
            for k, v in sorted(db.proto_count.items(), key=lambda x: -x[1]):
                r.write("  %-11s %6d\n" % (k, v))
            r.write("\nSkipped (unsupported/non-hifi protocols):\n")
            for k, v in sorted(db.skipped.items(), key=lambda x: -x[1]):
                r.write("  %-24s %6d\n" % (k, v))
            r.write("\nBrands (%d):\n" % len(brands))
            for b in brands:
                r.write("  %-28s %4d profiles\n" % (b["name"], len(b["profiles"])))


if __name__ == "__main__":
    main()
