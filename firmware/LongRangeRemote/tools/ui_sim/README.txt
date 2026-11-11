Host simulation of the on-device UI: the real ui.cpp drawn with the real U8g2 fonts into a
400x300 buffer, driven by scripted key presses (script_*.h), with a small fake code database.
  ./build.sh script_walkthrough.h out        -> out/NN_name.png and out/sheet.png
The scripts also check behaviour (a code is sent, Wi-Fi is saved, ...): "checks ok" at the end.
