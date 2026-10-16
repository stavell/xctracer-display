#include "apps/games/tetris/tetris_view.h"
#include <stdio.h>
#include <string.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "platform/gfx/gray.h"

using namespace tetris;

namespace {
const int CELL = 18;
const int WELL_X = 2, WELL_Y = 4;                         // top-left of the visible well interior
const int WELL_W = Game::W * CELL, WELL_H = Game::VISIBLE * CELL;
const int SIDE_X = 190, SIDE_W = 48;                      // side column
const int STRIP_Y = 370;                                  // score strip
const int PREV_CELL = 10;

// 1 px black outline of the rectangle (x, y, w, h); Bitmap1::rectIncl fills, so build it from four spans.
void outline(const Bitmap1& bm, int x, int y, int w, int h) {
  bm.hspan(x, y, w, DRAW_CLEAR);
  bm.hspan(x, y + h - 1, w, DRAW_CLEAR);
  bm.vspan(x, y, h, DRAW_CLEAR);
  bm.vspan(x + w - 1, y, h, DRAW_CLEAR);
}

int hiddenToPx(int by) { return WELL_Y + (by - Game::HIDDEN) * CELL; }
int boardToPxX(int bx) { return WELL_X + bx * CELL; }
}  // namespace

// ---- cells
// Every cell is black with the pattern painted into its inside, leaving a 2 px black edge at the bottom
// and right and 1 px at the top and left, which reads as a bevel. The solid I gets a white highlight
// line instead, being black already.
void TetrisView::drawCell(const Bitmap1& bm, int x, int y, int s, uint8_t t) {
  bm.fillRect(x, y, s, s, DRAW_CLEAR);
  int ix = x + 1, iy = y + 1, iw = s - 3, ih = s - 3;     // inside, minus the shadow edge
  switch (t) {
    case P_I:                                              // solid with a highlight
      bm.hspan(ix + 1, iy + 1, iw - 2, DRAW_SET);
      bm.vspan(ix + 1, iy + 2, ih - 3, DRAW_SET);
      break;
    case P_O: {                                            // hollow with a solid centre
      bm.fillRect(ix, iy, iw, ih, DRAW_SET);
      int c = iw / 3;
      bm.fillRect(ix + (iw - c) / 2, iy + (ih - c) / 2, c, c, DRAW_CLEAR);
      break;
    }
    case P_T: fillRect(bm, ix, iy, iw, ih, Gray(8)); break;
    case P_J: fillRect(bm, ix, iy, iw, ih, Gray(12)); break;
    case P_L: fillRect(bm, ix, iy, iw, ih, Gray(4)); break;
    case P_S:                                              // diagonal stripes
      for (int j = 0; j < ih; j++)
        for (int i = 0; i < iw; i++)
          if (((i + j) & 3) < 2) bm.pixel(ix + i, iy + j, DRAW_SET);
      break;
    case P_Z:                                              // horizontal stripes
      for (int j = 0; j < ih; j++)
        if ((j & 3) < 2) bm.hspan(ix, iy + j, iw, DRAW_SET);
      break;
    default: break;
  }
}

static void ghostCell(const Bitmap1& bm, int x, int y, int s) {
  fillRect(bm, x + 1, y + 1, s - 2, s - 2, Gray(13));
  hspan(bm, x, y, s, Gray(6));
  hspan(bm, x, y + s - 1, s, Gray(6));
  vspan(bm, x, y, s, Gray(6));
  vspan(bm, x + s - 1, y, s, Gray(6));
}

// ---- text
void TetrisView::ensureFonts(int rot) {
  if (!small_.isBuilt(&FreeSans9pt7b, rot)) small_.build(&FreeSans9pt7b, rot);
  if (!smallBold_.isBuilt(&FreeSansBold9pt7b, rot)) smallBold_.build(&FreeSansBold9pt7b, rot);
  if (!mid_.isBuilt(&FreeSansBold12pt7b, rot)) mid_.build(&FreeSansBold12pt7b, rot);
  if (!big_.isBuilt(&FreeSansBold24pt7b, rot)) big_.build(&FreeSansBold24pt7b, rot);
}

void TetrisView::text(const Bitmap1& bm, const Font1& f, int x, int baseline, const char* s) const {
  drawText(bm, x, baseline, s, f, DRAW_CLEAR);
}

void TetrisView::textCentered(const Bitmap1& bm, const Font1& f, int cx, int baseline, const char* s) const {
  drawText(bm, cx - textWidth(s, f) / 2, baseline, s, f, DRAW_CLEAR);
}

// ---- parts
void TetrisView::drawWell(const Bitmap1& bm, const Game& g, uint32_t now) const {
  // Frame: 2 px black around the interior.
  bm.fillRect(WELL_X - 2, WELL_Y - 2, WELL_W + 4, 2, DRAW_CLEAR);
  bm.fillRect(WELL_X - 2, WELL_Y + WELL_H, WELL_W + 4, 2, DRAW_CLEAR);
  bm.fillRect(WELL_X - 2, WELL_Y, 2, WELL_H, DRAW_CLEAR);
  bm.fillRect(WELL_X + WELL_W, WELL_Y, 2, WELL_H, DRAW_CLEAR);
  // Grid dots at the cell corners.
  for (int r = 1; r < Game::VISIBLE; r++)
    for (int c = 1; c < Game::W; c++) bm.pixel(WELL_X + c * CELL, WELL_Y + r * CELL, DRAW_CLEAR);

  for (int y = Game::HIDDEN; y < Game::H; y++)
    for (int x = 0; x < Game::W; x++)
      if (uint8_t t = g.cell(x, y)) drawCell(bm, boardToPxX(x), hiddenToPx(y), CELL, t);

  if (g.state() == ST_PLAYING && g.current() != P_NONE) {
    Block b[4];
    g.curBlocks(b);
    int dy = g.ghostY() - g.curY();
    if (dy > 0)
      for (int i = 0; i < 4; i++) {
        int gy = b[i].y + dy;
        bool covered = false;                              // the piece itself sits over its ghost
        for (int j = 0; j < 4; j++)
          if (b[j].x == b[i].x && b[j].y == gy) covered = true;
        if (!covered && gy >= Game::HIDDEN) ghostCell(bm, boardToPxX(b[i].x), hiddenToPx(gy), CELL);
      }
    for (int i = 0; i < 4; i++)
      if (b[i].y >= Game::HIDDEN) drawCell(bm, boardToPxX(b[i].x), hiddenToPx(b[i].y), CELL, g.current());
  }

  if (g.state() == ST_CLEARING) {                          // blink the full rows
    int ph = flashPhase(g, now);
    for (int y = Game::HIDDEN; y < Game::H; y++) {
      if (!(g.clearRows() & (1u << y))) continue;
      if ((ph & 1) == 0) bm.fillRect(WELL_X, hiddenToPx(y), WELL_W, CELL, DRAW_INVERT);
      else fillRect(bm, WELL_X, hiddenToPx(y), WELL_W, CELL, Gray(12));
    }
  }
}

void TetrisView::drawPreview(const Bitmap1& bm, int bx, int by, int bw, int bh, Piece p, bool dim) const {
  if (p == P_NONE) return;
  const Block* b = Game::shape(p, 0);
  int x0 = 99, x1 = -1, y0 = 99, y1 = -1;
  for (int i = 0; i < 4; i++) {
    if (b[i].x < x0) x0 = b[i].x;
    if (b[i].x > x1) x1 = b[i].x;
    if (b[i].y < y0) y0 = b[i].y;
    if (b[i].y > y1) y1 = b[i].y;
  }
  int pw = (x1 - x0 + 1) * PREV_CELL, ph = (y1 - y0 + 1) * PREV_CELL;
  int ox = bx + (bw - pw) / 2, oy = by + (bh - ph) / 2;
  for (int i = 0; i < 4; i++) {
    int cx = ox + (b[i].x - x0) * PREV_CELL, cy = oy + (b[i].y - y0) * PREV_CELL;
    if (dim) ghostCell(bm, cx, cy, PREV_CELL);
    else drawCell(bm, cx, cy, PREV_CELL, p);
  }
}

void TetrisView::drawSide(const Bitmap1& bm, const Game& g) {
  const int cx = SIDE_X + SIDE_W / 2;
  textCentered(bm, small_, cx, 18, "HOLD");
  outline(bm, SIDE_X, 24, SIDE_W, 34);
  drawPreview(bm, SIDE_X, 24, SIDE_W, 34, g.held(), g.holdUsed());

  textCentered(bm, small_, cx, 84, "NEXT");
  outline(bm, SIDE_X, 90, SIDE_W, 3 * 32 + 4);
  for (int i = 0; i < Game::NEXT_N; i++) drawPreview(bm, SIDE_X, 92 + i * 32, SIDE_W, 32, g.next(i), false);

  char buf[16];
  textCentered(bm, small_, cx, 226, "LEVEL");
  snprintf(buf, sizeof buf, "%d", g.level());
  textCentered(bm, mid_, cx, 250, buf);
  textCentered(bm, small_, cx, 290, "LINES");
  snprintf(buf, sizeof buf, "%d", g.lines());
  textCentered(bm, mid_, cx, 314, buf);
}

void TetrisView::drawStrip(const Bitmap1& bm, const Game& g) {
  bm.fillRect(0, STRIP_Y - 2, 240, 1, DRAW_CLEAR);
  char buf[16];
  text(bm, small_, 4, STRIP_Y + 11, "SCORE");
  snprintf(buf, sizeof buf, "%lu", (unsigned long)g.score());
  text(bm, smallBold_, 4, STRIP_Y + 27, buf);
  text(bm, small_, 130, STRIP_Y + 11, "BEST");
  snprintf(buf, sizeof buf, "%lu", (unsigned long)g.highScore());
  text(bm, smallBold_, 130, STRIP_Y + 27, buf);
}

// A white box with a thick black frame and a grey drop shadow, centred on the well.
void TetrisView::overlay(const Bitmap1& bm, const char* title, const char* l1, const char* l2, const char* l3) {
  const int w = 166, h = 120, x = WELL_X + (WELL_W - w) / 2, y = 120;
  fillRect(bm, x + 5, y + 5, w, h, Gray(6));
  bm.fillRect(x, y, w, h, DRAW_CLEAR);
  bm.fillRect(x + 3, y + 3, w - 6, h - 6, DRAW_SET);
  const int cx = x + w / 2;
  textCentered(bm, mid_, cx, y + 36, title);
  bm.fillRect(x + 14, y + 46, w - 28, 2, DRAW_CLEAR);
  if (l1) textCentered(bm, smallBold_, cx, y + 70, l1);
  if (l2) textCentered(bm, small_, cx, y + 92, l2);
  if (l3) textCentered(bm, small_, cx, y + 112 - 2, l3);
}

void TetrisView::drawTitle(const Bitmap1& bm, const Game& g) {
  // Frame
  outline(bm, 0, 0, 240, 400);
  outline(bm, 3, 3, 234, 394);
  textCentered(bm, big_, 120, 92, "TETRIS");
  bm.fillRect(30, 104, 180, 3, DRAW_CLEAR);
  // The seven pieces in two rows, as a swatch card.
  static const Piece row1[] = {P_I, P_O, P_T, P_S};
  static const Piece row2[] = {P_Z, P_J, P_L};
  const int S = 14;
  auto place = [&](const Piece* ps, int n, int y) {
    int total = 0;
    int widths[4];
    for (int i = 0; i < n; i++) {
      const Block* b = Game::shape(ps[i], 0);
      int mx = 0;
      for (int k = 0; k < 4; k++) mx = b[k].x > mx ? b[k].x : mx;
      widths[i] = (mx + 1) * S;
      total += widths[i] + 12;
    }
    total -= 12;
    int x = (240 - total) / 2;
    for (int i = 0; i < n; i++) {
      const Block* b = Game::shape(ps[i], 0);
      int miny = 9;
      for (int k = 0; k < 4; k++) miny = b[k].y < miny ? b[k].y : miny;
      for (int k = 0; k < 4; k++) drawCell(bm, x + b[k].x * S, y + (b[k].y - miny) * S, S, ps[i]);
      x += widths[i] + 12;
    }
  };
  place(row1, 4, 126);
  place(row2, 3, 176);

  textCentered(bm, mid_, 120, 250, "MAIN: START");
  textCentered(bm, small_, 120, 290, "LEFT / RIGHT   move");
  textCentered(bm, small_, 120, 312, "DOWN soft   UP hard drop");
  textCentered(bm, small_, 120, 334, "A rotate   B rotate back");
  textCentered(bm, small_, 120, 356, "hold MAIN: hold piece");
  char buf[24];
  snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)g.highScore());
  textCentered(bm, smallBold_, 120, 384, buf);
}

// ---- entry points
int TetrisView::flashPhase(const Game& g, uint32_t now) {
  if (g.state() != ST_CLEARING) return 0;
  int32_t d = (int32_t)(now - g.clearStart());
  return d < 0 ? 0 : d / FLASH_PHASE_MS;
}

uint32_t TetrisView::nextFlashAt(const Game& g, uint32_t now) {
  if (g.state() != ST_CLEARING) return 0xFFFFFFFFu;
  return g.clearStart() + (uint32_t)(flashPhase(g, now) + 1) * FLASH_PHASE_MS;
}

void TetrisView::draw(const Bitmap1& bm, const Game& g, uint32_t now) {
  ensureFonts(bm.rotation);
  if (g.state() == ST_TITLE) {
    drawTitle(bm, g);
    return;
  }
  drawWell(bm, g, now);
  drawSide(bm, g);
  drawStrip(bm, g);
  char buf[24];
  if (g.state() == ST_PAUSED) {
    overlay(bm, "PAUSED", "MAIN: resume", nullptr, nullptr);
  } else if (g.state() == ST_OVER) {
    snprintf(buf, sizeof buf, "score %lu", (unsigned long)g.score());
    overlay(bm, "GAME OVER", g.newBest() ? "NEW BEST!" : buf, g.newBest() ? buf : nullptr, "MAIN: new game");
  }
}
