#pragma once
extern "C" {
#include "u8g2.h"
}
#define U8G2_R1 nullptr
class U8G2 {
 public:
  u8g2_t u;
  void setFont(const uint8_t *f) { u8g2_SetFont(&u, f); }
  void setDrawColor(uint8_t c) { u8g2_SetDrawColor(&u, c); }
  void setFontMode(uint8_t m) { u8g2_SetFontMode(&u, m); }
  int drawStr(int x, int y, const char *s) { return u8g2_DrawStr(&u, x, y, s); }
  int getStrWidth(const char *s) { return u8g2_GetStrWidth(&u, s); }
  void drawBox(int x, int y, int w, int h) { u8g2_DrawBox(&u, x, y, w, h); }
  void drawFrame(int x, int y, int w, int h) { u8g2_DrawFrame(&u, x, y, w, h); }
  void drawRBox(int x, int y, int w, int h, int r) { u8g2_DrawRBox(&u, x, y, w, h, r); }
  void drawRFrame(int x, int y, int w, int h, int r) { u8g2_DrawRFrame(&u, x, y, w, h, r); }
  void drawHLine(int x, int y, int w) { u8g2_DrawHLine(&u, x, y, w); }
  void drawVLine(int x, int y, int h) { u8g2_DrawVLine(&u, x, y, h); }
  void drawLine(int a, int b, int c, int d) { u8g2_DrawLine(&u, a, b, c, d); }
  void drawPixel(int x, int y) { u8g2_DrawPixel(&u, x, y); }
  void drawDisc(int x, int y, int r) { u8g2_DrawDisc(&u, x, y, r, U8G2_DRAW_ALL); }
  void drawCircle(int x, int y, int r) { u8g2_DrawCircle(&u, x, y, r, U8G2_DRAW_ALL); }
  void drawTriangle(int a, int b, int c, int d, int e, int f) { u8g2_DrawTriangle(&u, a, b, c, d, e, f); }
  void clearBuffer() { u8g2_ClearBuffer(&u); }
  void sendBuffer();
};
class ST7305_U8g2 {
 public:
  U8G2 g;
  ST7305_U8g2(int, int, int, int, int) {}
  void begin(int, const void *);
  U8G2 *getU8g2() { return &g; }
};
