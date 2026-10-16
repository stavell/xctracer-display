#pragma once
// Tests for apps/games/tetris: rules (rotation and SRS kicks, 7-bag, scoring, levels, lock delay, DAS,
// pause, game over, determinism) and the app wrapper (keys, frame requests, drawing). Included by
// tests.cpp, after CHECK is defined.
#include "apps/games/tetris/tetris_app.h"

namespace tt {
using namespace tetris;

static void clearBoard(Game& g) {
  for (int y = 0; y < Game::H; y++)
    for (int x = 0; x < Game::W; x++) g.testSetCell(x, y, 0);
}

static int countBlocks(const Game& g) {
  int n = 0;
  for (int y = 0; y < Game::H; y++)
    for (int x = 0; x < Game::W; x++) n += g.cell(x, y) != 0;
  return n;
}

static void shapes() {
  for (int t = P_I; t <= P_L; t++) {
    int n = Game::boxSize((Piece)t);
    for (int r = 0; r < 4; r++) {
      const Block* b = Game::shape((Piece)t, r);
      for (int i = 0; i < 4; i++) {
        CHECK(b[i].x >= 0 && b[i].x < n && b[i].y >= 0 && b[i].y < n);
        for (int j = i + 1; j < 4; j++) CHECK(b[i].x != b[j].x || b[i].y != b[j].y);
      }
    }
  }
  // T pointing right after one clockwise turn: bar in the middle column, nub on the right.
  const Block* r1 = Game::shape(P_T, 1);
  int bar = 0, nub = 0;
  for (int i = 0; i < 4; i++) {
    if (r1[i].x == 1) bar++;
    if (r1[i].x == 2 && r1[i].y == 1) nub++;
  }
  CHECK(bar == 3 && nub == 1);
  // I vertical (state 1) sits in column 2 of its box, state 3 in column 1.
  for (int i = 0; i < 4; i++) {
    CHECK(Game::shape(P_I, 1)[i].x == 2);
    CHECK(Game::shape(P_I, 3)[i].x == 1);
  }
}

static void rotation_and_kicks() {
  Game g;
  g.newGame(1, 0);
  clearBoard(g);
  // Free space: a clockwise turn keeps the position, four turns come back.
  g.testSetPiece(P_T, 0, 3, 5, 0);
  g.press(ACT_CW, 0); g.release(ACT_CW, 0);
  CHECK(g.curRot() == 1 && g.curX() == 3 && g.curY() == 5);
  for (int i = 0; i < 3; i++) { g.press(ACT_CW, 0); g.release(ACT_CW, 0); }
  CHECK(g.curRot() == 0);
  g.press(ACT_CCW, 0); g.release(ACT_CCW, 0);
  CHECK(g.curRot() == 3);

  // J turned clockwise from state 1 against the left wall: kick 2 of 1>>2 is (+1, 0).
  g.testSetPiece(P_J, 1, -1, 5, 0);
  g.press(ACT_CW, 0); g.release(ACT_CW, 0);
  CHECK(g.curRot() == 2 && g.curX() == 0 && g.curY() == 5);

  // I vertical against the left wall, counter-clockwise: 1>>0 test 2 is (+2, 0).
  g.testSetPiece(P_I, 1, -2, 5, 0);
  g.press(ACT_CCW, 0); g.release(ACT_CCW, 0);
  CHECK(g.curRot() == 0 && g.curX() == 0 && g.curY() == 5);

  // I horizontal on the floor, clockwise: tests 1-4 fail, test 5 of 0>>1 is (+1, +2 up).
  g.testSetPiece(P_I, 0, 3, 20, 0);
  CHECK(g.grounded());
  g.press(ACT_CW, 0); g.release(ACT_CW, 0);
  CHECK(g.curRot() == 1 && g.curX() == 4 && g.curY() == 18);

  // I vertical against the right wall, clockwise 1>>2: box x 7 puts the bar at column 9 (state 1).
  // State 2 needs columns x..x+3 = 7..10: out. Test 2 of 1>>2 is (-1, 0): 6..9 fits.
  g.testSetPiece(P_I, 1, 7, 5, 0);
  g.press(ACT_CW, 0); g.release(ACT_CW, 0);
  CHECK(g.curRot() == 2 && g.curX() == 6);

  // The O does not move or turn.
  g.testSetPiece(P_O, 0, 4, 5, 0);
  g.press(ACT_CW, 0); g.release(ACT_CW, 0);
  CHECK(g.curRot() == 0 && g.curX() == 4 && g.curY() == 5);

  // A turn that fits nowhere is refused: T in a one-wide shaft.
  clearBoard(g);
  for (int y = 0; y < Game::H; y++)
    for (int x = 0; x < Game::W; x++)
      if (x != 5) g.testSetCell(x, y, 1);
  g.testSetPiece(P_I, 1, 3, 5, 0);                 // vertical bar in column 5
  g.press(ACT_CW, 0); g.release(ACT_CW, 0);
  CHECK(g.curRot() == 1 && g.curX() == 3);
}

static void bag() {
  Game g;
  g.newGame(12345, 0);
  Piece seq[28];
  for (int i = 0; i < 28; i++) {
    seq[i] = g.current();
    Piece peek = g.next(0);
    clearBoard(g);
    g.press(ACT_HARD, 100 + i);
    g.release(ACT_HARD, 100 + i);
    CHECK(g.current() == peek);                     // the preview is what comes
  }
  for (int grp = 0; grp < 4; grp++) {
    int seen[8] = {};
    for (int i = 0; i < 7; i++) seen[seq[grp * 7 + i]]++;
    bool ok = true;
    for (int t = 1; t <= 7; t++) ok = ok && seen[t] == 1;
    CHECK(ok);
  }
  for (int i = 0; i < Game::NEXT_N; i++) CHECK(g.next(i) != P_NONE);
}

// Fill the bottom k rows except column 9 and drop a vertical I into it from the top.
static void clearLines(Game& g, int k, uint32_t& t) {
  clearBoard(g);
  for (int y = Game::H - k; y < Game::H; y++)
    for (int x = 0; x < 9; x++) g.testSetCell(x, y, 3);
  g.testSetPiece(P_I, 1, 7, 0, t);
  g.press(ACT_HARD, t);
  g.release(ACT_HARD, t);
}

static void scoring() {
  static const int pts[5] = {0, 100, 300, 500, 800};
  for (int k = 1; k <= 4; k++) {
    Game g;
    g.newGame(1, 0);
    uint32_t t = 10;
    uint32_t s0 = g.score();
    clearLines(g, k, t);
    CHECK(g.state() == ST_CLEARING);
    CHECK(g.lines() == k);
    CHECK(g.score() - s0 == (uint32_t)(pts[k] + 2 * 18));       // line points + hard drop 2/cell over 18 rows
    CHECK(g.clearRows() != 0);
    // Frozen while the rows blink: input does nothing, the clock does not end it early.
    g.press(ACT_LEFT, t + 50);
    g.release(ACT_LEFT, t + 60);
    g.tick(t + g.cfg.clearMs - 1);
    CHECK(g.state() == ST_CLEARING);
    g.tick(t + g.cfg.clearMs);
    CHECK(g.state() == ST_PLAYING && g.clearRows() == 0);
    CHECK(g.current() != P_NONE);
    // The I's remaining blocks fall by the cleared rows; no filled row is left.
    CHECK(g.cell(0, Game::H - 1) == 0);
    CHECK((g.cell(9, Game::H - 1) != 0) == (k < 4));
    CHECK(countBlocks(g) == 4 - k);
  }
  // Soft drop: 1 point per cell, from the key press and then from the repeat.
  Game g;
  g.newGame(1, 0);
  clearBoard(g);
  g.testSetPiece(P_I, 0, 3, 5, 0);
  uint32_t s0 = g.score();
  g.press(ACT_SOFT, 0);
  CHECK(g.curY() == 6 && g.score() - s0 == 1);
  g.tick(25);
  CHECK(g.curY() == 7 && g.score() - s0 == 2);
  g.tick(100);                                     // 50, 75, 100: three more rows
  CHECK(g.curY() == 10 && g.score() - s0 == 5);
  g.release(ACT_SOFT, 100);
  g.tick(600);
  CHECK(g.curY() == 10);                           // gravity (1 s per row) has not come due
  // Hard drop on an empty board: 2 points per row.
  g.testSetPiece(P_O, 0, 4, 0, 1000);
  uint32_t s1 = g.score();
  int dist = g.ghostY() - g.curY();
  g.press(ACT_HARD, 1000);
  g.release(ACT_HARD, 1000);
  CHECK(g.score() - s1 == (uint32_t)(2 * dist) && dist == 20);
}

static void levels() {
  Game g;
  g.newGame(1, 0);
  uint32_t t = 0;
  for (int i = 0; i < 3; i++) {                     // three tetrises: 12 lines
    clearLines(g, 4, t);
    t += 1000;
    g.tick(t);
  }
  CHECK(g.lines() == 12 && g.level() == 2);
  CHECK(g.gravityMs() == 793);
  uint32_t s0 = g.score();
  clearLines(g, 4, t);
  CHECK(g.score() - s0 == (uint32_t)(800 * 2 + 36));   // the level now counts
  CHECK(Game::gravityMsForLevel(1) == 1000);
  CHECK(Game::gravityMsForLevel(10) == 64);
  bool mono = true;
  for (int l = 2; l <= 25; l++) mono = mono && Game::gravityMsForLevel(l) <= Game::gravityMsForLevel(l - 1);
  CHECK(mono);
}

static void lock_delay() {
  Game g;
  g.newGame(1, 0);
  clearBoard(g);
  g.testSetPiece(P_O, 0, 4, 20, 0);                 // on the floor
  CHECK(g.grounded());
  g.tick(499);
  CHECK(g.current() == P_O && countBlocks(g) == 0);
  g.tick(500);
  CHECK(countBlocks(g) == 4 && g.cell(5, 21) == P_O);

  // A move at 300 ms restarts the delay: it locks at 800, not 500.
  clearBoard(g);
  g.testSetPiece(P_O, 0, 4, 20, 0);
  g.press(ACT_LEFT, 300);
  g.release(ACT_LEFT, 310);
  g.tick(799);
  CHECK(countBlocks(g) == 0);
  g.tick(800);
  CHECK(countBlocks(g) == 4);

  // 15 resets are free; the 16th move does not extend the delay.
  clearBoard(g);
  g.testSetPiece(P_O, 0, 4, 20, 0);
  uint32_t t = 0;
  for (int i = 0; i < 15; i++) {
    t += 100;
    g.press(i & 1 ? ACT_RIGHT : ACT_LEFT, t);
    g.release(i & 1 ? ACT_RIGHT : ACT_LEFT, t + 1);
    g.tick(t + 2);
  }
  CHECK(countBlocks(g) == 0 && g.lockResetsUsed() == 15);
  uint32_t lockAt = t + 500;                        // set by the 15th move
  t += 100;
  g.press(ACT_LEFT, t);                             // the 16th: no new reset
  g.release(ACT_LEFT, t + 1);
  g.tick(lockAt - 1);
  CHECK(countBlocks(g) == 0);
  g.tick(lockAt);
  CHECK(countBlocks(g) == 4);

  // Falling to a lower row gives the resets back.
  clearBoard(g);
  g.testSetPiece(P_O, 0, 4, 10, 0);
  for (int y = 12; y < 22; y++) g.testSetCell(0, y, 1);   // nothing under the O: it falls
  CHECK(!g.grounded());
}

static void das() {
  // Left held: one step at once, the next after 170 ms, then every 50 ms.
  Game g;
  g.newGame(1, 0);
  clearBoard(g);
  g.testSetPiece(P_I, 0, 3, 5, 0);
  g.press(ACT_LEFT, 0);
  CHECK(g.curX() == 2);
  g.tick(169);
  CHECK(g.curX() == 2);
  g.tick(170);
  CHECK(g.curX() == 1);
  g.tick(219);
  CHECK(g.curX() == 1);
  g.tick(220);
  CHECK(g.curX() == 0);
  g.tick(400);
  CHECK(g.curX() == 0);                             // wall
  g.release(ACT_LEFT, 400);
  g.press(ACT_RIGHT, 400);
  CHECK(g.curX() == 1);
  g.release(ACT_RIGHT, 410);
  g.tick(2000);
  CHECK(g.curX() == 1);                             // released: no more repeats

  // The result does not depend on how often tick() is called.
  Game a, b;
  a.newGame(7, 0);
  b.newGame(7, 0);
  clearBoard(a);
  clearBoard(b);
  a.testSetPiece(P_T, 0, 3, 3, 0);
  b.testSetPiece(P_T, 0, 3, 3, 0);
  a.press(ACT_RIGHT, 0);
  b.press(ACT_RIGHT, 0);
  a.press(ACT_SOFT, 0);
  b.press(ACT_SOFT, 0);
  for (uint32_t t = 1; t <= 900; t++) a.tick(t);
  b.tick(900);
  CHECK(a.curX() == b.curX() && a.curY() == b.curY() && a.score() == b.score());

  // Both keys down: the later one wins; releasing it resumes the other at the repeat rate.
  Game c;
  c.newGame(1, 0);
  clearBoard(c);
  c.testSetPiece(P_I, 0, 3, 5, 0);
  c.press(ACT_LEFT, 0);
  c.tick(100);
  c.press(ACT_RIGHT, 100);
  int x1 = c.curX();                                // left once, right once: back at 3
  CHECK(x1 == 3);
  c.tick(270);                                      // right's DAS expires at 270
  CHECK(c.curX() == 4);
  c.release(ACT_RIGHT, 270);
  c.tick(320);
  CHECK(c.curX() == 3);                             // left takes over
}

static void hold_piece() {
  Game g;
  g.newGame(3, 0);
  Piece first = g.current(), second = g.next(0);
  CHECK(g.held() == P_NONE);
  g.press(ACT_HOLD, 0);
  g.release(ACT_HOLD, 0);
  CHECK(g.held() == first && g.current() == second && g.holdUsed());
  g.press(ACT_HOLD, 1);                             // once per drop
  g.release(ACT_HOLD, 1);
  CHECK(g.held() == first && g.current() == second);
  clearBoard(g);
  g.press(ACT_HARD, 2);
  g.release(ACT_HARD, 2);
  CHECK(!g.holdUsed());
  Piece cur = g.current();
  g.press(ACT_HOLD, 3);
  g.release(ACT_HOLD, 3);
  CHECK(g.current() == first && g.held() == cur);
  CHECK(g.curRot() == 0);                           // comes back in the spawn orientation
}

static void game_over() {
  // Block out: the spawn cells are taken.
  Game g;
  g.newGame(1, 0);
  for (int x = 3; x < 7; x++) {
    g.testSetCell(x, 1, 1);
    g.testSetCell(x, 2, 1);
  }
  g.press(ACT_HARD, 0);
  g.release(ACT_HARD, 0);
  CHECK(g.state() == ST_OVER);
  CHECK(g.highScore() == g.score() && g.score() > 0 && g.newBest());
  // Keys do nothing on the game-over screen.
  g.press(ACT_LEFT, 10);
  CHECK(g.state() == ST_OVER);

  // Lock out: the piece comes to rest entirely above the visible well.
  Game h;
  h.newGame(1, 0);
  clearBoard(h);
  h.setHighScore(1000000);
  h.testSetCell(4, 2, 1);
  h.testSetCell(5, 2, 1);
  h.testSetPiece(P_O, 0, 4, 0, 0);
  h.press(ACT_HARD, 0);
  h.release(ACT_HARD, 0);
  CHECK(h.state() == ST_OVER && !h.newBest() && h.highScore() == 1000000);

  // The high score survives a new game; the score does not.
  g.newGame(2, 100);
  CHECK(g.score() == 0 && g.highScore() > 0 && !g.newBest() && g.state() == ST_PLAYING);
}

static void pause_freezes() {
  Game g;
  g.newGame(1, 0);
  int y0 = g.curY();
  g.pause(100);
  CHECK(g.state() == ST_PAUSED);
  g.tick(5000);
  CHECK(g.curY() == y0);
  g.press(ACT_HARD, 5000);                          // keys are ignored while paused
  CHECK(g.curY() == y0 && g.score() == 0);
  g.resume(5100);
  g.tick(5999);
  CHECK(g.curY() == y0 && g.state() == ST_PLAYING);   // gravity was due at 1000, now at 6000
  g.tick(6000);
  CHECK(g.curY() == y0 + 1);
  CHECK(g.nextEventAt() == 7000);
  g.pause(6001);
  CHECK(g.nextEventAt() == 0xFFFFFFFFu);
}

static void determinism() {
  auto play = [](uint32_t seed, uint32_t& score, int& blocks, Piece* seq) {
    Game g;
    g.newGame(seed, 0);
    uint32_t t = 0;
    for (int i = 0; i < 12 && g.state() != ST_OVER; i++) {
      seq[i] = g.current();
      Act a = (Act)(i % 3);                         // left, right, soft
      g.press(a, t);
      t += 130;
      g.tick(t);
      g.release(a, t);
      g.press(ACT_CW, t);
      g.release(ACT_CW, t);
      g.press(ACT_HARD, t);
      g.release(ACT_HARD, t);
      t += 300;
      g.tick(t);
    }
    score = g.score();
    blocks = countBlocks(g);
  };
  uint32_t s1, s2, s3;
  int b1, b2, b3;
  Piece q1[12] = {}, q2[12] = {}, q3[12] = {};
  play(99, s1, b1, q1);
  play(99, s2, b2, q2);
  play(100, s3, b3, q3);
  CHECK(s1 == s2 && b1 == b2 && memcmp(q1, q2, sizeof q1) == 0);
  CHECK(memcmp(q1, q3, sizeof q1) != 0);
}

static uint8_t gBuf[50 * 240];
static int blackPixels(const Bitmap1& bm) {
  int n = 0;
  for (int y = 0; y < bm.height(); y++)
    for (int x = 0; x < bm.width(); x++) n += bm.getPixel(x, y) == 0;
  return n;
}

static void app_wrapper() {
  TetrisApp app(5);
  Bitmap1 bm{gBuf, 400, 240, 50, 1};
  uint32_t t = 1000;
  app.begin(t);
  CHECK(app.game().state() == ST_TITLE);
  CHECK(app.wantsFrame(t));                         // nothing drawn yet
  memset(gBuf, 0xFF, sizeof gBuf);
  app.draw(bm, t);
  int title = blackPixels(bm);
  CHECK(title > 1500);
  CHECK(!app.wantsFrame(t) && app.nextWakeMs(t) == 0xFFFFFFFFu);   // idle: sleep until a key

  // MAIN starts at the press; the release that follows does not pause.
  app.onKey(KEY_MAIN, true, t);
  CHECK(app.game().state() == ST_PLAYING);
  app.onKey(KEY_MAIN, false, t + 80);
  CHECK(app.game().state() == ST_PLAYING);
  CHECK(app.wantsFrame(t + 80));
  memset(gBuf, 0xFF, sizeof gBuf);
  app.draw(bm, t + 80);
  CHECK(blackPixels(bm) > 1500 && !app.wantsFrame(t + 80));
  uint32_t wake = app.nextWakeMs(t + 80);
  CHECK(wake > t + 80 && wake <= t + 80 + 1000);     // the next gravity step

  // The time of a gravity step brings a new frame.
  t += 1100;
  app.tick(t);
  CHECK(app.wantsFrame(t));
  app.draw(bm, t);

  // Short press of MAIN pauses at the release, the next short press resumes.
  app.onKey(KEY_MAIN, true, t);
  CHECK(app.game().state() == ST_PLAYING);
  app.onKey(KEY_MAIN, false, t + 100);
  CHECK(app.game().state() == ST_PAUSED);
  memset(gBuf, 0xFF, sizeof gBuf);
  app.draw(bm, t + 100);
  t += 5000;
  app.onKey(KEY_MAIN, true, t);
  app.onKey(KEY_MAIN, false, t + 50);
  CHECK(app.game().state() == ST_PLAYING);

  // Long press while playing swaps the hold piece and does not pause.
  t += 100;
  Piece cur = app.game().current();
  app.onKey(KEY_MAIN, true, t);
  app.tick(t + 399);
  CHECK(app.game().held() == P_NONE);
  app.draw(bm, t);
  CHECK(app.nextWakeMs(t + 399) <= t + 400);
  app.tick(t + 400);
  CHECK(app.game().held() == cur);
  app.onKey(KEY_MAIN, false, t + 600);
  CHECK(app.game().state() == ST_PLAYING);

  // Keys reach the rules.
  int x = app.game().curX();
  app.onKey(KEY_LEFT, true, t + 700);
  app.onKey(KEY_LEFT, false, t + 720);
  CHECK(app.game().curX() == x - 1);
  int r = app.game().curRot();
  app.onKey(KEY_ACTION, true, t + 730);
  app.onKey(KEY_ACTION, false, t + 740);
  CHECK(app.game().curRot() == ((r + 1) & 3) || app.game().current() == P_O);
  app.onKey(KEY_B, true, t + 750);
  app.onKey(KEY_B, false, t + 760);
  CHECK(app.game().curRot() == r || app.game().current() == P_O);

  // Game over screen restarts with MAIN.
  Game& g = app.game();
  for (int i = 0; i < 40 && g.state() != ST_OVER; i++) {
    app.onKey(KEY_UP, true, t + 800 + i);
    app.onKey(KEY_UP, false, t + 800 + i);
  }
  CHECK(g.state() == ST_OVER);
  memset(gBuf, 0xFF, sizeof gBuf);
  app.draw(bm, t + 900);
  CHECK(blackPixels(bm) > 1500);
  app.onKey(KEY_MAIN, true, t + 1000);
  app.onKey(KEY_MAIN, false, t + 1100);
  CHECK(g.state() == ST_PLAYING && g.score() == 0);

  // A line clear asks for a frame at every blink step.
  uint32_t u = t + 2000;
  clearBoard(g);
  for (int x2 = 0; x2 < 9; x2++) g.testSetCell(x2, 21, 3);
  g.testSetPiece(P_I, 1, 7, 0, u);
  app.onKey(KEY_UP, true, u);
  app.onKey(KEY_UP, false, u);
  CHECK(g.state() == ST_CLEARING);
  app.draw(bm, u);
  CHECK(!app.wantsFrame(u + 10));
  CHECK(app.wantsFrame(u + TetrisView::FLASH_PHASE_MS));
  CHECK(app.nextWakeMs(u + 10) == u + TetrisView::FLASH_PHASE_MS);
  app.draw(bm, u + TetrisView::FLASH_PHASE_MS);
  app.tick(u + g.cfg.clearMs);
  CHECK(g.state() == ST_PLAYING && app.wantsFrame(u + g.cfg.clearMs));
}

// Draw every state with sanitizers watching for out-of-range writes.
static void view_states() {
  TetrisView v;
  Bitmap1 bm{gBuf, 400, 240, 50, 1};
  Game g;
  for (int rot = 0; rot < 4; rot++) {
    Bitmap1 b{gBuf, 400, 240, 50, (uint8_t)rot};
    memset(gBuf, 0xFF, sizeof gBuf);
    v.draw(b, g, 0);                                // title
    g.newGame(4, 0);
    for (int i = 0; i < 8; i++) {                   // a few pieces down
      g.press(i & 1 ? ACT_LEFT : ACT_RIGHT, i);
      g.release(i & 1 ? ACT_LEFT : ACT_RIGHT, i);
      g.press(ACT_HARD, i);
      g.release(ACT_HARD, i);
    }
    v.draw(b, g, 100);
    g.pause(100);
    v.draw(b, g, 100);
    g.resume(100);
  }
  // Same picture whatever the rotation of the buffer: compare through the logical view.
  Bitmap1 b1{gBuf, 400, 240, 50, 1};
  static uint8_t buf2[50 * 240];
  Bitmap1 b3{buf2, 400, 240, 50, 3};
  memset(gBuf, 0xFF, sizeof gBuf);
  memset(buf2, 0xFF, sizeof buf2);
  v.draw(b1, g, 100);
  v.draw(b3, g, 100);
  bool same = true;
  for (int y = 0; y < 400 && same; y++)
    for (int x = 0; x < 240; x++)
      if (b1.getPixel(x, y) != b3.getPixel(x, y)) { same = false; break; }
  CHECK(same);
  // Every piece type has its own look.
  static uint8_t cellBuf[50 * 240];
  uint32_t hash[8] = {};
  for (int t = 1; t <= 7; t++) {
    memset(cellBuf, 0xFF, sizeof cellBuf);
    Bitmap1 cb{cellBuf, 400, 240, 50, 1};
    TetrisView::drawCell(cb, 10, 10, 18, (uint8_t)t);
    uint32_t h = 2166136261u;
    for (int y = 10; y < 28; y++)
      for (int x = 10; x < 28; x++) h = (h ^ cb.getPixel(x, y)) * 16777619u;
    hash[t] = h;
    for (int u = 1; u < t; u++) CHECK(hash[u] != h);
  }
}

static void test_tetris() {
  shapes();
  rotation_and_kicks();
  bag();
  scoring();
  levels();
  lock_delay();
  das();
  hold_piece();
  game_over();
  pause_freezes();
  determinism();
  app_wrapper();
  view_states();
}

}  // namespace tt
