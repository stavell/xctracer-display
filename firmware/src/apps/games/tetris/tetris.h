#pragma once
// Tetris rules, pure C++: no drawing, no hardware, time is passed in (milliseconds, any origin).
//
// Guideline rules: 10x20 well with two hidden rows above it, 7-bag randomizer, SRS rotation with wall
// kicks, lock delay with a move-reset limit, DAS / ARR, hold, next queue, guideline scoring. Not
// implemented: T-spin and combo scoring, back-to-back bonus, initial rotation / hold.
//
// Driving it: press() / release() with the time of each key edge, tick(now) as often as convenient (the
// result does not depend on how often: every timer is an absolute deadline and tick() runs them in
// order). Pause shifts all deadlines, so a paused game does not age.
//
// Coordinates: x 0..9 left to right, y 0..21 top to bottom; rows 0 and 1 are hidden, the visible well
// is rows 2..21.
#include <stdint.h>

namespace tetris {

enum Piece : uint8_t { P_NONE = 0, P_I, P_O, P_T, P_S, P_Z, P_J, P_L };
// Player actions; the app maps keys onto them.
enum Act : uint8_t { ACT_LEFT, ACT_RIGHT, ACT_SOFT, ACT_CW, ACT_CCW, ACT_HARD, ACT_HOLD, ACT_COUNT };
enum State : uint8_t { ST_TITLE, ST_PLAYING, ST_CLEARING, ST_PAUSED, ST_OVER };

struct Block { int8_t x, y; };

struct Config {
  uint16_t dasMs = 170;       // delay before auto-repeat
  uint16_t arrMs = 50;        // auto-repeat interval
  uint16_t softMs = 25;       // soft drop: one row per this long while the key is held
  uint16_t lockMs = 500;      // lock delay
  uint8_t lockResets = 15;    // moves / rotations that restart the lock delay, per piece
  uint16_t clearMs = 180;     // line-clear animation; the game is frozen meanwhile
};

class Game {
 public:
  static const int W = 10, H = 22, HIDDEN = 2, VISIBLE = 20, NEXT_N = 3;

  Game();
  Config cfg;

  // Lifecycle
  void newGame(uint32_t seed, uint32_t now);      // clear everything and start playing
  void toTitle();                                  // back to the title screen (keeps the high score)
  void pause(uint32_t now);
  void resume(uint32_t now);
  void tick(uint32_t now);
  void press(Act a, uint32_t now);
  void release(Act a, uint32_t now);
  // Absolute time of the next thing that happens by itself (drop, lock, repeat, end of the clear
  // animation); UINT32_MAX if nothing does (title, pause, game over).
  uint32_t nextEventAt() const;

  // State for the view
  State state() const { return state_; }
  uint8_t cell(int x, int y) const { return (x < 0 || x >= W || y < 0 || y >= H) ? 0 : board_[y][x]; }
  Piece current() const { return cur_; }
  int curRot() const { return rot_; }
  int curX() const { return px_; }
  int curY() const { return py_; }
  void curBlocks(Block out[4]) const;              // board coordinates of the falling piece
  int ghostY() const;                              // y the falling piece would land at
  Piece next(int i) const { return (i >= 0 && i < NEXT_N) ? queue_[i] : P_NONE; }
  Piece held() const { return hold_; }
  bool holdUsed() const { return holdUsed_; }
  uint32_t score() const { return score_; }
  uint32_t highScore() const { return high_; }
  void setHighScore(uint32_t h) { high_ = h; }     // restore from a settings store
  bool newBest() const { return newBest_; }
  int lines() const { return lines_; }
  int level() const { return level_; }
  uint32_t clearRows() const { return clearMask_; }   // bit y set: row y is being cleared
  uint32_t clearStart() const { return clearEnd_ - cfg.clearMs; }
  uint32_t version() const { return version_; }    // bumps whenever the picture changes
  uint32_t gravityMs() const;                      // time per row at the current level
  bool grounded() const { return grounded_; }
  int lockResetsUsed() const { return resets_; }

  // Shapes (also for the view's previews): the four blocks of piece t in rotation rot, in its box.
  static const Block* shape(Piece t, int rot);
  static int boxSize(Piece t) { return (t == P_I || t == P_O) ? 4 : 3; }
  static uint32_t gravityMsForLevel(int level);

  // Test seams: set up positions without playing to them.
  void testSetPiece(Piece t, int rot, int x, int y, uint32_t now);
  void testSetCell(int x, int y, uint8_t v) { board_[y][x] = v; }

 private:
  bool collides(Piece t, int rot, int x, int y) const;
  bool tryMove(int dx, int dy);
  bool tryRotate(int dir);                         // +1 clockwise, -1 counter-clockwise
  void spawn(Piece t, uint32_t now);
  void spawnNext(uint32_t now);
  void landed(uint32_t now, bool moved);           // re-evaluate ground contact after a move
  void lock(uint32_t now);
  void finishClear(uint32_t now);
  void gameOver();
  void doHold(uint32_t now);
  void hardDrop(uint32_t now);
  void moveSide(int dir, uint32_t now);
  Piece pull();
  void fillQueue();
  uint32_t rnd();
  void shiftTimes(uint32_t delta);
  void dasStart(int dir, uint32_t now);
  void bump() { version_++; }

  uint8_t board_[H][W];
  State state_ = ST_TITLE;
  Piece cur_ = P_NONE, hold_ = P_NONE;
  Piece queue_[NEXT_N + 4];
  int qn_ = 0;
  Piece bag_[7];
  int bagPos_ = 7;
  uint32_t rng_ = 1;
  int rot_ = 0, px_ = 0, py_ = 0, lowest_ = 0, resets_ = 0;
  bool grounded_ = false, holdUsed_ = false, newBest_ = false;
  uint32_t score_ = 0, high_ = 0, version_ = 0;
  int lines_ = 0, level_ = 1;
  // Deadlines (absolute ms)
  uint32_t gravAt_ = 0, lockAt_ = 0, softAt_ = 0, dasAt_ = 0, clearEnd_ = 0, pausedAt_ = 0;
  uint32_t clearMask_ = 0;
  State pausedFrom_ = ST_PLAYING;
  bool held_[ACT_COUNT] = {};
  int dasDir_ = 0;                                 // -1, 0, +1: direction being repeated
};

}  // namespace tetris
