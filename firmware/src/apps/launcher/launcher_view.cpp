#include "apps/launcher/launcher_view.h"
#include <math.h>
#include <stdio.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "platform/gfx/shapes.h"

namespace {
const int W = 240;
const int HDR_H = 32;
const int VISIBLE = 4;
const int FOOT_Y = 330;

DrawMode other(DrawMode m) { return m == DRAW_SET ? DRAW_CLEAR : DRAW_SET; }
}  // namespace

void LauncherView::ensureFonts(int rot) {
  if (!small_.isBuilt(&FreeSans9pt7b, rot)) small_.build(&FreeSans9pt7b, rot);
  if (!bold_.isBuilt(&FreeSansBold9pt7b, rot)) bold_.build(&FreeSansBold9pt7b, rot);
  if (!mid_.isBuilt(&FreeSansBold12pt7b, rot)) mid_.build(&FreeSansBold12pt7b, rot);
}

// Icons live in a 48x48 box at (x, y), drawn in `ink` on a `paper` background.
void LauncherView::icon(const Bitmap1& bm, LauncherIcon ic, int x, int y, DrawMode ink) const {
  const DrawMode paper = other(ink);
  const int cx = x + 24, cy = y + 24;
  switch (ic) {
    case ICON_VARIO: {                                    // a dial with ticks and a needle
      circle(bm, cx, cy, 23, ink);
      circle(bm, cx, cy, 22, ink);
      for (int a = -60; a <= 60; a += 30) {
        float r = a * 0.017453292f;
        line(bm, cx + (int)lroundf(sinf(r) * 16), cy - (int)lroundf(cosf(r) * 16),
             cx + (int)lroundf(sinf(r) * 20), cy - (int)lroundf(cosf(r) * 20), ink);
      }
      line(bm, cx, cy, cx + 11, cy - 13, ink);
      line(bm, cx + 1, cy, cx + 12, cy - 13, ink);
      fillDisc(bm, cx, cy, 3, ink);
      break;
    }
    case ICON_TETRIS: {                                   // a few blocks
      static const uint8_t cells[][2] = {{0, 0}, {1, 0}, {2, 0}, {1, 1}, {0, 2}, {1, 2}};
      for (const auto& c : cells) {
        int px = x + 1 + c[0] * 16, py = y + 1 + c[1] * 16;
        bm.fillRect(px, py, 15, 15, ink);
        bm.fillRect(px + 4, py + 4, 7, 7, paper);
        bm.fillRect(px + 6, py + 6, 3, 3, ink);
      }
      break;
    }
    case ICON_COMPASS: {                                  // ring and a two-tone needle
      circle(bm, cx, cy, 23, ink);
      circle(bm, cx, cy, 22, ink);
      for (int r = 0; r <= 17; r++) {                     // north half solid
        int hw = 6 * r / 17;
        bm.hspan(cx - hw, cy - 17 + r, 2 * hw + 1, ink);
      }
      line(bm, cx - 6, cy, cx, cy + 17, ink);             // south half outlined
      line(bm, cx + 6, cy, cx, cy + 17, ink);
      line(bm, cx - 6, cy, cx + 6, cy, ink);
      bm.hspan(cx - 1, cy - 1, 3, paper);
      break;
    }
    case ICON_GEAR: {
      circle(bm, cx, cy, 13, ink);
      circle(bm, cx, cy, 12, ink);
      circle(bm, cx, cy, 5, ink);
      bm.fillRect(cx - 3, cy - 20, 7, 8, ink);
      bm.fillRect(cx - 3, cy + 13, 7, 8, ink);
      bm.fillRect(cx - 20, cy - 3, 8, 7, ink);
      bm.fillRect(cx + 13, cy - 3, 8, 7, ink);
      break;
    }
  }
}

void LauncherView::draw(const Bitmap1& bm, const LauncherModel& m) {
  ensureFonts(bm.rotation);
  // Header: title left, battery right, white on black.
  bm.fillRect(0, 0, W, HDR_H, DRAW_CLEAR);
  drawText(bm, 10, 23, "APPS", mid_, DRAW_SET);
  if (m.battPct >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d%%", m.battPct);
    drawText(bm, W - 10 - textWidth(b, bold_), 22, b, bold_, DRAW_SET);
  }

  int first = m.selected >= VISIBLE ? m.selected - VISIBLE + 1 : 0;
  for (int k = 0; k < VISIBLE && first + k < m.count; k++) {
    const int i = first + k, y = ROW_Y + k * ROW_H;
    const bool on = i == m.selected;
    const DrawMode ink = on ? DRAW_SET : DRAW_CLEAR;
    if (on) bm.fillRect(4, y, W - 8, ROW_H - 6, DRAW_CLEAR);
    else bm.hspan(14, y + ROW_H - 4, W - 28, DRAW_CLEAR);
    icon(bm, m.rows[i].icon, 14, y + 6, ink);
    drawText(bm, 74, y + 30, m.rows[i].title, mid_, ink);
    drawText(bm, 74, y + 50, m.rows[i].sub, small_, ink);
  }
  if (first > 0) drawText(bm, W - 24, ROW_Y + 12, "^", bold_, DRAW_CLEAR);
  if (first + VISIBLE < m.count) drawText(bm, W - 24, FOOT_Y - 6, "v", bold_, DRAW_CLEAR);

  // Footer
  bm.hspan(0, FOOT_Y, W, DRAW_CLEAR);
  bm.hspan(0, FOOT_Y + 1, W, DRAW_CLEAR);
  char v[40];
  snprintf(v, sizeof v, "firmware %s", m.version);
  drawText(bm, 10, FOOT_Y + 26, v, small_, DRAW_CLEAR);
  const char* hint = m.dpad ? "up / down, A: start" : "press: next    hold: start";
  if (m.heldMs >= 1500) hint = "keep holding: power off";
  drawText(bm, 10, FOOT_Y + 52, hint, m.heldMs >= 1500 ? bold_ : small_, DRAW_CLEAR);
}
