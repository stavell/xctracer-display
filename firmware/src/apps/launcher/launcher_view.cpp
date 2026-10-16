#include "apps/launcher/launcher_view.h"
#include <stdio.h>
#include "platform/gfx/icons.h"
#include "platform/gfx/shapes.h"

using namespace ui;

namespace {
const int GRID_TOP = 42, GRID_BOTTOM = 330, GAP = 8, TILE_W = 108, MARGIN = 8;
const int FOOT_Y = 336;
}  // namespace

void LauncherView::draw(const Bitmap1& bm, const HomeModel& m) {
  f_.ensure(bm.rotation);

  // Status bar: time, map pack, battery.
  bm.fillRect(0, 0, W, BAR_H, DRAW_CLEAR);
  char t[12];
  if (m.localMinutes >= 0) snprintf(t, sizeof t, "%02d:%02d", m.localMinutes / 60, m.localMinutes % 60);
  else snprintf(t, sizeof t, "--:--");
  drawText(bm, 10, 23, t, f_.mid, DRAW_SET);
  const int tw = textWidth(t, f_.mid);
  // Map chip: "MAP" in a white frame when a pack is installed, "no map" when not.
  const int cx = 10 + tw + 14, cw = 44;
  if (m.mapOk) {
    roundFrame(bm, cx, 7, cw, 18, 3, 2, DRAW_SET);
    drawText(bm, cx + (cw - textWidth("MAP", f_.bold)) / 2, 21, "MAP", f_.bold, DRAW_SET);
  } else {
    drawText(bm, cx, 21, "no map", f_.small, DRAW_SET);
  }
  if (m.battPct >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d%%", m.battPct);
    const int bw = textWidth(b, f_.bold);
    drawText(bm, W - 10 - bw, 22, b, f_.bold, DRAW_SET);
    battery(bm, W - 10 - bw - 36, 9, m.battPct, DRAW_SET);
  }

  // Grid of tiles.
  const int n = m.count < HomeModel::MAX_TILES ? m.count : HomeModel::MAX_TILES;
  const int rows = (n + COLS - 1) / COLS;
  int tileH = rows > 0 ? (GRID_BOTTOM - GRID_TOP - (rows - 1) * GAP) / rows : 0;
  if (tileH > 136) tileH = 136;
  const bool roomy = tileH >= 120;
  for (int i = 0; i < n; i++) {
    const int x = MARGIN + (i % COLS) * (TILE_W + GAP), y = GRID_TOP + (i / COLS) * (tileH + GAP);
    const bool on = i == m.selected;
    const DrawMode ink = on ? DRAW_SET : DRAW_CLEAR;
    if (on) roundFill(bm, x, y, TILE_W, tileH, 8, DRAW_CLEAR);
    else roundFrame(bm, x, y, TILE_W, tileH, 8, 2, DRAW_CLEAR);
    const int iy = y + (roomy ? 16 : 8);
    drawIcon(bm, m.tiles[i].icon, x + (TILE_W - ICON_SIZE) / 2, iy, ink);
    centered(bm, f_.mid, x + TILE_W / 2, iy + ICON_SIZE + (roomy ? 30 : 24), m.tiles[i].title, ink);
    if (roomy) centered(bm, f_.small, x + TILE_W / 2, iy + ICON_SIZE + 52, m.tiles[i].sub, ink);
  }

  // Footer: what the chosen tile is, and how to use the box.
  bm.hspan(0, FOOT_Y, W, DRAW_CLEAR);
  bm.hspan(0, FOOT_Y + 1, W, DRAW_CLEAR);
  if (m.heldMs >= 1500) {
    centered(bm, f_.bold, W / 2, FOOT_Y + 40, "keep holding: power off", DRAW_CLEAR);
    return;
  }
  if (m.selected >= 0 && m.selected < n) centered(bm, f_.bold, W / 2, FOOT_Y + 28, m.tiles[m.selected].about, DRAW_CLEAR);
  centered(bm, f_.small, W / 2, FOOT_Y + 52, m.dpad ? "arrows: move    A: open" : "press: next    hold: open", DRAW_CLEAR);
}
