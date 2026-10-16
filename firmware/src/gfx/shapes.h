#pragma once
// Lines, circles and rings for Bitmap1, written straight into the bytes (no virtual calls).
//
// Why: Adafruit_GFX draws lines and circle outlines one virtual drawPixel call at a time (about
// 1.1 us each on the nRF52840) and has no ring. These produce exactly the pixels Adafruit_GFX would
// (line, circle) or a documented set (ring), so a view can switch to them without the picture changing.
//
//   line(screen, 10, 10, 90, 40, DRAW_SET);          // same pixels as Adafruit_GFX::drawLine
//   circle(screen, 120, 100, 50, DRAW_SET);          // same pixels as Adafruit_GFX::drawCircle
//   fillRing(screen, 120, 100, 50, 40, Gray(13));    // dithered annulus, 40 < d <= 50
//   fillDisc(screen, 120, 100, 50, DRAW_INVERT);     // filled disc
//
// A ring is every pixel whose distance d from the centre satisfies Ri^2 < dx^2 + dy^2 <= R^2, plus the
// two pixels (cx, cy +- Ri) (a quirk of the span-per-row construction the first version used, kept so
// existing screens do not change). Ri = 0 is the disc dx^2 + dy^2 <= R^2. It is clipped to the
// inclusive box `clip` as well as to the bitmap. It is built from the longest runs the bitmap's
// rotation allows: spans along native rows, which are whole-byte writes, instead of one native row per
// pixel.
#include "gfx/bitmap1.h"
#include "gfx/gray.h"

struct ClipBox {
  int x0 = -100000, y0 = -100000, x1 = 100000, y1 = 100000;     // inclusive
  ClipBox() {}
  ClipBox(int ax0, int ay0, int ax1, int ay1) : x0(ax0), y0(ay0), x1(ax1), y1(ay1) {}
};

void line(const Bitmap1& bm, int x0, int y0, int x1, int y1, DrawMode m);
void circle(const Bitmap1& bm, int cx, int cy, int r, DrawMode m);

void fillRing(const Bitmap1& bm, int cx, int cy, int R, int Ri, DrawMode m, const ClipBox& clip = ClipBox());
void fillRing(const Bitmap1& bm, int cx, int cy, int R, int Ri, Gray g, const ClipBox& clip = ClipBox());
inline void fillDisc(const Bitmap1& bm, int cx, int cy, int R, DrawMode m, const ClipBox& clip = ClipBox()) {
  fillRing(bm, cx, cy, R, 0, m, clip);
}
inline void fillDisc(const Bitmap1& bm, int cx, int cy, int R, Gray g, const ClipBox& clip = ClipBox()) {
  fillRing(bm, cx, cy, R, 0, g, clip);
}
