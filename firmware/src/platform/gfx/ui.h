#pragma once
// Small 1-bit UI kit shared by the box's own screens (home, settings, calibration): three fonts, the black title
// bar with a battery, rounded boxes, wrapped text. Portrait 240x400, drawn straight into a Bitmap1.
// Colours follow Bitmap1: DRAW_CLEAR is black ink, DRAW_SET is white.
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"

struct UiFonts {
  Font1 small, bold, mid;                           // Sans 9, SansBold 9, SansBold 12
  void ensure(int rotation);
};

namespace ui {

const int W = 240, H = 400, BAR_H = 32;

inline DrawMode other(DrawMode m) { return m == DRAW_SET ? DRAW_CLEAR : DRAW_SET; }

// Filled box with the corners cut by r pixels.
void roundFill(const Bitmap1& bm, int x, int y, int w, int h, int r, DrawMode m);
// Outline of the given thickness inside the box.
void roundFrame(const Bitmap1& bm, int x, int y, int w, int h, int r, int thick, DrawMode m);
// Battery glyph with the percentage inside the fill level, ink on whatever is behind. pct < 0: outline only.
void battery(const Bitmap1& bm, int x, int y, int pct, DrawMode ink);
// Black bar across the top: title left (white), battery glyph and percentage right.
void titleBar(const Bitmap1& bm, UiFonts& f, const char* title, int battPct);
// Text centred on cx.
void centered(const Bitmap1& bm, const Font1& f, int cx, int baseline, const char* s, DrawMode m);
// Word-wrapped text from (x, baseline) in a column w wide; returns the baseline after the last line.
int wrapped(const Bitmap1& bm, const Font1& f, int x, int baseline, int w, int lineH, const char* s, DrawMode m);
// Horizontal progress bar: frame plus fill, frac 0..1.
void bar(const Bitmap1& bm, int x, int y, int w, int h, float frac);

}  // namespace ui
