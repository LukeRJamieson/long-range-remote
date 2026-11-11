#!/bin/bash
# Host build of the real ui.cpp against stubs: scripted key presses in, screenshots out.
#   ./build.sh script_walkthrough.h out
# First run fetches U8g2's C sources and builds them (needs git, gcc, python3 + Pillow).
set -e
cd "$(dirname "$0")"
SCRIPT=${1:-script_walkthrough.h}; OUT=${2:-out}; FW=../..
if [ ! -f libu8g2.a ]; then
  [ -d u8g2 ] || { git clone -q --depth 1 --filter=blob:none --sparse https://github.com/olikraus/u8g2.git && (cd u8g2 && git sparse-checkout set csrc >/dev/null); }
  (cd u8g2/csrc && gcc -O1 -c -w *.c -I. && ar rcs ../../libu8g2.a *.o && rm -f *.o)
fi
mkdir -p "$OUT"
cp "$FW/ui.cpp" ui_build.cpp      # so its #include "ST7305_U8g2.h" finds the stub, not the real driver
g++ -std=gnu++17 -O0 -w -DSCRIPT="\"$SCRIPT\"" -DOUTDIR="\"$OUT\"" -Istub -I. -Iu8g2/csrc -I"$FW" main.cpp stubs.cpp disp.cpp ui_build.cpp libu8g2.a -o uisim_bin
./uisim_bin
python3 -c "
from PIL import Image; import glob
for f in sorted(glob.glob('$OUT/*.pgm')): Image.open(f).save(f[:-3]+'png')"
python3 sheet.py "$OUT" "$OUT/sheet.png"
