// Scripted run of the real ui.cpp: key presses in, screenshots out.
#include <Arduino.h>
#include "config.h"
#include "ui.h"
#include "settings.h"
#include "sim.h"
#include "discover.h"
void dumpPgm(const char *path);
static int shot_n = 0;
void run(int ms) { for (int t = 0; t < ms; t += 5) { sim_ms += 5; ui::loop(); discover::loop(); } }
void shot(const char *name) { run(30); char p[200]; snprintf(p, sizeof(p), OUTDIR "/%02d_%s.pgm", ++shot_n, name); dumpPgm(p); }
int bit(char k) { switch (k) { case 'U': return KEYBIT_UP; case 'D': return KEYBIT_DOWN; case 'L': return KEYBIT_LEFT; case 'R': return KEYBIT_RIGHT; case 'S': return KEYBIT_SELECT; default: return KEYBIT_BACK; } }
void press(char k, int hold = 80) { sim.keys = 1u << bit(k); run(hold); sim.keys = 0; run(80); }
void seq(const char *s) { for (; *s; s++) press(*s); }
#include SCRIPT
