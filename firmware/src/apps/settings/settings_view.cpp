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
    else bm.hspan(14, y + ROW_H - 3, W - 28, DRAW_CLEAR);
    drawText(bm, 14, y + 19, m.title[i], f_.mid, ink);
    drawText(bm, 14, y + 34, m.sub[i], f_.small, ink);
  }
  const int fy = ROW_Y + m.count * ROW_H + 6;
  bm.hspan(0, fy, W, DRAW_CLEAR);
  bm.hspan(0, fy + 1, W, DRAW_CLEAR);
  if (m.heldMs >= 1500) {
    centered(bm, f_.bold, W / 2, H - 18, "keep holding: home", DRAW_CLEAR);
  } else {
    centered(bm, f_.small, W / 2, H - 28, m.dpad ? "arrows: move, change    A: open" : "press: next    hold: change", DRAW_CLEAR);
    centered(bm, f_.small, W / 2, H - 8, m.dpad ? "B: home" : "hold 3 s: home", DRAW_CLEAR);
  }
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
  centered(bm, f_.small, W / 2, H - 5, "press: back", DRAW_CLEAR);
}
