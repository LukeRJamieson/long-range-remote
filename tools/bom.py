#!/usr/bin/env python3
"""
Bill of materials for the long-range remote (Lite LED head + control board, 4 x AA), one complete set.

Prices: Digi-Key Australia, AUD ex GST, checked 3 Oct 2026 (bulk / cut-tape packaging, the unit price
at the price break that the summed quantity reaches). Core Electronics: AUD inc GST, same day.

Prices marked USD were only readable on US sites (Digi-Key US or Mouser US, 4 Oct 2026); the upload file
uses the MPN for those.

Writes:
  bom/lite_digikey_bom.csv            every line with refs, price, stock
  bom/lite_digikey_upload.csv         Digi-Key BOM-manager upload (Quantity, Part Number, Customer Ref)
  bom/lite_core_electronics.csv       display ribbon cable and headers
"""
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "bom"

# Digi-Key AU data (dk part, mfr, mpn, description, stock, price breaks [(qty, AUD)])
DK = {
    "SFH4545": ("475-SFH4545-ND", "ams OSRAM", "SFH 4545", "IR emitter 950nm 5mm radial, +/-5 deg, 100mA", "38,056",
                [(1, 1.25), (10, 0.869), (100, 0.6353), (500, 0.53054)]),
    "R24": ("ERJ-P08J240V", "Panasonic", "ERJ-P08J240V", "24R 5% 0.66W 1206 pulse-withstanding thick film, AEC-Q200 "
            "(USD: Mouser US price, 4 Oct 2026; Digi-Key PN not read - the upload uses the MPN)",
            "11,750 (Mouser)", [(1, 0.17), (10, 0.054), (100, 0.043)]),
    "AA4": ("2478", "Keystone", "2478", "4 x AA battery holder, open, 6in wire leads, 63 x 58 x 16 mm "
            "(USD: Mouser US price for 534-2478, 4 Oct 2026; Digi-Key lists it too - the upload uses the MPN)", "748 (Mouser)", [(1, 2.08), (10, 1.89), (100, 1.81)]),
    "PFET80": ("IPD80P03P4L07ATMA2", "Infineon", "IPD80P03P4L07ATMA2", "P-MOSFET -30V -80A logic-level, 12mOhm max "
               "@ -4.5V, DPAK (TO-252) - LED power gate (USD: Mouser US price for 726-IPD80P03P4L07AT2, 4 Oct "
               "2026; the upload uses the MPN)", "9,179 (Mouser)", [(1, 2.07)]),
    "2N3904": ("2N3904FS-ND", "onsemi", "2N3904BU", "NPN 40V 0.2A, TO-92 (USD)", "109,580 (US site)",
               [(1, 0.28), (10, 0.168), (100, 0.105)]),
    "IDC10": ("732-2094-ND", "Wurth Elektronik", "61201021621", "2x5 2.54mm shrouded box header, vertical", "34,340",
              [(1, 0.66), (10, 0.645), (50, 0.595)]),
    "IDC16": ("732-2095-ND", "Wurth Elektronik", "61201621621", "2x8 2.54mm shrouded box header, vertical", "7,987",
              [(1, 0.87), (10, 0.858), (50, 0.7942)]),
    "XH2": ("455-B2B-XH-A-ND", "JST", "B2B-XH-A(LF)(SN)", "JST XH 2-pin header, vertical", "293,978",
            [(1, 0.15), (10, 0.127), (100, 0.108)]),
    "C470": ("P14419-ND", "Panasonic", "EEU-FR1E471", "470uF 25V low-ESR aluminium, 10 x 12.5mm (14mm max seated), 5mm pitch", "3,128 (12 Oct)",
             [(1, 1.46), (10, 0.925), (50, 0.7018)]),
    "C100n": ("445-173265-1-ND", "TDK", "FG18X7R1H104KNT06", "100nF 50V X7R radial, 2.5mm pitch", "42,546",
              [(1, 0.39), (10, 0.226), (50, 0.1616)]),
    "C1u": ("445-173374-1-ND", "TDK", "FG24X7R1H105KRT06", "1uF 50V X7R radial, 5mm pitch", "30,819",
            [(1, 0.79), (10, 0.478), (50, 0.3574)]),
    "R1k": ("1.00KXBK-ND", "YAGEO", "MFR-25FBF52-1K", "1k 1% 1/4W metal film axial", "64,471",
            [(1, 0.15), (10, 0.061), (25, 0.0508)]),
    "R10k": ("10.0KXBK-ND", "YAGEO", "MFR-25FBF52-10K", "10k 1% 1/4W metal film axial", "69,768",
             [(1, 0.15), (10, 0.061), (25, 0.0508)]),
    "R470k": ("470KXBK-ND", "YAGEO", "MFR-25FBF52-470K", "470k 1% 1/4W metal film axial", "10,000",
              [(1, 0.15), (10, 0.061), (25, 0.0508)]),
    "R100k": ("100KXBK-ND", "YAGEO", "MFR-25FBF52-100K", "100k 1% 1/4W metal film axial", "19,216",
              [(1, 0.15), (10, 0.061), (25, 0.0508)]),
    "R47": ("MFR-25FBF52-47R", "YAGEO", "MFR-25FBF52-47R", "47R 1% 1/4W metal film axial - Q2 gate "
            "(price assumed equal to the other MFR-25 values, not checked; the upload uses the MPN)", "-",
            [(1, 0.15), (10, 0.061), (25, 0.0508)]),
    "AOD424": ("785-1565-1-ND", "Alpha & Omega", "AOD424", "N-MOSFET 20V 45A, 5.7 mOhm max @ Vgs 2.5V, DPAK - "
               "LED switch, driven straight from GPIO17 (Digi-Key AU, 11 Oct 2026)", "32,110", [(1, 2.21), (10, 1.398)]),
    "B3F1000": ("SW400-ND", "Omron (Aratas)", "B3F-1000", "Tactile switch 6x6mm, 4.3mm", "26,202",
                [(1, 0.51), (10, 0.435)]),
    "XHP2": ("XHP-2", "JST", "XHP-2", "JST XH 2-way plug housing, for the AA holder's leads - price not checked", "-",
             [(1, 0.0)]),
    "SXH": ("SXH-001T-P0.6", "JST", "SXH-001T-P0.6", "JST XH crimp contact, 22-28 AWG - price not checked (needs an XH crimp tool)", "-",
            [(1, 0.0)]),
    "IDCSOCK16": ("LKC16H-ND", "CNC Tech", "3030-16-0102-00", "16-way IDC ribbon socket, 2.54mm, keyed - the new end "
                  "when the 16-way ribbon is cut to length (Digi-Key AU, 12 Oct 2026)", "3,015", [(1, 0.82), (10, 0.686)]),
    "CABLE10": ("1528-370-ND", "Adafruit", "370", "10-pin 2x5 0.1in socket/socket IDC cable, 6in", "261",
                [(1, 2.91)]),
}

USD = {"2N3904", "R24", "AA4", "PFET80"}

# (board, refs, key, qty)
LITE_LINES = [
    ("LED head lite", "D1-D66", "SFH4545", 66),
    ("LED head lite", "R1-R33", "R24", 33),
    # LED head J1 (2x5 0.1in male header): two 5-pin pieces of the Pololu 1x40 strip (Core Electronics list)
    ("Control lite", "Q2 (LED switch)", "AOD424", 1),
    ("Control lite", "C1 C2", "C470", 2),
    ("Control lite", "C3 C6 C7", "C100n", 3),
    ("Control lite", "C8", "C1u", 1),
    ("Control lite", "R11", "R1k", 1),
    ("Control lite", "R4 R7 R14", "R100k", 3),
    ("Control lite", "R12 R13 (pack sense)", "R470k", 2),
    ("Control lite", "R15 R16", "R10k", 2),
    ("Control lite", "Q5 (LED power gate)", "PFET80", 1),
    ("Control lite", "Q6 (gate driver)", "2N3904", 1),
    ("Control lite", "R6 (Q2 gate, slows the edge)", "R47", 1),
    ("Control lite", "SW1-SW6 (D-pad + SELECT + BACK)", "B3F1000", 6),
    ("Control lite", "J5", "IDC10", 1),
    ("Control lite", "J2", "IDC16", 1),
    ("Control lite", "J1", "XH2", 1),
    ("Cables", "LED head J1 - control J5", "CABLE10", 1),
    ("Cables", "16-way ribbon: new socket after cutting it to ~70 mm (+ 1 spare)", "IDCSOCK16", 2),
    ("Battery", "4 x AA holder (wire leads to control J1)", "AA4", 1),
    ("Battery", "plug for the holder's leads (mates control J1)", "XHP2", 1),
    ("Battery", "crimps for that plug (+ 2 spares)", "SXH", 4),
]
LITE_CORE = [  # (item, qty, unit AUD inc GST, note)
    ("Adafruit GPIO Ribbon Cable 2x8 IDC, 16 pins, 12in", 1, 4.75, "control J2 <-> Waveshare header: cut to ~70 mm and re-terminated (Digi-Key LKC16H-ND)"),
    ("Pololu 1x40 breakaway male header, double-sided", 1, 2.40, "2x 8 pins: gender changer for the Waveshare's female header; 2x 5 pins: LED head J1"),
]

def unit_price(breaks, qty):
    p = breaks[0][1]
    for q, price in breaks:
        if qty >= q:
            p = price
    return p


def build(lines, core, prefix, title):
    need = {}
    for board, refs, key, qty in lines:
        need[key] = need.get(key, 0) + qty
    usd_keys = [k for k in need if k in USD]
    aud = sum(unit_price(DK[k][5], q) * q for k, q in need.items() if k not in USD)
    usd = sum(unit_price(DK[k][5], q) * q for k, q in need.items() if k in USD)
    with open(OUT / f"{prefix}digikey_upload.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Quantity", "Part Number", "Customer Reference"])
        for k, q in need.items():
            w.writerow([q, DK[k][0], "; ".join(f"{b} {r}" for b, r, kk, _ in lines if kk == k)])
    with open(OUT / f"{prefix}digikey_bom.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Board", "Refs", "Qty", "Manufacturer", "MPN", "Digi-Key PN", "Description",
                    "Unit price", "Extended", "Currency", "Stock (3-4 Oct 2026)"])
        for board, refs, key, qty in lines:
            dk, mfr, mpn, desc, stock, breaks = DK[key]
            u = unit_price(breaks, need[key])
            w.writerow([board, refs, qty, mfr, mpn, dk, desc, f"{u:.4f}", f"{u * qty:.2f}",
                        "USD" if key in USD else "AUD ex GST", stock])
        w.writerow([])
        w.writerow(["", "", "", "TOTAL", "", "", "AUD ex GST lines", "", f"{aud:.2f}", "AUD ex GST"])
        w.writerow(["", "", "", "", "", "", f"USD lines ({', '.join(usd_keys)})", "", f"{usd:.2f}", "USD"])
    with open(OUT / f"{prefix}core_electronics.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Item", "Qty", "Unit (AUD inc GST)", "Extended (AUD inc GST)", "Note"])
        core_total = 0.0
        for item, q, u, note in core:
            w.writerow([item, q, f"{u:.2f}", f"{u * q:.2f}", note])
            core_total += u * q
        w.writerow(["TOTAL", "", "", f"{core_total:.2f}", ""])
    unpriced = [k for k in need if unit_price(DK[k][5], need[k]) == 0]
    print(f"{title}: Digi-Key ${aud:.2f} AUD ex GST + US${usd:.2f} ({', '.join(usd_keys)}); unpriced: {', '.join(unpriced) or 'none'}; "
          f"Core Electronics ${core_total:.2f} AUD inc GST")
    return aud, usd, core_total


def main():
    OUT.mkdir(exist_ok=True)
    for f in OUT.glob("digikey_upload_option_*.csv"):
        f.unlink()
    for f in ("digikey_bom.csv", "digikey_upload.csv", "core_electronics.csv"):   # old full-version files
        (OUT / f).unlink(missing_ok=True)
    return build(LITE_LINES, LITE_CORE, "lite_", "Lite")


if __name__ == "__main__":
    main()
