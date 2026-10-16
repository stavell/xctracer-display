#pragma once
// Tetris as an IApp: keys -> rules (tetris.h), rules -> picture (tetris_view.h), and the frame
// request API the host uses to draw only when something changed.
//
// Keys: LEFT / RIGHT move (auto-repeat), DOWN soft drop, UP hard drop, ACTION (A) rotate clockwise,
// B rotate counter-clockwise. MAIN is the single switch of the current hardware: a short press
// (acted on at release) pauses and resumes, and starts or restarts on the title and game-over screens
// (acted on at press); a long press (400 ms) while playing swaps the hold piece, so every function
// works with one key and nothing is lost on the six-button console either.
// Paused: a short press (or A) resumes, a long press (or B) goes Home to the launcher. In the box image the host also
// takes the switch's 3 s hold (and A + B held 1.5 s) to go back to the launcher, in every state.
//
// Frames: wantsFrame(now) is true when the picture differs from the last draw(); nextWakeMs(now) is the
// next time anything will change by itself (gravity step, lock, key repeat, clear-animation blink,
// long-press threshold), so the device can sleep until then or until a key edge.
//
// High score: kept in memory; a host that persists it passes an IScoreStore (load at begin(), save
// when a game ends with a new best).
#include <stdint.h>
#include "apps/games/tetris/tetris.h"
#include "apps/games/tetris/tetris_view.h"
#include "platform/app_host.h"

struct IScoreStore {
  virtual ~IScoreStore() {}
  virtual uint32_t load() = 0;
  virtual void save(uint32_t score) = 0;
};

class TetrisApp : public IApp {
 public:
  static const uint32_t LONG_PRESS_MS = 400;

  explicit TetrisApp(uint32_t seed = 1) : seed_(seed) {}
  void setScoreStore(IScoreStore* s) { store_ = s; }

  const char* name() const override { return "tetris"; }
  void begin(uint32_t nowMs) override;
  void end() override {}
  void tick(uint32_t nowMs) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  bool wantsFrame(uint32_t nowMs) override;
  uint32_t nextWakeMs(uint32_t nowMs) override;
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  tetris::Game& game() { return game_; }
  const tetris::Game& game() const { return game_; }

 private:
  void afterStep();
  void startGame(uint32_t now);

  tetris::Game game_;
  TetrisView view_;
  IScoreStore* store_ = nullptr;
  uint32_t seed_;
  uint32_t games_ = 0;
  uint32_t drawnVersion_ = 0;
  int drawnPhase_ = -1;
  bool everDrawn_ = false;
  bool pauseDown_ = false;                       // the press in progress started on the pause screen
  bool mainDown_ = false, longPending_ = false, longFired_ = false, ignoreRelease_ = false;
  uint32_t mainDownAt_ = 0;
  bool saved_ = false;
};
