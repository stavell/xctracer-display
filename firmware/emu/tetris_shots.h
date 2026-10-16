#pragma once
// Screen renders of the tetris app for emu/out (240x400 PGMs, doubled to PNG by build.sh).
#include <stdio.h>
#include <string.h>
#include <string>
#include "apps/games/tetris/tetris_app.h"

namespace ts {
using namespace tetris;

static uint8_t gFb[50 * 240];

static void dump(TetrisApp& app, const char* dir, const char* name, uint32_t now) {
  memset(gFb, 0xFF, sizeof gFb);
  Bitmap1 bm{gFb, 400, 240, 50, 1};
  app.draw(bm, now);
  std::string path = std::string(dir) + "/" + name + ".pgm";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P5\n240 400\n255\n");
  for (int y = 0; y < 400; y++)
    for (int x = 0; x < 240; x++) fputc(bm.getPixel(x, y) ? 255 : 0, f);
  fclose(f);
}

static void tap(TetrisApp& a, Key k, uint32_t& t) {
  a.onKey(k, true, t);
  t += 20;
  a.onKey(k, false, t);
  t += 20;
}

static void shots(const char* dir) {
  TetrisApp app(11);
  uint32_t t = 1000;
  app.begin(t);
  dump(app, dir, "tetris_title", t);
  tap(app, KEY_MAIN, t);
  Game& g = app.game();
  // A ragged stack of mixed pieces, one in the air with its ghost, something held.
  static const int heights[10] = {3, 4, 4, 2, 3, 5, 5, 4, 1, 3};
  for (int x = 0; x < 10; x++)
    for (int y = 22 - heights[x]; y < 22; y++) g.testSetCell(x, y, (uint8_t)(1 + (x / 2 + y * 3) % 7));
  g.testSetCell(3, 20, 0);                          // a hole
  app.onKey(KEY_MAIN, true, t);                     // long press: hold
  t += 450;
  app.tick(t);
  app.onKey(KEY_MAIN, false, t);
  g.testSetPiece(g.current(), 0, 5, 6, t);
  tap(app, KEY_ACTION, t);
  dump(app, dir, "tetris_game", t);

  // Pause
  app.onKey(KEY_MAIN, true, t);
  t += 60;
  app.onKey(KEY_MAIN, false, t);
  dump(app, dir, "tetris_pause", t);
  t += 1000;
  tap(app, KEY_MAIN, t);

  // Line clear: three rows ready and a vertical I dropped into the gap, blink phases 0 and 1
  for (int y = 0; y < 22; y++)
    for (int x = 0; x < 10; x++) g.testSetCell(x, y, 0);
  for (int y = 17; y < 22; y++)
    for (int x = 0; x < 9; x++)
      if (y > 18 || x < 4) g.testSetCell(x, y, (uint8_t)(1 + (x + y) % 7));
  g.testSetPiece(P_I, 1, 7, 0, t);
  tap(app, KEY_UP, t);
  uint32_t t0 = t - 40;
  dump(app, dir, "tetris_clear", t0 + 5);
  dump(app, dir, "tetris_clear2", t0 + TetrisView::FLASH_PHASE_MS + 5);

  // Game over: stack to the top
  t += 1000;
  app.tick(t);
  for (int i = 0; i < 60 && g.state() != ST_OVER; i++) tap(app, KEY_UP, t);
  dump(app, dir, "tetris_over", t);
}

}  // namespace ts
