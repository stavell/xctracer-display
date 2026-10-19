#pragma once
// The screen of the sleeping clock, a small desk weather station, drawn without a font (seven-segment glyphs and
// shapes only, so no RAM): the time in big digits, temperature and humidity, the pressure in hPa with its 3 h
// tendency arrow and a forecast pictogram, a 24 h pressure chart, the battery as a small indicator at the top.
// Unknown values read "--": never a made-up one. Portrait 240x400. While the power-off hold goes on, a bar fills
// along the bottom (countdown()).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "platform/gfx/shapes.h"
#include "platform/gfx/ui.h"
#include "platform/power/weather.h"

namespace clockview {

// Segments a b c d e f g of the digits 0..9 (bit 0 = a, the top bar).
static const uint8_t kSeg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
static const uint8_t kDash = 0x40, kLetterC = 0x39, kLetterH = 0x74, kLetterP = 0x73, kLetterA = 0x5F;

// One glyph from a segment mask in a box w x h at (x, y), segment thickness t.
inline void glyph(const Bitmap1& bm, int x, int y, int w, int h, int t, uint8_t s) {
  const int mid = y + (h - t) / 2, r = t / 2;
  const int vh = (h - 3 * t) / 2 + t;                       // vertical segments overlap the bars a little
  if (s & 0x01) ui::roundFill(bm, x + t / 2, y, w - t, t, r, DRAW_CLEAR);                 // a
  if (s & 0x02) ui::roundFill(bm, x + w - t, y + t / 2, t, vh, r, DRAW_CLEAR);             // b
  if (s & 0x04) ui::roundFill(bm, x + w - t, mid + t / 2, t, vh, r, DRAW_CLEAR);           // c
  if (s & 0x08) ui::roundFill(bm, x + t / 2, y + h - t, w - t, t, r, DRAW_CLEAR);          // d
  if (s & 0x10) ui::roundFill(bm, x, mid + t / 2, t, vh, r, DRAW_CLEAR);                   // e
  if (s & 0x20) ui::roundFill(bm, x, y + t / 2, t, vh, r, DRAW_CLEAR);                     // f
  if (s & 0x40) ui::roundFill(bm, x + t / 2, mid, w - t, t, r, DRAW_CLEAR);                // g
}
// One digit (0..9, or -1 for a dash).
inline void digit(const Bitmap1& bm, int x, int y, int w, int h, int t, int d) {
  glyph(bm, x, y, w, h, t, d >= 0 && d <= 9 ? kSeg[d] : kDash);
}

// An integer as digits (a leading '-' for negatives), or "--" when !ok. Returns the x after the last glyph.
inline int number(const Bitmap1& bm, int x, int y, int w, int h, int t, int gap, bool ok, int v) {
  if (!ok) {
    for (int i = 0; i < 2; i++, x += w + gap) digit(bm, x, y, w, h, t, -1);
    return x;
  }
  char b[8];
  snprintf(b, sizeof b, "%d", v);
  for (const char* p = b; *p; p++, x += w + gap) digit(bm, x, y, w, h, t, *p == '-' ? -1 : *p - '0');
  return x;
}

// The percent sign in a box of 20 x h.
inline void percent(const Bitmap1& bm, int x, int y, int h, int t) {
  const int s = t + 1;
  ui::roundFill(bm, x, y, s + 3, s + 3, 2, DRAW_CLEAR);
  ui::roundFill(bm, x + 20 - s - 3, y + h - s - 3, s + 3, s + 3, 2, DRAW_CLEAR);
  lineGrown(bm, x + 19, y + 1, x + 1, y + h - 2, t / 2, DRAW_CLEAR);
}

// ---- pictograms ------------------------------------------------------------------------------------------------

// A filled cloud centred at (cx, cy), s wide; `halo` grows it in white first so it stands out from what is behind it.
inline void cloudShape(const Bitmap1& bm, int cx, int cy, int s, int grow, DrawMode m) {
  const int r = s * 28 / 100 + grow;
  fillDisc(bm, cx - s * 30 / 100, cy + s * 4 / 100, r * 80 / 100, m);
  fillDisc(bm, cx - s * 4 / 100, cy - s * 12 / 100, r, m);
  fillDisc(bm, cx + s * 28 / 100, cy + s * 6 / 100, r * 78 / 100, m);
  ui::roundFill(bm, cx - s * 46 / 100 - grow, cy + s * 4 / 100 - grow, s * 92 / 100 + 2 * grow, s * 24 / 100 + 2 * grow, s * 12 / 100 + grow, m);
}
inline void cloud(const Bitmap1& bm, int cx, int cy, int s, bool halo) {
  if (halo) cloudShape(bm, cx, cy, s, 3, DRAW_SET);
  cloudShape(bm, cx, cy, s, 0, DRAW_CLEAR);
}
inline void sun(const Bitmap1& bm, int cx, int cy, int r) {
  fillDisc(bm, cx, cy, r, DRAW_CLEAR);
  for (int i = 0; i < 8; i++) {
    const float a = i * 0.785398f;
    const float c = cosf(a), s = sinf(a);
    lineGrown(bm, cx + (int)lroundf(c * (r + 5)), cy + (int)lroundf(s * (r + 5)),
              cx + (int)lroundf(c * (r + 11)), cy + (int)lroundf(s * (r + 11)), 1, DRAW_CLEAR);
  }
}
// The forecast in a box 64 x 56 at (x, y).
inline void forecast(const Bitmap1& bm, int x, int y, Forecast f) {
  const int cx = x + 32, cy = y + 28;
  switch (f) {
    case FC_SUN: sun(bm, cx, cy, 13); break;
    case FC_SUN_CLOUD:
      sun(bm, cx - 10, cy - 9, 10);
      cloud(bm, cx + 6, cy + 9, 46, true);
      break;
    case FC_CLOUD: cloud(bm, cx, cy + 2, 58, false); break;
    case FC_RAIN:
      cloud(bm, cx, cy - 8, 54, false);
      for (int i = 0; i < 3; i++) lineGrown(bm, cx - 16 + i * 16, cy + 14, cx - 21 + i * 16, cy + 26, 1, DRAW_CLEAR);
      break;
    case FC_STORM:
      cloud(bm, cx, cy - 10, 54, false);
      lineGrown(bm, cx + 2, cy + 8, cx - 6, cy + 18, 2, DRAW_CLEAR);     // the bolt, white-edged against the cloud
      lineGrown(bm, cx - 6, cy + 18, cx + 5, cy + 18, 2, DRAW_CLEAR);
      lineGrown(bm, cx + 5, cy + 18, cx - 3, cy + 28, 2, DRAW_CLEAR);
      break;
    default: break;
  }
}

// The 3 h tendency: an arrow up / right (steady) / down centred at (cx, cy), doubled when it changes fast.
inline void trend(const Bitmap1& bm, int cx, int cy, int cls) {
  const int a = cls < 0 ? -cls : cls, hw = 12, hh = 12;
  auto head = [&](int y0, int dir) {                         // dir > 0 up
    for (int r = 0; r < hh; r++) {
      const int w = dir > 0 ? r * hw / (hh - 1) : (hh - 1 - r) * hw / (hh - 1);
      bm.hspan(cx - w, y0 + r, 2 * w + 1, DRAW_CLEAR);
    }
  };
  if (cls == 0) {                                            // steady: a sideways arrow
    bm.fillRect(cx - 14, cy - 3, 16, 7, DRAW_CLEAR);
    for (int c = 0; c < hh; c++) {
      const int h = (hh - 1 - c) * hw / (hh - 1);
      bm.vspan(cx + 2 + c, cy - h, 2 * h + 1, DRAW_CLEAR);
    }
    return;
  }
  const int up = cls > 0 ? 1 : -1;
  if (a == 1) {
    bm.fillRect(cx - 4, cy - 14 + (up > 0 ? 8 : 0), 8, 20, DRAW_CLEAR);
    head(up > 0 ? cy - 16 : cy + 4, up);
  } else {
    head(cy - 20 + (up > 0 ? 0 : 16), up);
    head(cy - 4 + (up > 0 ? 0 : 16), up);
  }
}

// ---- the screen ------------------------------------------------------------------------------------------------

// The power-off countdown bar along the bottom: bar 0..steps-1.
inline void countdown(const Bitmap1& bm, int bar, int steps) {
  const int w = 160, h = 14, x = (ui::W - w) / 2, y = 374;
  ui::roundFill(bm, x, y, w, h, 7, DRAW_CLEAR);
  ui::roundFill(bm, x + 2, y + 2, w - 4, h - 4, 5, DRAW_SET);
  const int f = (w - 8) * (bar + 1) / steps;
  if (f > 0) ui::roundFill(bm, x + 4, y + 4, f, h - 8, 3, DRAW_CLEAR);
}

inline void hpaLabel(const Bitmap1& bm, int x, int y) {
  glyph(bm, x, y, 9, 16, 3, kLetterH);
  glyph(bm, x + 12, y, 9, 16, 3, kLetterP);
  glyph(bm, x + 24, y, 9, 16, 3, kLetterA);
}

inline void chart(const Bitmap1& bm, int x, int y, int w, int h, const WeatherLog* wl) {
  int lo = 0, hi = 0;
  const bool have = wl && wl->chartRange(lo, hi);
  for (int hr = 0; hr <= 24; hr += 6) {                      // time ticks: now, -6 h, -12 h, -18 h, -24 h
    const int tx = x + chartX(hr * 6, w);
    bm.vspan(tx, y + h + 2, hr == 0 || hr == 24 ? 7 : 5, DRAW_CLEAR);
  }
  bm.hspan(x, y + h + 1, w, DRAW_CLEAR);
  for (int gx = x; gx < x + w; gx += 8) bm.fillRect(gx, y + h / 2, 3, 1, DRAW_CLEAR);   // a dotted middle line
  if (!have) return;
  int px = 0, py = 0;
  bool first = true;
  for (int age = wl->count() - 1; age >= 0; age--) {          // oldest to newest
    const int sx = x + chartX(age, w), sy = y + chartY(wl->get(age), lo, hi, h);
    if (!first) lineGrown(bm, px, py, sx, sy, 1, DRAW_CLEAR);
    px = sx; py = sy; first = false;
  }
  fillDisc(bm, px, py, 4, DRAW_CLEAR);                       // now
  fillDisc(bm, px, py, 2, DRAW_SET);
  char b[8];                                                 // the scale: highest / lowest hPa of the chart, small
  const int ys[2] = {y, y + h - 14}, vs[2] = {hi / 10, lo / 10};
  for (int k = 0; k < 2; k++) {
    snprintf(b, sizeof b, "%d", vs[k]);
    int dx = x - 5 - (int)strlen(b) * 10;
    for (const char* p = b; *p; p++, dx += 10) digit(bm, dx, ys[k], 8, 14, 3, *p - '0');
  }
}

// bm is a white canvas. minute: local minutes of the day, < 0 unknown. battPct < 0: no battery shown.
// wl may be null (no weather: "--" and an empty chart). bar >= 0: the power-off countdown bar.
inline void draw(const Bitmap1& bm, int minute, int battPct, const WeatherLog* wl = nullptr, int bar = -1, int steps = 12) {
  // Battery: a small indicator, top right.
  if (battPct >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d", battPct > 100 ? 100 : battPct);
    const int n = (int)strlen(b);
    ui::battery(bm, ui::W - 10 - 29, 11, battPct, DRAW_CLEAR);
    int bx = ui::W - 10 - 29 - 10 - n * 12;
    for (int i = 0; i < n; i++, bx += 12) digit(bm, bx, 10, 9, 18, 3, b[i] - '0');
  }
  // The time.
  const int DW = 44, DH = 84, T = 10, GAP = 6, COLON = 18;
  const int total = 4 * DW + 4 * GAP + COLON;
  int x = (ui::W - total) / 2;
  const int y = 40;
  int dg[4] = {-1, -1, -1, -1};
  if (minute >= 0 && minute < 1440) { dg[0] = minute / 600; dg[1] = minute / 60 % 10; dg[2] = minute % 60 / 10; dg[3] = minute % 10; }
  digit(bm, x, y, DW, DH, T, dg[0]); x += DW + GAP;
  digit(bm, x, y, DW, DH, T, dg[1]); x += DW + GAP;
  bm.fillRect(x + (COLON - 10) / 2, y + 22, 10, 10, DRAW_CLEAR);                            // the colon
  bm.fillRect(x + (COLON - 10) / 2, y + DH - 32, 10, 10, DRAW_CLEAR);
  x += COLON + GAP;
  digit(bm, x, y, DW, DH, T, dg[2]); x += DW + GAP;
  digit(bm, x, y, DW, DH, T, dg[3]);

  bm.hspan(12, 142, ui::W - 24, DRAW_CLEAR);
  // Temperature and humidity.
  const bool tOk = wl && isfinite(wl->tempC), hOk = wl && isfinite(wl->rhPct);
  const int ty = 156, TW = 26, TH = 56, TT = 7, TG = 6;
  int tx = number(bm, 10, ty, TW, TH, TT, TG, tOk, tOk ? (int)lroundf(wl->tempC) : 0);
  ui::roundFill(bm, tx + 1, ty + 2, 14, 14, 7, DRAW_CLEAR);                                  // the degree ring
  ui::roundFill(bm, tx + 4, ty + 5, 8, 8, 4, DRAW_SET);
  glyph(bm, tx + 20, ty + 12, 24, 44, 7, kLetterC);
  int rh = hOk ? (int)lroundf(wl->rhPct) : 0;
  if (rh > 99) rh = 99;
  if (rh < 0) rh = 0;
  const int hx = number(bm, 142, ty, TW, TH, TT, TG, hOk, rh);
  percent(bm, hx + 2, ty + 14, 40, 7);

  bm.hspan(12, 226, ui::W - 24, DRAW_CLEAR);
  // Pressure: the value, the tendency arrow and the forecast pictogram.
  const int py = 238;
  const bool pOk = wl && wl->latest() > 0;
  if (pOk) {
    char b[8];
    snprintf(b, sizeof b, "%d", (wl->latest() + 5) / 10);
    int px = 10;
    for (const char* p = b; *p; p++, px += 22) digit(bm, px, py, 18, 40, 6, *p - '0');
  } else {
    int px = 10;
    for (int i = 0; i < 4; i++, px += 22) digit(bm, px, py, 18, 40, 6, -1);
  }
  hpaLabel(bm, 10, py + 48);
  int cls = 0;
  if (wl && wl->tendency(cls)) trend(bm, 128, py + 38, cls);
  else bm.fillRect(118, py + 18, 20, 6, DRAW_CLEAR);                                         // unknown: a dash
  if (wl) forecast(bm, 164, py - 4, wl->forecast());

  // The 24 h chart.
  chart(bm, 46, 306, 184, 62, wl);
  if (bar >= 0) countdown(bm, bar, steps);
}

}  // namespace clockview
