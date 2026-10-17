#!/usr/bin/env python3
"""
ngspice transient simulations of the control board (power gate, LED switch) and the 66-LED head.

Models (fitted to datasheet figures, not vendor SPICE models):
  Q5  IPD80P03P4L-07  P-ch VDMOS, Vth -1.5 V (datasheet -1.0..-2.0), ~9 mOhm at Vgs -4.5 V (12 max), Ciss ~4.4 nF
  Q2  AOD424          N-ch VDMOS, Vth 1.0 V (0.5..1.6), ~5 mOhm at Vgs 2.5-3.3 V (5.7 max at 2.5 V), Ciss ~3.9 nF
  Q6  2N3904          standard Gummel-Poon model
  GPIO17              ESP32-S3 output: 3.3 V through RGPIO (~20 ohm at drive strength 3, ~40 ohm at 2)
  Ribbon  10-way, 6": 12 mOhm + 60 nH each way (120 nH loop: pessimistic, the conductors alternate)
  LEDs SFH 4545       diode fitted to Vf 1.5 V @ 100 mA and 2.3 V @ 1 A; 33 strings of 2 + 24 ohm (area 33)
  Pack 4 x AA NiMH    ideal source + 0.15 ohm (cells + holder springs)
Run: python3 sim/control_lite_sim.py   (needs ngspice)
"""
import re
import subprocess
import sys
import tempfile

MODELS = """
.model PFET VDMOS(pchan Vto=-1.5 Kp=60 Rd=2m Rs=1m Cgdmax=1.5n Cgdmin=0.25n Cgs=4n Cjo=1.5n Is=1e-12 Rb=3m)
.model NFET VDMOS(Vto=1.0 Kp=150 Rd=1.5m Rs=1m Cgdmax=1.6n Cgdmin=0.45n Cgs=3.3n Cjo=1.5n Is=1e-12 Rb=3m)
.model Q2N3904 NPN(Is=6.734f Xti=3 Eg=1.11 Vaf=74.03 Bf=416.4 Ne=1.259 Ise=6.734f Ikf=66.78m Xtb=1.5 Br=.7371
+ Nc=2 Isc=0 Ikr=0 Rc=1 Cjc=3.638p Mjc=.3085 Vjc=.75 Fc=.5 Cje=4.493p Mje=.2593 Vje=.75 Tr=239.5n Tf=301.2p
+ Itf=.4 Vtf=4 Xtf=2 Rb=10)
.model DLED D(Is=1.14e-13 N=2 Rs=0.76 Cjo=30p Bv=5 Ibv=10u)
"""


R6, RGPIO = 47, 40            # gate resistor on the board; ESP32-S3 GPIO17 at drive strength 2


def deck(vpack, t_cells, t_arm, t_ir0, t_ir1, t_disarm, t_stop, ir_without_arm=False):
    arm = f"PWL(0 0 {t_arm} 0 {t_arm + 1e-6} 3.3 {t_disarm} 3.3 {t_disarm + 1e-6} 0)"
    if ir_without_arm:
        arm = "DC 0"
    period = 1 / 38000
    ir = (f"PULSE(0 3.3 {t_ir0} 20n 20n {period / 2} {period})")
    return f"""* control board lite
{MODELS}
* AA pack: switched in at t_cells (cells going in)
Vcell cell 0 PWL(0 0 {t_cells} 0 {t_cells + 10e-6} {vpack})
Rint cell batt 0.15
* pack sense
R12 batt sense 470k
R13 sense 0 470k
C6 sense 0 100n
* power gate
MQ5 vbat pwr_g batt batt PFET
R14 batt pwr_g 100k
C8 batt pwr_g 1u
C7 pwr_g vbat 100n
R15 pwr_g pwr_sw 10k
QQ6 pwr_sw q6_b 0 Q2N3904
R16 arm q6_b 10k
R4 arm 0 100k
Varm armsrc 0 {arm}
Rarm armsrc arm 50
* reservoir + decoupling (470 uF x 2, ~50 mOhm ESR each)
C1 vbat c1e 470u
Rc1 c1e 0 0.05
C2 vbat c2e 470u
Rc2 c2e 0 0.05
C3 vbat 0 100n
C4 vbat 0 1u
C5 vbat 0 100n
* IR drive: ESP32 GPIO17 -> R6 -> Q2 gate (R7 holds it low at boot)

Virp irp 0 {ir}
Bir irgate 0 V = (time > {t_ir0} && time < {t_ir1}) ? V(irp) : 0
Rgpio irgate ir_drv {RGPIO}
R6 ir_drv q2_g {R6 + 1.4}
R7 q2_g 0 100k
MQ2 led_rtn_b q2_g 0 0 NFET
* ribbon + LED head (33 strings: 2 x SFH 4545 + 24 ohm)
Rrib1 vbat rl1 0.012
Lrib1 rl1 led_pwr 60n
DA led_pwr mid1 DLED area=33
DB mid1 mid2 DLED area=33
RS mid2 led_rtn {24 / 33}
Rrib2 led_rtn rl2 0.012
Lrib2 rl2 led_rtn_b 60n
.options method=gear reltol=1e-3 abstol=1e-9 vntol=1e-5
.tran 0.5u {t_stop} 0 20u
.control
run
let ipack = -i(Vcell)
let iled = (v(mid2) - v(led_rtn)) / {24 / 33} / 33
let vgs5 = v(pwr_g) - v(batt)
meas tran pk_ipack MAX ipack
meas tran pk_vbat MAX v(vbat)
meas tran pk_iled MAX iled
meas tran vbat_end FIND v(vbat) AT={t_ir0 - 1e-4}
meas tran vbatt_end FIND v(batt) AT={t_ir0 - 1e-4}
meas tran vgs5_on FIND vgs5 AT={t_ir0 - 1e-4}
meas tran vbat_min_tx MIN v(vbat) FROM={t_ir0} TO={t_ir1}
meas tran vgate_q2 MAX v(q2_g) FROM={t_ir0} TO={t_ir1}
meas tran iled_avg AVG iled FROM={t_ir0 + 0.002} TO={t_ir1}
meas tran ipack_avg_tx AVG ipack FROM={t_ir0 + 0.002} TO={t_ir1}
meas tran vbat_t95 WHEN v(vbat)={0.95 * vpack * 0.97} RISE=1
meas tran sense_v FIND v(sense) AT={t_ir0 - 1e-4}
meas tran vbat_after FIND v(vbat) AT={t_stop - 1e-3}
meas tran pk_ins MAX ipack FROM=0 TO={t_arm - 1e-4}
meas tran pk_vbat_ins MAX v(vbat) FROM=0 TO={t_arm - 1e-4}
meas tran pk_arm MAX ipack FROM={t_arm} TO={t_ir0 - 1e-4}
meas tran t_off WHEN v(vbat)=1.0 FALL=1
meas tran vgs5_after FIND vgs5 AT={t_stop - 1e-3}
meas tran iled_preir MAX iled FROM=0 TO={t_ir0}
let vrev = v(led_rtn) - v(led_pwr)
meas tran led_rev MAX vrev FROM={t_ir0} TO={t_ir1}
meas tran q2_vds MAX v(led_rtn_b) FROM={t_ir0} TO={t_ir1}
quit
.endc
.end
"""


def run(name, **kw):
    with tempfile.NamedTemporaryFile("w", suffix=".cir", delete=False) as f:
        f.write(deck(**kw))
    r = subprocess.run(["ngspice", "-b", f.name], capture_output=True, text=True, timeout=900)
    res = dict(re.findall(r"^(\w+)\s*=\s*([-+0-9.eE]+)", r.stdout, re.M))
    if not res:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit(name + ": no results")
    return {k: float(v) for k, v in res.items()}


CASES = [
    ("fresh NiMH (5.6 V)", dict(vpack=5.6)),
    ("cutoff (4.4 V)", dict(vpack=4.4)),
    ("alkaline, misuse (6.4 V)", dict(vpack=6.4)),
]


def main():
    base = dict(t_cells=1e-3, t_arm=20e-3, t_ir0=50e-3, t_ir1=80e-3, t_disarm=80e-3, t_stop=0.45)
    for name, kw in CASES:
        r = run(name, **base, **kw)
        print(f"\n== {name}")
        print(f"  cells in, ARM low : surge {r['pk_ins']:.3f} A, VBAT up to {r['pk_vbat_ins']:.3f} V, LED current before IR {r['iled_preir']*1e3:.2f} mA/string")
        print(f"  ARM high          : charging surge into C1/C2 {r['pk_arm']:.2f} A peak")
        print(f"  ARM high          : VBAT reaches 95 % at {(r['vbat_t95'] - base['t_arm'])*1e3:.1f} ms after ARM; at IR start VBAT {r['vbat_end']:.3f} V (pack {r['vbatt_end']:.3f} V), Q5 Vgs {r['vgs5_on']:.2f} V")
        print(f"  pack sense        : {r['sense_v']:.3f} V at GPIO1 (pack/2 = {r['vbatt_end']/2:.3f} V)")
        print(f"  38 kHz carrier    : LED peak {r['pk_iled']*1e3:.0f} mA/string, average {r['iled_avg']*1e3:.0f} mA/string, pack avg {r['ipack_avg_tx']:.2f} A, VBAT min {r['vbat_min_tx']:.2f} V, Q2 gate max {r['vgate_q2']:.2f} V (from the 3.3 V GPIO)")
        print(f"  switching         : LED string reverse peak {r['led_rev']:.2f} V (SFH 4545 max 5 V per LED, 2 per string), Q2 Vds peak {r['q2_vds']:.1f} V (AOD424: 20 V max)")
        print(f"  ARM low (+370 ms) : VBAT {r['vbat_after']:.2f} V, Q5 Vgs {r['vgs5_after']:.2f} V" + (f", VBAT below 1 V {(r['t_off'] - base['t_disarm'])*1e3:.0f} ms after ARM low" if 't_off' in r else ""))
    # fault: IR_DRV toggling with ARM low -> LEDs must stay dark
    r = run("fault", vpack=5.6, **dict(base, t_arm=1.0, t_disarm=1.1), ir_without_arm=True)
    print(f"\n== fault: carrier on IR_DRV while ARM is low: LED peak {r['pk_iled']*1e3:.3f} mA/string, VBAT max {r['pk_vbat']:.3f} V, surge at cell insertion {r['pk_ins']:.3f} A")


if __name__ == "__main__":
    main()
