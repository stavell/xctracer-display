#include "platform/map/map_draw.h"
#include <math.h>
#include "platform/gfx/shapes.h"

namespace mapdraw {

void thickLine(const Bitmap1& bm, int x0, int y0, int x1, int y1, int w, DrawMode m) {
  if (w <= 1) { line(bm, x0, y0, x1, y1, m); return; }
  for (int a = -(w - 1) / 2; a <= w / 2; a++)
    for (int b = -(w - 1) / 2; b <= w / 2; b++) line(bm, x0 + a, y0 + b, x1 + a, y1 + b, m);
}

bool segVisible(int x0, int y0, int x1, int y1, int w, int h, int m) {
  if ((x0 < -m && x1 < -m) || (x0 > w + m && x1 > w + m)) return false;
  if ((y0 < -m && y1 < -m) || (y0 > h + m && y1 > h + m)) return false;
  return true;
}

int toPx(float v) {
  if (!(v > -30000.0f)) return -30000;
  if (v > 30000.0f) return 30000;
  return (int)floorf(v);
}

void roadPoly(const Bitmap1& bm, const terrain::VecXform& xf, const terrain::RoadPoly& r, int pass, int w, int h, int ox, int oy) {
  if (r.cls > 3) return;                                              // only motorway .. secondary roads, never anything else (rail)
  static const int core[4] = {4, 3, 3, 2};                            // motorway, trunk, primary, secondary: always a solid line 2+ px wide
  const int cw = core[r.cls];
  for (int i = 0; i + 1 < r.n; i++) {
    const int x0 = toPx(xf.px(r.x[i], r.y[i])), y0 = toPx(xf.py(r.x[i], r.y[i]));
    const int x1 = toPx(xf.px(r.x[i + 1], r.y[i + 1])), y1 = toPx(xf.py(r.x[i + 1], r.y[i + 1]));
    if (!segVisible(x0, y0, x1, y1, w, h, 4)) continue;
    if (pass == 0) thickLine(bm, x0 + ox, y0 + oy, x1 + ox, y1 + oy, cw + 2, DRAW_SET);
    else thickLine(bm, x0 + ox, y0 + oy, x1 + ox, y1 + oy, cw, DRAW_CLEAR);
  }
}

bool Labels::place(int x, int y, int tw, int W, Rect* out) {
  if (full()) return false;
  Rect r{x + 6, y - 14, x + 6 + tw, y + 2};
  if (r.x1 >= W - 1) { r.x0 = x - 6 - tw; r.x1 = x - 6; }             // no room on the right: left of the dot
  bool hit = r.x0 < 1;
  for (int i = 0; i < n_ && !hit; i++) {
    const Rect& o = r_[i];
    hit = !(r.x0 > o.x1 + 2 || r.x1 < o.x0 - 2 || r.y0 > o.y1 || r.y1 < o.y0);
  }
  if (hit) return false;
  r_[n_++] = r;
  *out = r;
  return true;
}

void townDot(const Bitmap1& bm, int x, int y, bool city) {
  fillDisc(bm, x, y, city ? 5 : 4, DRAW_SET);
  fillDisc(bm, x, y, city ? 3 : 2, DRAW_CLEAR);
}

void haloText(const Bitmap1& bm, int x, int baseline, const char* s, const Font1& f) {
  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
      if (dx || dy) drawText(bm, x + dx, baseline + dy, s, f, DRAW_SET);
  drawText(bm, x, baseline, s, f, DRAW_CLEAR);
}

}  // namespace mapdraw
