#pragma once
// Font1: Adafruit GFXfont glyphs rendered once into pre-rotated Atlases, drawn with whole-byte copies.
//
// Adafruit_GFX::drawChar calls writePixel once per set bit of the glyph (2.8 ms for "1234" in
// FreeSansBold24pt on the nRF52840). Font1 keeps one Atlas per glyph (ASCII 32..126) for ONE rotation
// and draws a string with drawText(). Metrics and baseline are GFX's, at text size 1: the cursor
// (x, y) is on the baseline, a glyph's ink starts at (x + xOffset, y + yOffset), the cursor then
// advances by xAdvance. Characters outside the font's range are skipped without advancing, '\n'
// returns to x = 0 and moves down by yAdvance, '\r' is ignored (all as GFX's print()).
//
// Wrapping: GFX wraps a glyph to the next line when cursor + xOffset + width > canvas width, and so
// does getTextBounds, so a width measured at the right edge can be a surprise. Pass the canvas width
// as wrapW to reproduce that; 0 (the default) never wraps.
//
// Storage is one block, allocated by build() and SHARED: a Font1 built from the same font for the same rotation as a
// live one points at that one's block (font.cpp keeps the count); nothing is allocated while drawing. Not for the loop's stack: keep Font1 objects static.
//
// Use:
//   static Font1 big;
//   big.build(&FreeSansBold24pt7b, 1);                              // once, for the panel's rotation
//   int xEnd = drawText(screen, 10, 60, "1234", big, DRAW_CLEAR);   // black text
//   int w = textWidth("1234", big);
#include <stddef.h>
#include <stdint.h>
#include <gfxfont.h>      // Adafruit GFX's font structs (plain C, no Arduino)
#include "platform/gfx/atlas.h"

struct Font1 {
  struct Glyph {
    uint16_t off = 0;              // bitmap position in the font's block
    uint8_t w = 0, h = 0;          // 0 when the glyph has no bitmap (space)
    int8_t xo = 0, yo = 0;
    uint8_t xa = 0;                // xAdvance
  };
  static const int FIRST = 32, LAST = 126, COUNT = LAST - FIRST + 1;

  Font1() = default;
  Font1(const Font1&) = delete;
  Font1& operator=(const Font1&) = delete;
  ~Font1();

  // Render font f for destination rotation rot. False if memory ran out (the font is then unusable).
  bool build(const GFXfont* f, int rot);
  bool isBuilt(const GFXfont* f, int rot) const { return glyphs_ && src_ == f && rot_ == (rot & 3); }
  size_t ramBytes() const { return used_; }          // glyph table + all bitmaps (shared with other Font1s of the same font)
  int rotation() const { return rot_; }
  int yAdvance() const { return yAdv_; }
  // nullptr for characters outside 32..126 (or the font's own range).
  const Glyph* glyph(unsigned char c) const;
  Atlas atlas(const Glyph& g) const { return Atlas::over(block_ + g.off, g.w, g.h, rot_); }

 private:
  const GFXfont* src_ = nullptr;
  void release();                  // let go of the shared block
  uint8_t* block_ = nullptr;       // Glyph[COUNT] then the bitmaps (shared, see font.cpp)
  size_t used_ = 0;
  int8_t slot_ = -1;               // index of the shared block, -1 = none
  Glyph* glyphs_ = nullptr;
  uint8_t rot_ = 0;
  uint8_t yAdv_ = 0;
  uint16_t first_ = 0, last_ = 0;
};

// Draws s with its baseline cursor at (x, y); returns the cursor x after the last character.
// bm should have the font's rotation, otherwise it is drawn pixel by pixel (still correct).
int drawText(const Bitmap1& bm, int x, int y, const char* s, const Font1& f, DrawMode m, int wrapW = 0);

// Same numbers as Adafruit_GFX::getTextBounds(s, 0, 0, ...): x1, y1 is the upper left of the ink,
// w, h its size (0 when there is none).
void textBounds(const char* s, const Font1& f, int& x1, int& y1, int& w, int& h, int wrapW = 0);
int textWidth(const char* s, const Font1& f, int wrapW = 0);
