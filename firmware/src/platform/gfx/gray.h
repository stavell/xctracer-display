#pragma once
// Pseudo-grey on a 1-bit bitmap by ordered dithering (8x8 Bayer matrix), for Bitmap1.
//
// Gray(level): 0 = black ... 16 = white, 17 steps. A pixel is white when its Bayer threshold
// (0..63) is below 4 * level, so level 0 equals DRAW_CLEAR, level 16 equals DRAW_SET, and level 8
// lights exactly 32 of the 64 pixels of every aligned 8x8 block.
//
// The threshold comes from SCREEN coordinates (the logical x, y you draw with, after rotation), never
// from the shape's origin: grey areas that touch join without a seam, and a shape moved by one pixel
// keeps the same pattern under it instead of crawling.
//
//   fillRect(screen, 10, 10, 100, 40, Gray(8));          // 50% grey
//   hspan(screen, 0, 5, 240, Gray(3));
//   static const uint8_t img[] = {...};                  // 4 bits per pixel, high nibble = left pixel
//   Gray4 spr{img, 16, 16, 8};                           // buf, w, h, stride in bytes
//   blitGray(screen, 50, 60, spr, 0, 0, 16, 16, 0);      // nibble value 0 is transparent
//
// Grey sprites: nibble n (0..15) is drawn as level (16 n + 7) / 15, so 0 is black and 15 is white.
// host/img2gray.py makes such arrays from PNGs. Rows are written a byte at a time.
#include "platform/gfx/bitmap1.h"

struct Gray {
  uint8_t level;
  explicit Gray(int l) : level((uint8_t)(l < 0 ? 0 : l > 16 ? 16 : l)) {}
};

// Bayer threshold 0..63 at a screen position, from a 64-entry table (kBayer8, in gray.cpp). The closed
// form is v = sum over i<3 of (((x^y)>>i)&1) << (5-2i) | ((y>>i)&1) << (4-2i); the tests check both.
extern const uint8_t kBayer8[64];
inline int bayer8(int x, int y) {
  GFX_TALLY(bayer, 1);
  return kBayer8[((y & 7) << 3) | (x & 7)];
}

void rectIncl(const Bitmap1& bm, int x0, int y0, int x1, int y1, Gray g);   // inclusive corners
inline void hline(const Bitmap1& bm, int x0, int x1, int y, Gray g) { rectIncl(bm, x0, y, x1, y, g); }
inline void vline(const Bitmap1& bm, int x, int y0, int y1, Gray g) { rectIncl(bm, x, y0, x, y1, g); }
inline void hspan(const Bitmap1& bm, int x, int y, int len, Gray g) { hline(bm, x, x + len - 1, y, g); }
inline void vspan(const Bitmap1& bm, int x, int y, int len, Gray g) { vline(bm, x, y, y + len - 1, g); }
inline void fillRect(const Bitmap1& bm, int x, int y, int w, int h, Gray g) {
  if (w > 0 && h > 0) rectIncl(bm, x, y, x + w - 1, y + h - 1, g);
}

// A 4-bit-per-pixel grey image: pixel (x, y) is the high nibble (x even) or low nibble (x odd) of
// byte y * stride + x / 2.
struct Gray4 {
  const uint8_t* buf;
  int16_t w, h;
  uint16_t stride;
};

// Copy (sx, sy, w, h) of src to (x, y) of dst, dithered on screen coordinates and clipped on both
// sides. Source nibbles equal to `transparent` (0..15, or -1 for none) are skipped.
void blitGray(const Bitmap1& dst, int x, int y, const Gray4& src, int sx, int sy, int w, int h,
              int transparent = -1);
