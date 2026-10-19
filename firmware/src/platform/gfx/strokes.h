#pragma once
// Smooth drawing for the desk-clock screens, on Bitmap1: round-capped strokes (a disc stamped along a path), stroke
// numerals in a geometric sans style (straight lines and arcs, tabular width), scanline polygon fill and a few
// dithered shapes. Pure, no fonts, no RAM.
//
//   Pen p(bm, 6);                       // stroke 13 px wide (radius 6)
//   strokeDigit(p, 20, 40, 32, 88, 7);  // a 7 whose centre line fills the box 32 x 88 at (20, 40); ink is 6 px wider all round
#include <math.h>
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

namespace strokes {

// A pen with a round tip: lineTo / arc stamp discs of radius r along the path. flip = the whole path turned by 180 degrees
// inside the box (ox, oy, w, h), so a 9 is a 6.
struct Pen {
  const Bitmap1& bm;
  int r;
  DrawMode mode;
  float lx = 0, ly = 0;
  bool down = false;
  Pen(const Bitmap1& b, int radius, DrawMode m = DRAW_CLEAR) : bm(b), r(radius), mode(m) {}
  void stamp(float x, float y) const { fillDisc(bm, (int)lroundf(x), (int)lroundf(y), r, mode); }
  void lift() { down = false; }
  void moveTo(float x, float y) { lx = x; ly = y; down = true; stamp(x, y); }
  void lineTo(float x, float y) {
    if (!down) { moveTo(x, y); return; }
    const float dx = x - lx, dy = y - ly, len = sqrtf(dx * dx + dy * dy);
    const float step = r > 2 ? r * 0.5f : 1.0f;
    const int n = (int)ceilf(len / step);
    for (int i = 1; i <= n; i++) stamp(lx + dx * i / n, ly + dy * i / n);
    lx = x; ly = y;
  }
};

// Ellipse arc (centre, radii, degrees; angle 0 = right, 90 = down) as a stroke; continues the current stroke.
inline void arc(Pen& p, float cx, float cy, float rx, float ry, float a0, float a1) {
  const float big = rx > ry ? rx : ry;
  int n = (int)ceilf(big * fabsf(a1 - a0) * 0.0174533f / (p.r > 2 ? p.r * 0.5f : 1.0f));
  if (n < 2) n = 2;
  for (int i = 0; i <= n; i++) {
    const float a = (a0 + (a1 - a0) * i / n) * 0.0174533f;
    p.lineTo(cx + rx * cosf(a), cy + ry * sinf(a));
  }
}

// One numeral 0..9 whose centre line fills the box W x H at (ox, oy). -1: a dash, -2: a minus sign (shorter).
inline void strokeDigit(Pen& p, float ox, float oy, float W, float H, int d) {
  const float R = W * 0.5f;
  bool flip = false;
  if (d == 9) { flip = true; d = 6; }
  struct Map {
    float ox, oy, W, H; bool flip;
    float x(float v) const { return ox + (flip ? W - v : v); }
    float y(float v) const { return oy + (flip ? H - v : v); }
  } m{ox, oy, W, H, flip};
  auto mv = [&](float x, float y) { p.lift(); p.moveTo(m.x(x), m.y(y)); };
  auto ln = [&](float x, float y) { p.lineTo(m.x(x), m.y(y)); };
  auto ar = [&](float cx, float cy, float rx, float ry, float a0, float a1) {
    // a flipped path is the arc seen through the point reflection: angle + 180
    const float o = flip ? 180.0f : 0.0f;
    arc(p, m.x(cx), m.y(cy), rx, ry, a0 + o, a1 + o);
  };
  p.lift();
  switch (d) {
    case 0:
      mv(0, R); ar(R, R, R, R, 180, 360); ln(W, H - R); ar(R, H - R, R, R, 0, 180); ln(0, R);
      break;
    case 1:
      mv(W * 0.14f, R * 0.5f + 2); ln(W * 0.58f, 0); ln(W * 0.58f, H);
      break;
    case 2:
      mv(0, R); ar(R, R, R, R, 180, 395); ln(0, H); ln(W, H);
      break;
    case 3:
      mv(R * 0.12f, H * 0.12f); ar(R, H * 0.25f, R * 0.95f, H * 0.25f, 200, 450);
      ar(R, H * 0.75f, R, H * 0.25f, 270, 520);
      break;
    case 4:
      mv(W * 0.74f, 0); ln(W * 0.74f, H);
      mv(W * 0.74f, 0); ln(0, H * 0.68f); ln(W, H * 0.68f);
      break;
    case 5:
      mv(W * 0.92f, 0); ln(W * 0.1f, 0); ln(W * 0.05f, H * 0.47f);
      ar(R, H * 0.69f, R, H * 0.31f, -145, 150);
      break;
    case 6:
      mv(R + R * 0.82f, R - R * 0.57f); ar(R, R, R, R, -35, -180); ln(0, H * 0.68f);
      mv(0, H * 0.68f); ar(R, H * 0.68f, R, H * 0.32f, 180, 540);
      break;
    case 7:
      mv(0, 0); ln(W, 0); ln(W * 0.3f, H);
      break;
    case 8:
      mv(R + R * 0.9f, H * 0.26f); ar(R, H * 0.26f, R * 0.9f, H * 0.26f, 0, 360);
      mv(W, H * 0.74f); ar(R, H * 0.74f, R, H * 0.26f, 0, 360);
      break;
    case -2: mv(W * 0.1f, H * 0.5f); ln(W * 0.9f, H * 0.5f); break;
    default: mv(W * 0.12f, H * 0.5f); ln(W * 0.88f, H * 0.5f); break;
  }
}

// Even-odd scanline fill of a polygon (at most 12 points) in black or paper.
inline void polyFill(const Bitmap1& bm, const int (*pt)[2], int n, DrawMode m) {
  int y0 = pt[0][1], y1 = pt[0][1];
  for (int i = 1; i < n; i++) { if (pt[i][1] < y0) y0 = pt[i][1]; if (pt[i][1] > y1) y1 = pt[i][1]; }
  for (int y = y0; y <= y1; y++) {
    int xs[12], k = 0;
    for (int i = 0; i < n; i++) {
      const int ax = pt[i][0], ay = pt[i][1], bx = pt[(i + 1) % n][0], by = pt[(i + 1) % n][1];
      if (ay == by) continue;
      if ((y < ay) == (y < by)) continue;                    // the edge does not cross this row (half-open)
      if (k < 12) xs[k++] = ax + (bx - ax) * (y - ay) / (by - ay);
    }
    for (int i = 1; i < k; i++) { const int v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
    for (int i = 0; i + 1 < k; i += 2) bm.hspan(xs[i], y, xs[i + 1] - xs[i] + 1, m);
  }
}

// A 2 px line (round-ish ends) for pictogram details.
inline void line2(const Bitmap1& bm, int x0, int y0, int x1, int y1) {
  line(bm, x0, y0, x1, y1, DRAW_CLEAR);
  line(bm, x0 + 1, y0, x1 + 1, y1, DRAW_CLEAR);
  line(bm, x0, y0 + 1, x1, y1 + 1, DRAW_CLEAR);
}

// ---- dithered pictogram parts --------------------------------------------------------------------------------------
// A shape painted twice: grown by the outline width in black, then itself in `fill` (a Gray level; 16 = paper).

// The cloud: a base bar and three bumps, centred at (cx, cy), s wide. grow > 0 paints the outline.
template <class Paint>
inline void cloudParts(Paint&& paint, int cx, int cy, int s, int grow) {
  const int rb = s * 17 / 100 + grow;                                    // base bar ends
  paint.rect(cx - s * 34 / 100 - grow, cy - rb + s * 5 / 100 + 0, s * 68 / 100 + 2 * grow, 2 * rb);
  paint.disc(cx - s * 34 / 100, cy + s * 5 / 100, rb);
  paint.disc(cx + s * 34 / 100, cy + s * 5 / 100, rb);
  paint.disc(cx - s * 12 / 100, cy - s * 8 / 100, s * 22 / 100 + grow);
  paint.disc(cx + s * 13 / 100, cy - s * 2 / 100, s * 17 / 100 + grow);
}
struct SolidPaint {
  const Bitmap1& bm; DrawMode m;
  void disc(int x, int y, int r) { fillDisc(bm, x, y, r, m); }
  void rect(int x, int y, int w, int h) { bm.fillRect(x, y, w, h, m); }
};
struct GrayPaint {
  const Bitmap1& bm; Gray g;
  void disc(int x, int y, int r) { fillDisc(bm, x, y, r, g); }
  void rect(int x, int y, int w, int h) { fillRect(bm, x, y, w, h, g); }
};
// A cloud with a 2 px outline and a dithered body (whiteLevel: lit pixels of 16).
inline void cloud(const Bitmap1& bm, int cx, int cy, int s, int whiteLevel) {
  cloudParts(SolidPaint{bm, DRAW_CLEAR}, cx, cy, s, 2);
  cloudParts(SolidPaint{bm, DRAW_SET}, cx, cy, s, 0);
  if (whiteLevel < 16) cloudParts(GrayPaint{bm, Gray(whiteLevel)}, cx, cy, s, 0);
}
// A disc with a 2 px outline and a dithered body.
inline void ball(const Bitmap1& bm, int cx, int cy, int r, int whiteLevel) {
  fillDisc(bm, cx, cy, r, DRAW_CLEAR);
  fillDisc(bm, cx, cy, r - 2, DRAW_SET);
  if (whiteLevel < 16) fillDisc(bm, cx, cy, r - 2, Gray(whiteLevel));
}

}  // namespace strokes
