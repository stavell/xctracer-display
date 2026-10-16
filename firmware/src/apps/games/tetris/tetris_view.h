#pragma once
// Draws a tetris::Game onto a Bitmap1 (portrait 240x400, white canvas). Fast gfx only: spans, grey
// dither and the glyph atlas; no Adafruit_GFX, no per-cell allocation. Stateless apart from the font
// cache, so it redraws the whole frame each time it is asked.
//
// Look: 18 px cells (well 180x360 at the left), a side column with HOLD / NEXT / LEVEL / LINES, score
// and best in a strip at the bottom. Each piece type has its own fill (solid, hollow, three greys,
// diagonal and horizontal stripes) so pieces are told apart without colour; the ghost is a light grey
// dither with a dotted edge.
#include <stdint.h>
#include "apps/games/tetris/tetris.h"
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"

class TetrisView {
 public:
  static const int FLASH_PHASE_MS = 45;       // one blink step of the line-clear animation

  // Draw the frame for game state g at time now (needed only for the line-clear blink).
  void draw(const Bitmap1& bm, const tetris::Game& g, uint32_t now);

  // Blink step of the clear animation at `now` (0 when no clear is running).
  static int flashPhase(const tetris::Game& g, uint32_t now);
  // Time at which flashPhase() changes next, UINT32_MAX if no clear is running.
  static uint32_t nextFlashAt(const tetris::Game& g, uint32_t now);

  // One block of type t (1..7) with its top-left at (x, y), s pixels square (s >= 6).
  static void drawCell(const Bitmap1& bm, int x, int y, int s, uint8_t t);

 private:
  void ensureFonts(int rot);
  void text(const Bitmap1& bm, const Font1& f, int x, int baseline, const char* s) const;
  void textCentered(const Bitmap1& bm, const Font1& f, int cx, int baseline, const char* s) const;
  void drawWell(const Bitmap1& bm, const tetris::Game& g, uint32_t now) const;
  void drawSide(const Bitmap1& bm, const tetris::Game& g);
  void drawPreview(const Bitmap1& bm, int bx, int by, int bw, int bh, tetris::Piece p, bool dim) const;
  void drawStrip(const Bitmap1& bm, const tetris::Game& g);
  void drawTitle(const Bitmap1& bm, const tetris::Game& g);
  void overlay(const Bitmap1& bm, const char* title, const char* l1, const char* l2, const char* l3);

  Font1 small_, smallBold_, mid_, big_;
};
