#pragma once
// The screen of the sleeping clock: the time in big seven-segment digits (no font, no RAM), a battery glyph with
// its percentage in small digits below. Unknown time reads "--:--": never a made-up one. Portrait 240x400.
#include <stdio.h>
#include <string.h>
#include "platform/gfx/ui.h"

namespace clockview {

// Segments a b c d e f g of the digits 0..9 (bit 0 = a, the top bar).
static const uint8_t kSeg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

// One digit (0..9, or -1 for a dash) in a box w x h at (x, y), segment thickness t.
inline void digit(const Bitmap1& bm, int x, int y, int w, int h, int t, int d) {
  const uint8_t s = d >= 0 && d <= 9 ? kSeg[d] : 0x40;
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

// bm is a white canvas. minute: local minutes of the day, < 0 unknown. battPct < 0: no battery shown.
inline void draw(const Bitmap1& bm, int minute, int battPct) {
  const int DW = 42, DH = 92, T = 10, GAP = 6, COLON = 20;
  const int total = 4 * DW + 4 * GAP + COLON;
  int x = (ui::W - total) / 2;
  const int y = 150;
  int dg[4] = {-1, -1, -1, -1};
  if (minute >= 0 && minute < 1440) { dg[0] = minute / 600; dg[1] = minute / 60 % 10; dg[2] = minute % 60 / 10; dg[3] = minute % 10; }
  digit(bm, x, y, DW, DH, T, dg[0]); x += DW + GAP;
  digit(bm, x, y, DW, DH, T, dg[1]); x += DW + GAP;
  bm.fillRect(x + (COLON - 10) / 2, y + 24, 10, 10, DRAW_CLEAR);                            // the colon
  bm.fillRect(x + (COLON - 10) / 2, y + DH - 34, 10, 10, DRAW_CLEAR);
  x += COLON + GAP;
  digit(bm, x, y, DW, DH, T, dg[2]); x += DW + GAP;
  digit(bm, x, y, DW, DH, T, dg[3]);
  if (battPct >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d", battPct > 100 ? 100 : battPct);
    const int n = (int)strlen(b), sw = 14, sh = 24, st = 4, sg = 6;
    const int bw = 29, gap = 12, tw = n * (sw + sg) + 14;
    int bx = (ui::W - (bw + gap + tw)) / 2;
    const int by = 310;
    ui::battery(bm, bx, by + 3, battPct, DRAW_CLEAR);
    bx += bw + gap;
    for (int i = 0; i < n; i++, bx += sw + sg) digit(bm, bx, by, sw, sh, st, b[i] - '0');
    bm.fillRect(bx, by + 2, 4, 4, DRAW_CLEAR);                                              // a small percent sign
    bm.fillRect(bx + 10, by + sh - 6, 4, 4, DRAW_CLEAR);
    for (int i = 0; i < sh - 4; i++) bm.fillRect(bx + 11 - i * 9 / (sh - 4), by + 2 + i, 2, 1, DRAW_CLEAR);
  }
}

}  // namespace clockview
