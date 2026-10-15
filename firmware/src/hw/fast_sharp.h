#pragma once
// Adafruit_SharpMem with every drawing primitive written straight into the frame buffer through
// Bitmap1 (gfx/bitmap1.h). Output is bit-identical to the library's generic per-pixel path, including
// its odd handling of zero and negative widths/heights: Adafruit_GFX draws such a span as a Bresenham
// line from x + w - 1 to x, so w = 0 covers two pixels. gfxRange() reproduces that, including the
// int16 truncation of x + w - 1.
#include <Adafruit_SharpMem.h>
#include "gfx/bitmap1.h"

// The inclusive pixel range [lo, hi] that GFX's writeLine covers for a span of n from p.
static inline void gfxRange(int16_t p, int16_t n, int& lo, int& hi) {
  int16_t q = (int16_t)(p + n - 1);
  lo = p < q ? p : q;
  hi = p < q ? q : p;
}

class FastSharp : public Adafruit_SharpMem {
 public:
  FastSharp(SPIClass* spi, uint8_t cs, uint16_t w, uint16_t h, uint32_t freq)
      : Adafruit_SharpMem(spi, cs, w, h, freq) {}

  void drawPixel(int16_t x, int16_t y, uint16_t c) override { bm().pixel(x, y, mode(c)); }
  void writePixel(int16_t x, int16_t y, uint16_t c) override { bm().pixel(x, y, mode(c)); }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { hl(x, y, w, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { hl(x, y, w, c); }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { vl(x, y, h, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { vl(x, y, h, c); }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { fr(x, y, w, h, c); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { fr(x, y, w, h, c); }
  void fillScreen(uint16_t c) override { fr(0, 0, width(), height(), c); }

 private:
  Bitmap1 bm() { return Bitmap1{getBuffer(), 400, 240, 50, getRotation()}; }
  static DrawMode mode(uint16_t c) { return c ? DRAW_SET : DRAW_CLEAR; }
  void hl(int16_t x, int16_t y, int16_t w, uint16_t c) {
    int lo, hi; gfxRange(x, w, lo, hi);
    bm().hline(lo, hi, y, mode(c));
  }
  void vl(int16_t x, int16_t y, int16_t h, uint16_t c) {
    int lo, hi; gfxRange(y, h, lo, hi);
    bm().vline(x, lo, hi, mode(c));
  }
  void fr(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0) return;                       // GFX loops over columns i < x + w
    int lo, hi; gfxRange(y, h, lo, hi);
    bm().rectIncl(x, lo, x + w - 1, hi, mode(c));
  }
};
