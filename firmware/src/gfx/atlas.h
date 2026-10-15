#pragma once
// Atlas: a 1-bit image stored in the NATIVE orientation of a destination bitmap with a given rotation.
//
// Why: Bitmap1::blit can only move whole bytes when both bitmaps have rotation 0. The panel runs in
// portrait (rotation 1), so every logical row of a sprite or glyph is a native column and would be
// drawn pixel by pixel. An Atlas is built once for the rotation it will be drawn at; drawing it to a
// Bitmap1 of that rotation is then a native-to-native copy: whole bytes per native row, shifted and
// masked at the edges. Same rotation, byte-identical result to drawing every set pixel with pixel().
//
// A set pixel of the atlas is "ink"; draw() only touches ink (the background is transparent):
//   DRAW_SET     ink becomes white     DRAW_CLEAR   ink becomes black (black text on white)
//   DRAW_INVERT  ink flips             DRAW_COPY    acts as DRAW_SET (as everywhere in Bitmap1)
// Clipped at every edge of the destination. If the destination's rotation differs from the atlas's,
// draw() falls back to one pixel() call per ink pixel: correct, just slower.
//
// Pure C++, no allocation: the caller owns the memory (Atlas::bytesFor bytes, any alignment).
//
// Use (a sprite for a game, drawn on the portrait panel):
//   static uint8_t mem[8];                                    // >= Atlas::bytesFor(8, 8, 1)
//   Atlas ship;
//   ship.build(mem, 8, 8, 1, [](void*, int x, int y) { return x == y || x + y == 7; }, nullptr);
//   ship.draw(screen, 100, 50, DRAW_CLEAR);                   // screen is a Bitmap1 with rotation 1
#include <stddef.h>
#include <stdint.h>
#include "gfx/bitmap1.h"

// Logical pixel (x, y) of the image, 0 <= x < w, 0 <= y < h: true is ink.
typedef bool (*AtlasPixelFn)(void* ctx, int x, int y);

struct Atlas {
  const uint8_t* data = nullptr;
  int16_t lw = 0, lh = 0;        // logical size
  int16_t nw = 0, nh = 0;        // native size (swapped for rotations 1 and 3)
  uint16_t stride = 0;           // bytes per native row
  uint8_t rot = 0;               // rotation the image is stored for

  static size_t bytesFor(int lw, int lh, int rot);
  // Render into mem (bytesFor bytes, cleared here). Returns false for a non-positive size.
  bool build(uint8_t* mem, int lw, int lh, int rot, AtlasPixelFn px, void* ctx);

  // An atlas over bytes that were already built for (lw, lh, rot), e.g. kept in a compact table.
  static Atlas over(const uint8_t* mem, int lw, int lh, int rot);

  uint8_t get(int x, int y) const;                           // logical pixel, 0 outside
  // Draw with the image's logical (0, 0) at logical (x, y) of dst.
  void draw(const Bitmap1& dst, int x, int y, DrawMode m) const;
};
