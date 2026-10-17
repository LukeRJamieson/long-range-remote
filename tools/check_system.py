#!/usr/bin/env python3
"""
Cross-board consistency checks for the long-range remote (led_head_lite + control_lite).

  1. The 10-way ribbon is straight-through (pin n = pin n), so LED head J1 and control J5 must carry
     the same signal on the same pin (odd = LED_PWR, even = LED_RTN).
  2. Control J2 must match the Waveshare ESP32-S3-RLCD-4.2 2x8 header pin functions.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kigen  # noqa: E402

ROOT = kigen.ROOT


def pins(board, ref):
    _, pad_net = kigen.parse_netlist(ROOT / board / "build" / f"{board}.net")
    out = {}
    for (r, p), n in pad_net.items():
        if r == ref:
            out[int(p)] = n.lstrip("/")
    return out


def norm(n):
    return {"VBAT": "LED_PWR"}.get(n, n)          # control board: J5 odd pins are the (gated) AA pack


def main():
    ok = True
    head, ctrl = "led_head_lite", "control_lite"      # (a leftover --lite argument is ignored)
    print(f"{head} + {ctrl}")
    # ---- 1. LED head ribbon
    a, c = pins(head, "J1"), pins(ctrl, "J5")
    for p in range(1, 11):
        na, nc = norm(a.get(p, "NC")), norm(c.get(p, "NC"))
        flag = "ok" if na == nc else "MISMATCH"
        ok &= na == nc
        print(f"  {head}.J1.{p:<2} {na:<10} <-> {ctrl}.J5.{p:<2} {nc:<10} {flag}")
    print()
    # ---- 2. Waveshare header (from the Waveshare ESP32-S3-RLCD-4.2 schematic, header P1)
    waveshare = {1: "VCC3V3", 2: "VBUS", 3: "GND", 4: "GND", 5: "GPIO0", 6: "GPIO19", 7: "GPIO1", 8: "GPIO20",
                 9: "GPIO2", 10: "U0TXD", 11: "GPIO3", 12: "U0RXD", 13: "GPIO17", 14: "SDA (GPIO13)",
                 15: "GPIO18", 16: "SCL (GPIO14)"}
    # keys go straight to GPIOs (firmware config.h KEY_*_PIN); RIGHT via R11 1k (net KEY_TXD); BACK on SCL
    # (GPIO14: the display board's I2C pull-up holds it high; the firmware doesn't use that bus)
    # the display runs from its own 18650 (no VBUS feed); pack sense on GPIO1; UP on pin 15 / SELECT on pin 5
    # (each at its own key's end of the header)
    want = {3: "GND", 4: "GND", 5: "BTN_SEL", 7: "VBAT_SENSE", 9: "ARM", 10: "KEY_TXD", 11: "BTN_DOWN",
            12: "BTN_LEFT", 13: "IR_DRV", 15: "BTN_UP", 16: "BTN_BACK"}
    j2 = pins(ctrl, "J2")
    for p in range(1, 17):
        got = j2.get(p, "NC")
        got = "NC" if got.startswith("unconnected") else got
        exp = want.get(p, "NC")
        flag = "ok" if got == exp else "MISMATCH"
        ok &= got == exp
        print(f"  {ctrl}.J2.{p:<2} {got:<10} -> Waveshare {waveshare[p]:<13} {flag}")
    # ---- 3. the firmware's pin numbers match the board's J2 wiring (config.h #defines)
    import re
    cfg = (ROOT / "firmware" / "LongRangeRemote" / "config.h").read_text()
    define = dict(re.findall(r"^#define\s+(\w+)\s+(\d+)\b", cfg, re.M))
    gpio_of_pin = {p: int(re.search(r"(\d+)\)?$", n).group(1)) if re.search(r"\d+\)?$", n) else None
                   for p, n in waveshare.items()}
    gpio_of_pin.update({10: 43, 12: 44})                       # U0TXD / U0RXD
    print()
    for macro, net in (("KEY_UP_PIN", "BTN_UP"), ("KEY_DOWN_PIN", "BTN_DOWN"), ("KEY_LEFT_PIN", "BTN_LEFT"),
                       ("KEY_RIGHT_PIN", "KEY_TXD"), ("KEY_SELECT_PIN", "BTN_SEL"), ("KEY_BACK_PIN", "BTN_BACK"),
                       ("IR_TX_PIN", "IR_DRV"), ("LED_ARM_PIN", "ARM"), ("PACK_ADC_PIN", "VBAT_SENSE")):
        pin = next((p for p, n in want.items() if n == net), None)
        exp = gpio_of_pin.get(pin)
        got = int(define[macro]) if macro in define else None
        flag = "ok" if got == exp and got is not None else "MISMATCH"
        ok &= flag == "ok"
        print(f"  firmware {macro:<15} = GPIO{got}  board {net:<10} on J2.{pin} = GPIO{exp}  {flag}")
    print("\nSYSTEM CHECK:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
