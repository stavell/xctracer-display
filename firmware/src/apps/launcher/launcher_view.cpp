#include "apps/launcher/launcher_view.h"
#include <stdio.h>
#include "platform/gfx/icons.h"
#include "platform/gfx/shapes.h"

using namespace ui;

namespace {
const int GRID_TOP = 42, GRID_BOTTOM = 334, GAP = 8, TILE_W = 108, MARGIN = 8;
}  // namespace

void LauncherView::draw(const Bitmap1& bm, const HomeModel& m) {
  f_.ensure(bm.rotation);

  // The shared title bar: the time as the title, the map chip after it.
  char t[12];
  if (m.localMinutes >= 0) snprintf(t, sizeof t, "%02d:%02d", m.localMinutes / 60, m.localMinutes % 60);
  else snprintf(t, sizeof t, "--:--");
  titleBar(bm, f_, t, m.battPct, m.mapOk ? "MAP" : "no map", m.mapOk);

  // Grid of tiles.
  const int n = m.count < HomeModel::MAX_TILES ? m.count : HomeModel::MAX_TILES;
  const int rows = (n + COLS - 1) / COLS;
  int tileH = rows > 0 ? (GRID_BOTTOM - GRID_TOP - (rows - 1) * GAP) / rows : 0;
  if (tileH > 136) tileH = 136;
  const bool roomy = tileH >= 120;
  // One title size for every tile: the largest that fits the longest label.
  const Font1* titleFont = &f_.mid;
  for (int i = 0; i < n; i++)
    if (textWidth(m.tiles[i].title, f_.mid) > TILE_W - 16) { titleFont = &f_.bold; break; }
  for (int i = 0; i < n; i++) {
    const int x = MARGIN + (i % COLS) * (TILE_W + GAP), y = GRID_TOP + (i / COLS) * (tileH + GAP);
    const bool on = i == m.selected;
    const DrawMode ink = on ? DRAW_SET : DRAW_CLEAR;
    if (on) roundFill(bm, x, y, TILE_W, tileH, 8, DRAW_CLEAR);
    else roundFrame(bm, x, y, TILE_W, tileH, 8, 2, DRAW_CLEAR);
    const int iy = y + (roomy ? 16 : 8);
    drawIcon(bm, m.tiles[i].icon, x + (TILE_W - ICON_SIZE) / 2, iy, ink);
    centered(bm, *titleFont, x + TILE_W / 2, iy + ICON_SIZE + (roomy ? 30 : 24), m.tiles[i].title, ink);
    if (roomy) centered(bm, f_.small, x + TILE_W / 2, iy + ICON_SIZE + 52, m.tiles[i].sub, ink);
  }

  // Footer: what the chosen tile is, then the one hint line.
  if (m.selected >= 0 && m.selected < n) {
    const char* about = m.tiles[m.selected].about;
    centered(bm, fitFont(about, W - 16, f_.bold, f_.small), W / 2, HINT_TOP - 12, about, DRAW_CLEAR);
  }
  hintLine(bm, f_, m.dpad ? "arrows: move    A: open" : "press: next    hold: open");
}
