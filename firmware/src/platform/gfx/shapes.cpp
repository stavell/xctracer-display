#include "platform/gfx/shapes.h"
#include <math.h>
#include <stdlib.h>

// Same pixels as Adafruit_GFX::drawLine (axis-parallel lines as spans, otherwise its Bresenham walk).
void line(const Bitmap1& bm, int x0, int y0, int x1, int y1, DrawMode m) {
  if (x0 == x1) { if (y0 > y1) { int t = y0; y0 = y1; y1 = t; } bm.vline(x0, y0, y1, m); return; }
  if (y0 == y1) { if (x0 > x1) { int t = x0; x0 = x1; x1 = t; } bm.hline(x0, x1, y0, m); return; }
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) { int t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
  int dx = x1 - x0, dy = abs(y1 - y0), err = dx / 2, ystep = y0 < y1 ? 1 : -1;
  for (; x0 <= x1; x0++) {
    if (steep) bm.pixel(y0, x0, m); else bm.pixel(x0, y0, m);
    err -= dy;
    if (err < 0) { y0 += ystep; err += dx; }
  }
}

// Same pixels as Adafruit_GFX::drawCircle (midpoint algorithm, eight-fold symmetry).
void circle(const Bitmap1& bm, int cx, int cy, int r, DrawMode m) {
  if (r < 0) return;
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  bm.pixel(cx, cy + r, m);
  bm.pixel(cx, cy - r, m);
  bm.pixel(cx + r, cy, m);
  bm.pixel(cx - r, cy, m);
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++;
    ddx += 2;
    f += ddx;
    bm.pixel(cx + x, cy + y, m);
    bm.pixel(cx - x, cy + y, m);
    bm.pixel(cx + x, cy - y, m);
    bm.pixel(cx - x, cy - y, m);
    bm.pixel(cx + y, cy + x, m);
    bm.pixel(cx - y, cy + x, m);
    bm.pixel(cx + y, cy - x, m);
    bm.pixel(cx - y, cy - x, m);
  }
}

namespace {

// floor(sqrt(n)) exactly, n >= 0
int isqrtInt(int n) {
  int r = (int)sqrtf((float)n);
  while (r * r > n) r--;
  while ((r + 1) * (r + 1) <= n) r++;
  return r;
}

struct PaintMode {
  const Bitmap1& bm; DrawMode m;
  void h(int x0, int x1, int y) const { bm.hline(x0, x1, y, m); }
  void v(int x, int y0, int y1) const { bm.vline(x, y0, y1, m); }
};
struct PaintGray {
  const Bitmap1& bm; Gray g;
  const uint8_t* pats;
  PaintGray(const Bitmap1& b, Gray gr) : bm(b), g(gr), pats(grayRowPatterns(b, gr.level)) {}
  void h(int x0, int x1, int y) const { hline(bm, x0, x1, y, g); }
  // Rings are thousands of short runs: in the portrait rotation a logical column is a run inside one native row, written here
  // without the generic clipping and coordinate conversion (the same bytes as vline(), measured 500 -> ~100 cycles per run).
  void v(int x, int y0, int y1) const {
    if (bm.rotation != 1) { vline(bm, x, y0, y1, g); return; }
    if (x < 0 || x >= bm.h) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= bm.w) y1 = bm.w - 1;
    if (y0 > y1) return;
    const int nx0 = bm.w - 1 - y1, nx1 = bm.w - 1 - y0;       // native columns of the run
    uint8_t* row = bm.buf + (size_t)x * bm.stride;
    const uint8_t pat = pats[x & 7];
    const int b0 = nx0 >> 3, b1 = nx1 >> 3;
    const uint8_t first = (uint8_t)(0xFFu << (nx0 & 7)), last = (uint8_t)(0xFFu >> (7 - (nx1 & 7)));
    if (b0 == b1) { const uint8_t m = first & last; row[b0] = (uint8_t)((row[b0] & ~m) | (pat & m)); return; }
    row[b0] = (uint8_t)((row[b0] & ~first) | (pat & first));
    for (int b = b0 + 1; b < b1; b++) row[b] = pat;
    row[b1] = (uint8_t)((row[b1] & ~last) | (pat & last));
  }
};

// One line of the ring along the axis `a` (the run direction): the centre c on that axis, the offset d
// across it. Emits the part(s) of the ring on that line through `emit(lo, hi)`.
template <class Emit>
void ringLine(bool byRows, int d, int R, int Ri, int c, int lo, int hi, Emit emit) {
  int o = isqrtInt(R * R - d * d);                 // outer half length
  int in = -1;                                     // inner half length excluded; -1 = none
  if (Ri > 0) {
    // The two extra pixels (cx, cy +- Ri) are kept: in a row at dy = +-Ri the whole row is drawn, and
    // in the column dx = 0 the hole stops one short of them.
    if (byRows && abs(d) == Ri) in = -1;
    else if (!byRows && d == 0) in = Ri - 1;
    else if (abs(d) <= Ri) in = isqrtInt(Ri * Ri - d * d);
  }
  auto put = [&](int a, int b) {
    if (a < lo) a = lo;
    if (b > hi) b = hi;
    if (a <= b) emit(a, b);
  };
  if (in < 0) { put(c - o, c + o); return; }
  put(c - o, c - in - 1);
  put(c + in + 1, c + o);
}

template <class Paint>
void ringImpl(const Bitmap1& bm, int cx, int cy, int R, int Ri, const ClipBox& clip, const Paint& p) {
  if (R < 0 || Ri < 0 || Ri > R) return;
  bool byRows = !(bm.rotation & 1);                // rotations 0, 2: logical rows are native rows
  if (byRows) {
    int y0 = cy - R < clip.y0 ? clip.y0 : cy - R, y1 = cy + R > clip.y1 ? clip.y1 : cy + R;
    for (int y = y0; y <= y1; y++)
      ringLine(true, y - cy, R, Ri, cx, clip.x0, clip.x1, [&](int a, int b) { p.h(a, b, y); });
  } else {                                         // rotations 1, 3: logical columns are native rows
    int x0 = cx - R < clip.x0 ? clip.x0 : cx - R, x1 = cx + R > clip.x1 ? clip.x1 : cx + R;
    for (int x = x0; x <= x1; x++)
      ringLine(false, x - cx, R, Ri, cy, clip.y0, clip.y1, [&](int a, int b) { p.v(x, a, b); });
  }
}

}  // namespace

void fillRing(const Bitmap1& bm, int cx, int cy, int R, int Ri, DrawMode m, const ClipBox& clip) {
  ringImpl(bm, cx, cy, R, Ri, clip, PaintMode{bm, m});
}
void fillRing(const Bitmap1& bm, int cx, int cy, int R, int Ri, Gray g, const ClipBox& clip) {
  ringImpl(bm, cx, cy, R, Ri, clip, PaintGray(bm, g));
}
