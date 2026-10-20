#include "apps/settings/settings_view.h"
#include <stdio.h>

using namespace ui;

void SettingsView::drawList(const Bitmap1& bm, const SettingsModel& m) {
  titleBar(bm, f_, "SETTINGS", m.battPct);
  for (int i = 0; i < m.count; i++) {
    const int y = ROW_Y + i * ROW_H;
    const bool on = i == m.selected;
    const DrawMode ink = on ? DRAW_SET : DRAW_CLEAR;
    if (on) roundFill(bm, 4, y, W - 8, ROW_H - 4, 6, DRAW_CLEAR);
    else bm.hspan(14, y + ROW_H - 2, W - 28, DRAW_CLEAR);
    drawText(bm, 14, y + 22, m.title[i], f_.mid, ink);
    drawText(bm, 14, y + 40, m.sub[i], f_.small, ink);
  }
  hintLine(bm, f_, m.dpad ? "up/down: move    A: open" : "press: next    hold: change");
}

void SettingsView::drawAbout(const Bitmap1& bm, const AboutModel& m) {
  titleBar(bm, f_, "ABOUT", m.battPct);
  int y = 58;
  drawText(bm, 10, y, "Firmware", f_.bold, DRAW_CLEAR);
  drawText(bm, 100, y, m.version, f_.small, DRAW_CLEAR);
  y += 28;
  drawText(bm, 10, y, "Map pack", f_.bold, DRAW_CLEAR);
  if (m.packOk) {
    char b[32];
    snprintf(b, sizeof b, "installed, %.1f MB", m.packBytes / 1048576.0);
    drawText(bm, 100, y, b, f_.small, DRAW_CLEAR);
    y += 24;
    drawText(bm, 10, y, m.area, f_.small, DRAW_CLEAR);
  } else {
    drawText(bm, 100, y, "none installed", f_.small, DRAW_CLEAR);
    y += 22;
    drawText(bm, 100, y, "no AGL, no map", f_.small, DRAW_CLEAR);
  }
  y += 32;
  bm.hspan(10, y - 17, W - 20, DRAW_CLEAR);
  if (m.credit1[0] || m.credit2[0]) {
    drawText(bm, 10, y, "Map data", f_.bold, DRAW_CLEAR);
    y += 22;
    if (m.credit1[0]) y = wrapped(bm, f_.small, 10, y, W - 20, 18, m.credit1, DRAW_CLEAR) + 4;
    if (m.credit2[0]) y = wrapped(bm, f_.small, 10, y, W - 20, 18, m.credit2, DRAW_CLEAR);
  } else {
    drawText(bm, 10, y, "No map data credits:", f_.small, DRAW_CLEAR);
    drawText(bm, 10, y + 19, "no map pack is installed.", f_.small, DRAW_CLEAR);
  }
  hintLine(bm, f_, m.dpad ? "A / B: back" : "press: back");
}
