// ir_protocols.h - IR protocol encoders for the HifiRemote universal remote.
//
// Every encoder turns (protocol, D, S, F, toggle) into two timing sequences:
//   * `first`  - what is sent on the initial key press
//   * `repeat` - what is sent (repeatedly) while the key is held
// Durations are in microseconds, alternating MARK (carrier on) / SPACE (off),
// always starting with a MARK and ending with a SPACE that includes the
// protocol's inter-frame gap, so frames can be sent back-to-back.
//
// Timings follow the IRP notation definitions from IrpProtocols.xml
// (http://www.harctoolbox.org/IrpProtocols.html). This file has no Arduino
// dependencies so it can be unit-tested on a PC (see tools/test_encoders.cpp).
#pragma once
#include <stdint.h>
#include <stddef.h>

// Keep in sync with PROTOS[] in tools/build_db.py
enum IrProto : uint8_t {
  IRP_RAW = 0,     // captured timings (D = index into raw table)
  IRP_NEC,         // NEC1: D = byte0|byte1<<8, F = byte2|byte3<<8, "ditto" repeat   (Yamaha, Onkyo, NAD, HK, Edifier...)
  IRP_NEC2,        // NEC2: as NEC but the whole frame repeats                      (many Onkyo/Integra, Pioneer AVR)
  IRP_NECX,        // NECx / Samsung32: 4.5 ms header                                (Samsung soundbars, LG)
  IRP_PIONEER,     // NEC2 framing on a 40 kHz carrier                               (Pioneer)
  IRP_AIWA,        // NEC42 / Aiwa: D = 13-bit address, F = 8-bit command            (Aiwa, some Sanyo)
  IRP_KASEIKYO,    // 48-bit Kaseikyo: D = vendor id, S = byte2, F = byte3|byte4<<8  (Panasonic, Technics, Denon-K, JVC-48)
  IRP_SONY12,      // Sony SIRC 12-bit: D 5, F 7                                     (Sony)
  IRP_SONY15,      // Sony SIRC 15-bit: D 8, F 7
  IRP_SONY20,      // Sony SIRC 20-bit: D 13 (device | subdevice<<5), F 7
  IRP_RC5,         // Philips RC5: D 5, F 7 (F >= 64 is "RC5X")                      (Philips, Marantz, Cambridge, Arcam, Naim, Rega, Musical Fidelity)
  IRP_RC5X,        // Philips/Marantz 20-bit extended RC5: D 5, S 7, F 6             (Marantz, Philips)
  IRP_RC6,         // Philips RC6 mode 0: D 8, F 8                                   (Philips, Linn, some Arcam)
  IRP_DENON,       // Denon/Sharp 15-bit: D 5, F 8, frame + inverted frame           (Denon, Marantz (newer), Sharp)
  IRP_DENON1,      // Denon first half only
  IRP_DENON2,      // Denon inverted half only
  IRP_SHARP,       // Sharp 15-bit (expansion bits 01/10)
  IRP_JVC,         // JVC: D 8, F 8                                                  (JVC, Kenwood-JVC)
  IRP_JVC2,        // JVC frames without leader
  IRP_MITSUBISHI,  // Mitsubishi: D 8, F 8                                           (Mitsubishi, old Kenwood)
  IRP_RECS80,      // Philips RECS80: D 3, F 6                                       (older Philips/Thomson hi-fi)
  IRP_BOSE,        // Bose: F 8                                                      (Bose Wave, Lifestyle, 3-2-1)
  IRP_RCA,         // RCA/Thomson 56-58 kHz: D 4, F 8                                (RCA)
  IRP_RCA38,       // RCA framing on 38.7 kHz
  IRP_TEACK,       // Teac-K (Kaseikyo framing, Teac checksum): D 4, S 8, F 8        (TEAC, Esoteric)
  IRP_AKAI,        // Akai: D 3, F 7                                                 (Akai)
  IRP_EMERSON,     // Emerson: D 6, F 6
  IRP_RCA38OLD,    // Early RCA-38 (long leader)
  IRP__COUNT
};

// A code as stored in the database / typed in by the user.
struct IrCodeParams {
  uint8_t  proto;
  uint16_t d, s, f;
};

// Output buffer for an encoded signal.
struct IrSignal {
  static const size_t MAX_TIMINGS = 400;   // enough for 2x Denon frames; raw signals are streamed separately
  uint32_t carrier_hz = 38000;
  uint8_t  duty_pct   = 33;
  uint16_t min_frames = 1;                 // frames to send on a quick tap (first + (min_frames-1) repeats)
  uint32_t first[MAX_TIMINGS];  uint16_t n_first = 0;
  uint32_t repeat[MAX_TIMINGS]; uint16_t n_repeat = 0;
};

// Human-readable protocol name, e.g. "NEC", "Sony12".
const char *irProtoName(uint8_t proto);
// Parse a protocol name (case-insensitive, accepts IRP aliases like "NEC1", "Sony12", "Panasonic",
// "Denon-K", "JVC-48", "NEC1-y1"...). Returns true and fills `out` with the firmware protocol and
// converted D/S/F (applying family checksums) for manual code entry.
bool irParseManual(const char *name, int d, int s, int f, IrCodeParams &out);

// Encode a parametric code. `toggle` is the RC5/RC6/RECS80 toggle bit (flip it on each new key press).
// Returns false for IRP_RAW (raw signals are handled by the database/transmitter) or bad protocol.
bool irEncode(const IrCodeParams &code, bool toggle, IrSignal &out);
