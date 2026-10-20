#pragma once
// The screen of the sleeping clock, a small desk weather station in the style of an e-paper desk clock: a hero time in
// round-capped stroke numerals (platform/gfx/strokes.h), the forecast pictogram with temperature and humidity, the
// pressure with its 3 h tendency and a 24 h chart. Portrait 240x400, 12 px side margins, hairlines between the blocks.
// Small text uses the shared Font1 cache; the numerals and shapes need no font. Unknown values read "--", never a made-up
// one. While the power-off hold goes on, a thin bar fills along the bottom (countdown()).
// Drawn once a minute: about a hundred stamped discs per numeral, a few ms.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "platform/gfx/font.h"
#include "platform/gfx/shapes.h"
#include "platform/gfx/strokes.h"
#include "platform/gfx/ui.h"
#include "platform/power/weather.h"

namespace clockview {

const int kMargin = 12;
const int kRight = ui::W - kMargin;             // 228

// The fonts of the screen, built on first use for the bitmap's rotation and kept (shared blocks, see font.h).
struct Fonts {
  UiFonts ui;
  Font1 big;                                    // SansBold 18
  void ensure(int rot) {
    ui.ensure(rot);
    if (!big.isBuilt(&FreeSansBold18pt7b, rot)) big.build(&FreeSansBold18pt7b, rot);
  }
};
inline Fonts& fonts() { static Fonts f; return f; }

inline void hairline(const Bitmap1& bm, int y) { hspan(bm, kMargin, y, ui::W - 2 * kMargin, Gray(8)); }

// ---- pictograms ------------------------------------------------------------------------------------------------

inline void sunRays(const Bitmap1& bm, int cx, int cy, int r0, int r1) {
  for (int i = 0; i < 8; i++) {
    const float a = i * 0.785398f, c = cosf(a), s = sinf(a);
    strokes::line2(bm, cx + (int)lroundf(c * r0), cy + (int)lroundf(s * r0), cx + (int)lroundf(c * r1), cy + (int)lroundf(s * r1));
  }
}
inline void drop(const Bitmap1& bm, int x, int y) { strokes::line2(bm, x + 5, y, x, y + 12); }
// The lightning bolt in a 20 x 32 box, with a paper halo so it stands out from the cloud it hangs from.
inline void bolt(const Bitmap1& bm, int x, int y) {
  const int p[7][2] = {{x + 12, 0}, {x, 16}, {x + 8, 16}, {x + 3, 32}, {x + 20, 11}, {x + 11, 11}, {x + 17, 0}};
  int q[7][2];
  for (int k = 0; k < 4; k++) {
    for (int i = 0; i < 7; i++) { q[i][0] = p[i][0] + (k & 1 ? 2 : -2); q[i][1] = p[i][1] + y + (k & 2 ? 2 : -2); }
    strokes::polyFill(bm, q, 7, DRAW_SET);
  }
  for (int i = 0; i < 7; i++) { q[i][0] = p[i][0]; q[i][1] = p[i][1] + y; }
  strokes::polyFill(bm, q, 7, DRAW_CLEAR);
}
// The forecast in the 72 x 72 box at (x, y).
inline void forecast(const Bitmap1& bm, int x, int y, Forecast f) {
  const int cx = x + 36, cy = y + 36;
  switch (f) {
    case FC_SUN:
      strokes::ball(bm, cx, cy, 17, 12);
      sunRays(bm, cx, cy, 24, 33);
      break;
    case FC_SUN_CLOUD:
      strokes::ball(bm, cx - 10, cy - 10, 14, 12);
      sunRays(bm, cx - 10, cy - 10, 20, 27);
      strokes::cloud(bm, cx + 5, cy + 11, 60, 9);
      break;
    case FC_CLOUD: strokes::cloud(bm, cx, cy + 2, 72, 9); break;
    case FC_RAIN:
      strokes::cloud(bm, cx, cy - 9, 68, 9);
      for (int i = 0; i < 4; i++) drop(bm, cx - 24 + i * 15, cy + 14);
      break;
    case FC_STORM:
      strokes::cloud(bm, cx, cy - 11, 68, 9);
      bolt(bm, cx - 9, cy + 1);
      break;
    default: break;
  }
}

// ---- small parts -------------------------------------------------------------------------------------------------

// The 3 h tendency, centred at (cx, cy): a filled triangle up / right (steady) / down, two when it changes fast.
inline void trend(const Bitmap1& bm, int cx, int cy, int cls) {
  if (cls == 0) {
    const int p[3][2] = {{cx - 6, cy - 7}, {cx + 7, cy}, {cx - 6, cy + 7}};
    strokes::polyFill(bm, p, 3, DRAW_CLEAR);
    return;
  }
  const int n = (cls < 0 ? -cls : cls) >= 2 ? 2 : 1, dir = cls > 0 ? -1 : 1;   // dir: the apex's side (-1 up)
  for (int i = 0; i < n; i++) {
    const int y = cy + (n == 2 ? (i ? 7 : -7) : 0);
    const int p[3][2] = {{cx - 8, y - dir * 5}, {cx + 8, y - dir * 5}, {cx, y + dir * 6}};
    strokes::polyFill(bm, p, 3, DRAW_CLEAR);
  }
}

// A droplet 10 wide, 15 high with its top at y.
inline void droplet(const Bitmap1& bm, int x, int y) {
  const int p[3][2] = {{x + 5, y}, {x, y + 9}, {x + 10, y + 9}};
  strokes::polyFill(bm, p, 3, DRAW_CLEAR);
  fillDisc(bm, x + 5, y + 10, 5, DRAW_CLEAR);
}

// Battery outline with a level, top right, 25 x 12. No digits: the fill says it.
inline void battery(const Bitmap1& bm, int x, int y, int pct) {
  ui::roundFrame(bm, x, y, 23, 12, 3, 1, DRAW_CLEAR);
  bm.fillRect(x + 23, y + 4, 2, 4, DRAW_CLEAR);
  const int w = 15 * (pct > 100 ? 100 : pct) / 100;
  if (w > 0) ui::roundFill(bm, x + 3, y + 3, w < 3 ? 3 : w, 6, 1, DRAW_CLEAR);
}

// The power-off countdown along the bottom: bar 0..steps-1.
inline void countdown(const Bitmap1& bm, int bar, int steps) {
  const int w = ui::W - 2 * kMargin, h = 8, y = 388;
  ui::roundFrame(bm, kMargin, y, w, h, 4, 1, DRAW_CLEAR);
  const int f = (w - 4) * (bar + 1) / steps;
  if (f > 0) ui::roundFill(bm, kMargin + 2, y + 2, f, h - 4, 2, DRAW_CLEAR);
}

// Width of the stroke numerals of s (digits and '-'), centre-line cell W, stroke radius r.
inline int numberWidth(const char* s, int W, int r, int gap) {
  int w = 0;
  for (const char* c = s; *c; c++) w += (*c == '.' ? 2 * r + 2 : *c == '-' ? W * 7 / 10 + 2 * r : W + 2 * r) + (c != s ? gap : 0);
  return w;
}
// Stroke numerals with the top-left of the ink at (x, y). Digit cell: centre-line box W x H, stroke radius r.
// s holds digits, '-' (a dash) and '.' (a decimal point on the baseline).
inline void number(const Bitmap1& bm, int x, int y, int W, int H, int r, int gap, const char* s) {
  strokes::Pen pen(bm, r);
  for (const char* c = s; *c; c++) {
    if (*c == '.') {
      fillDisc(bm, x + r + 1, y + 2 * r + H - r - 1, r + 1, DRAW_CLEAR);
      x += 2 * r + 2 + gap;
      continue;
    }
    const int cw = *c == '-' ? W * 7 / 10 : W;
    strokes::strokeDigit(pen, (float)(x + r), (float)(y + r), (float)cw, (float)H, *c == '-' ? -2 : *c - '0');
    x += cw + 2 * r + gap;
  }
}

inline void chart(const Bitmap1& bm, int x, int y, int w, int h, const WeatherLog* wl, Fonts& f, int minute) {
  int lo = 0, hi = 0;
  const bool have = wl && wl->count() >= 2 && wl->chartRange(lo, hi);
  const int cw = w - 8;                                           // the newest sample sits 8 px in, for the dot
  if (!have) {
    for (int gx = x; gx < x + w; gx += 6) bm.fillRect(gx, y + h / 2, 2, 1, DRAW_CLEAR);
    ui::centered(bm, f.ui.mid, x + w / 2, y + h / 2 - 6, "--", DRAW_CLEAR);
    return;
  }
  const int top = y + 8, ph = h - 16;                             // the line stays this far from the frame
  const int n = wl->count();                                      // column and row of a sample, computed where needed (no arrays: static RAM)
  auto XS = [&](int i) { return x + chartX(i, cw); };
  auto YS = [&](int i) { return top + chartY(wl->get(i), lo, hi, ph); };
  if (n > WeatherLog::kTrendAge) {                                // 3 h ago: a dotted vertical marker
    const int mx = XS(WeatherLog::kTrendAge);
    for (int gy = y; gy < y + h; gy += 4) bm.fillRect(mx, gy, 1, 2, DRAW_CLEAR);
  }
  for (int i = n - 1; i > 0; i--) {                              // the area under the line, light; samples are linear in between
    const int xa = XS(i), xb = XS(i - 1);
    for (int c = xa; c <= xb; c++) {
      const int cy = xb == xa ? YS(i) : YS(i) + (YS(i - 1) - YS(i)) * (c - xa) / (xb - xa);
      vline(bm, c, cy + 2, y + h - 1, Gray(13));
    }
  }
  {  // a faint grid, quieter than the fill: single dots every 6 px on a vertical every 6 h (at the wall-clock 00 / 06 / 12 / 18 h when the
     // time is known, else 6 h back from now) and a dotted line across the middle of the range
    int ages[5], na = 0;
    if (minute >= 0 && minute < 1440) { for (int b = 0; b < 1440; b += 360) { const int a = ((minute - b + 1440) % 1440) / 10; if (a >= 1 && a < n) ages[na++] = a; } }
    else { for (int a = 36; a < n; a += 36) ages[na++] = a; }
    for (int k = 0; k < na; k++) {
      const int gx = XS(ages[k]);
      for (int gy = y + 1; gy < y + h; gy += 6) bm.pixel(gx, gy, DRAW_CLEAR);
    }
    for (int gx = x + 2; gx < x + w; gx += 8) bm.pixel(gx, y + h / 2, DRAW_CLEAR);
  }
  for (int i = n - 1; i > 0; i--) {                              // the line, 2 px
    lineGrown(bm, XS(i), YS(i), XS(i - 1), YS(i - 1), 0, DRAW_CLEAR);
    lineGrown(bm, XS(i), YS(i) + 1, XS(i - 1), YS(i - 1) + 1, 0, DRAW_CLEAR);
  }
  fillDisc(bm, XS(0), YS(0), 6, DRAW_SET);                        // now: a ring
  fillDisc(bm, XS(0), YS(0), 5, DRAW_CLEAR);
  fillDisc(bm, XS(0), YS(0), 3, DRAW_SET);
  char b[24];                                                     // the span of the chart, small, under it
  snprintf(b, sizeof b, "%d-%d hPa", lo / 10, hi / 10);
  drawText(bm, x + w - 3 - textWidth(b, f.ui.small), y + h + 14, b, f.ui.small, DRAW_CLEAR);
}

// bm is a white canvas. minute: local minutes of the day, < 0 unknown. battPct < 0: no battery shown.
// wl may be null (no weather: "--" and an empty chart). bar >= 0: the power-off countdown bar. date: e.g. "Mon 19 Oct", or null.
inline void draw(const Bitmap1& bm, int minute, int battPct, const WeatherLog* wl = nullptr, int bar = -1, int steps = 12,
                 const char* date = nullptr) {
  Fonts& f = fonts();
  f.ensure(bm.rotation);
  if (date) drawText(bm, kMargin, 19, date, f.ui.small, DRAW_CLEAR);
  if (battPct >= 0) battery(bm, kRight - 25, 8, battPct);

  {  // The time: four numerals and a colon, tabular. Ink 44 x 99 per numeral (centre line 31 x 87, stroke 13).
    const int W = 31, H = 87, R = 6, GAP = 5, COLON = 28;
    const int cell = W + 2 * R;
    const int total = 4 * cell + 2 * GAP + COLON;
    const int x = (ui::W - total) / 2, y = 40;
    int dg[4] = {-1, -1, -1, -1};
    if (minute >= 0 && minute < 1440) { dg[0] = minute / 600; dg[1] = minute / 60 % 10; dg[2] = minute % 60 / 10; dg[3] = minute % 10; }
    strokes::Pen pen(bm, R);
    const int px[4] = {x, x + cell + GAP, x + 2 * cell + GAP + COLON, x + 3 * cell + 2 * GAP + COLON};
    for (int i = 0; i < 4; i++) strokes::strokeDigit(pen, (float)(px[i] + R), (float)(y + R), (float)W, (float)H, dg[i]);
    const int cx = x + 2 * cell + GAP + COLON / 2 - GAP / 2;
    fillDisc(bm, cx, y + 36, 7, DRAW_CLEAR);
    fillDisc(bm, cx, y + 66, 7, DRAW_CLEAR);
  }

  hairline(bm, 160);

  // Conditions: the pictogram at the left, temperature and humidity at the right.
  if (wl) forecast(bm, kMargin, 182, wl->forecast());
  const bool tOk = wl && isfinite(wl->tempC), hOk = wl && isfinite(wl->rhPct);
  {
    const int W = 18, H = 42, R = 3, GAP = 6;
    char b[8];
    if (tOk) {                                                    // one decimal: 21.4, -3.0
      const int t10 = (int)lroundf(wl->tempC * 10.0f), a10 = t10 < 0 ? -t10 : t10;
      snprintf(b, sizeof b, "%s%d.%d", t10 < 0 ? "-" : "", a10 / 10, a10 % 10);
    } else strcpy(b, "--");
    const int unit = 16;                                          // the degree ring
    const int x0 = kRight - unit - numberWidth(b, W, R, GAP);
    number(bm, x0, 174, W, H, R, GAP, b);
    const int ex = x0 + numberWidth(b, W, R, GAP);
    fillRing(bm, ex + 9, 180, 5, 2, DRAW_CLEAR);
  }
  {
    char b[8];
    if (hOk) { const int rh = (int)lroundf(wl->rhPct); snprintf(b, sizeof b, "%d%%", rh < 0 ? 0 : rh > 100 ? 100 : rh); }
    else strcpy(b, "--%");
    const int tw = textWidth(b, f.big);
    droplet(bm, kRight - 3 - tw - 18, 243);
    drawText(bm, kRight - 3 - tw, 262, b, f.big, DRAW_CLEAR);
  }

  hairline(bm, 276);

  {  // Pressure: the value, its unit and the tendency on one baseline.
    const int base = 308;
    const bool pOk = wl && wl->latest() > 0;
    char b[8];
    if (pOk) snprintf(b, sizeof b, "%d", (wl->latest() + 5) / 10); else strcpy(b, "--");
    int x = drawText(bm, kMargin, base, b, f.big, DRAW_CLEAR);
    x = drawText(bm, x + 5, base, "hPa", f.ui.small, DRAW_CLEAR);
    int cls = 0;
    if (wl && wl->tendency(cls)) trend(bm, x + 18, base - 9, cls);
  }

  chart(bm, kMargin, 318, ui::W - 2 * kMargin, 50, wl, f, minute);
  drawText(bm, kMargin, 318 + 50 + 14, "24h", f.ui.small, DRAW_CLEAR);
  if (bar >= 0) countdown(bm, bar, steps);
}

}  // namespace clockview
