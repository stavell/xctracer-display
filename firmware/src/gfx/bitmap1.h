#pragma once
// Bitmap1: drawing into a 1-bit-per-pixel bitmap, written straight into its bytes.
//
// What it is for: the fast path under the Sharp memory display (the panel buffer is one Bitmap1) and
// a small toolkit for off-screen sprites, tiles and games. Pure C++: no Arduino, no app, no view
// model, no allocation. It does not own its memory.
//
// Layout (the panel's): row-major in native rows of `stride` bytes; pixel (x, y) is bit (x & 7) of
// byte y * stride + x / 8, so the LSB is the leftmost pixel. A set bit is "white" (colour 1), a clear
// bit is "black" (colour 0). `w`, `h` are the NATIVE size; `rotation` (0..3, the Adafruit_GFX
// convention) says how the logical coordinates you draw with map onto it, and rotations 1 and 3 swap
// the logical width and height. Everything is clipped to the logical size; nothing writes outside
// the buffer.
//
// Use:
//   static uint8_t sprite[8 * 1];                     // an 8x8 sprite: stride 1, 8 rows
//   Bitmap1 spr{sprite, 8, 8, 1, 0};
//   Bitmap1 screen{panelBuffer, 400, 240, 50, 1};     // the panel, used in portrait (240x400)
//   spr.fill(DRAW_CLEAR);                             // all black
//   spr.fillRect(2, 2, 4, 4, DRAW_SET);
//   screen.hline(0, 239, 10, DRAW_INVERT);            // XOR: draw twice to erase
//   Bitmap1::blit(screen, 100, 50, spr, 0, 0, 8, 8, DRAW_COPY);               // opaque copy
//   Bitmap1::blit(screen, 100, 50, spr, 0, 0, 8, 8, DRAW_INVERT, SKIP_BLACK); // XOR the white pixels
//   Bitmap1::blit(screen, x, y, sheet, 16, 0, 16, 16, DRAW_COPY, KEY_NONE, &maskSheet);  // masked
//
// Modes: DRAW_SET writes white, DRAW_CLEAR black, DRAW_INVERT flips (XOR). DRAW_COPY is for blit
// (destination = source pixel); anywhere else it acts as DRAW_SET.
// Coordinates are int so sums like x + w never overflow; widths and heights <= 0 draw nothing.
#include <stddef.h>
#include <stdint.h>

enum DrawMode : uint8_t { DRAW_SET, DRAW_CLEAR, DRAW_INVERT, DRAW_COPY };
// Source pixels that blit leaves alone.
enum BlitKey : uint8_t { KEY_NONE, SKIP_BLACK, SKIP_WHITE };

struct Bitmap1 {
  uint8_t* buf;
  int16_t w, h;          // native size in pixels
  uint16_t stride;       // bytes per native row, >= (w + 7) / 8
  uint8_t rotation;      // 0..3

  int width() const { return (rotation & 1) ? h : w; }       // logical size
  int height() const { return (rotation & 1) ? w : h; }

  // Single pixel, inline: text and lines call this once per pixel, so it skips the rect machinery.
  void pixel(int x, int y, DrawMode m) const {
    if ((unsigned)x >= (unsigned)width() || (unsigned)y >= (unsigned)height()) return;
    int nx, ny;
    switch (rotation & 3) {
      case 1: nx = w - 1 - y; ny = x; break;
      case 2: nx = w - 1 - x; ny = h - 1 - y; break;
      case 3: nx = y; ny = h - 1 - x; break;
      default: nx = x; ny = y; break;
    }
    uint8_t& b = buf[(unsigned)ny * stride + (nx >> 3)];
    uint8_t bit = (uint8_t)(1u << (nx & 7));
    if (m == DRAW_CLEAR) b &= (uint8_t)~bit;
    else if (m == DRAW_INVERT) b ^= bit;
    else b |= bit;
  }
  uint8_t getPixel(int x, int y) const;                       // 0 outside
  void hline(int x0, int x1, int y, DrawMode m) const;        // inclusive; x0 > x1 draws nothing
  void vline(int x, int y0, int y1, DrawMode m) const;
  void rectIncl(int x0, int y0, int x1, int y1, DrawMode m) const;   // inclusive corners
  void hspan(int x, int y, int len, DrawMode m) const { hline(x, x + len - 1, y, m); }
  void vspan(int x, int y, int len, DrawMode m) const { vline(x, y, y + len - 1, m); }
  void fillRect(int x, int y, int rw, int rh, DrawMode m) const {
    if (rw > 0 && rh > 0) rectIncl(x, y, x + rw - 1, y + rh - 1, m);
  }
  void fill(DrawMode m) const { rectIncl(0, 0, width() - 1, height() - 1, m); }

  // Copy the rectangle (sx, sy, w, h) of src to (x, y) of dst, clipped on both sides. A source pixel
  // is skipped when it matches `key`, or when `mask` (same coordinates as src, same logical size or
  // larger) is black there. Each remaining pixel is combined as `m` says. dst and src must not share
  // memory. Whole bytes are written when both bitmaps have rotation 0.
  static void blit(const Bitmap1& dst, int x, int y, const Bitmap1& src, int sx, int sy, int w, int h,
                   DrawMode m, BlitKey key = KEY_NONE, const Bitmap1* mask = nullptr);

  // Native coordinates of a logical pixel (no clipping).
  void toNative(int x, int y, int& nx, int& ny) const;

 private:
  void nativeRect(int nx0, int ny0, int nx1, int ny1, DrawMode m) const;
};
