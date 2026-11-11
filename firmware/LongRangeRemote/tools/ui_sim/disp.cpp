#include "ST7305_U8g2.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static u8x8_display_info_t info;
static uint8_t cb(u8x8_t *u8x8, uint8_t msg, uint8_t, void *) {
  if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) { u8x8_d_helper_display_setup_memory(u8x8, &info); return 1; }
  return 1;
}
static uint8_t dummy(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
static uint8_t buf[50 * 8 * 38];
static U8G2 *G;
void ST7305_U8g2::begin(int, const void *) {
  memset(&info, 0, sizeof(info));
  info.tile_width = 50; info.tile_height = 38; info.pixel_width = 400; info.pixel_height = 300;
  u8g2_SetupDisplay(&g.u, cb, dummy, dummy, dummy);
  u8g2_SetupBuffer(&g.u, buf, 38, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  G = &g;
}
void U8G2::sendBuffer() {}
void dumpPgm(const char *path) {      // 3x scaled-free PGM, LCD-like colours
  FILE *f = fopen(path, "wb"); fprintf(f, "P5\n400 300\n255\n");
  for (int y = 0; y < 300; y++) for (int x = 0; x < 400; x++) {
    int on = buf[(y / 8) * 400 + x] >> (y % 8) & 1; fputc(on ? 25 : 200, f); }
  fclose(f);
}
