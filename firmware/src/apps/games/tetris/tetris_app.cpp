#include "apps/games/tetris/tetris_app.h"

using namespace tetris;

void TetrisApp::begin(uint32_t nowMs) {
  (void)nowMs;
  if (store_) game_.setHighScore(store_->load());
  game_.toTitle();
  mainDown_ = longPending_ = longFired_ = ignoreRelease_ = false;
  everDrawn_ = false;
}

void TetrisApp::startGame(uint32_t now) {
  games_++;
  game_.newGame(seed_ * 2654435761u + games_ * 40503u + now, now);
  saved_ = false;
}

// After any change of the rules: remember a new best.
void TetrisApp::afterStep() {
  if (game_.state() == ST_OVER && !saved_) {
    saved_ = true;
    if (store_ && game_.newBest()) store_->save(game_.highScore());
  }
}

void TetrisApp::tick(uint32_t nowMs) {
  game_.tick(nowMs);
  if (longPending_ && (int32_t)(nowMs - (mainDownAt_ + LONG_PRESS_MS)) >= 0) {
    longPending_ = false;
    longFired_ = true;
    if (game_.state() == ST_PLAYING) {
      game_.press(ACT_HOLD, nowMs);
      game_.release(ACT_HOLD, nowMs);
    }
  }
  afterStep();
}

void TetrisApp::onKey(Key key, bool down, uint32_t nowMs) {
  tick(nowMs);                                     // bring the game up to the time of the edge first
  Act a = ACT_COUNT;
  switch (key) {
    case KEY_LEFT: a = ACT_LEFT; break;
    case KEY_RIGHT: a = ACT_RIGHT; break;
    case KEY_DOWN: a = ACT_SOFT; break;
    case KEY_UP: a = ACT_HARD; break;
    case KEY_ACTION: a = ACT_CW; break;
    case KEY_B: a = ACT_CCW; break;
    default: break;
  }
  if (a != ACT_COUNT) {
    if (down) game_.press(a, nowMs);
    else game_.release(a, nowMs);
    afterStep();
    return;
  }
  if (key != KEY_MAIN) return;
  State st = game_.state();
  if (down) {
    if (mainDown_) return;                         // host key repeat
    mainDown_ = true;
    longFired_ = false;
    if (st == ST_TITLE || st == ST_OVER) {
      startGame(nowMs);
      ignoreRelease_ = true;
    } else if (st == ST_PLAYING) {
      longPending_ = true;
      mainDownAt_ = nowMs;
    }
  } else {
    mainDown_ = false;
    longPending_ = false;
    if (ignoreRelease_) {
      ignoreRelease_ = false;
    } else if (longFired_) {
      longFired_ = false;
    } else if (st == ST_PLAYING || st == ST_CLEARING) {
      game_.pause(nowMs);
    } else if (st == ST_PAUSED) {
      game_.resume(nowMs);
    }
  }
  afterStep();
}

bool TetrisApp::wantsFrame(uint32_t nowMs) {
  if (!everDrawn_ || game_.version() != drawnVersion_) return true;
  return game_.state() == ST_CLEARING && TetrisView::flashPhase(game_, nowMs) != drawnPhase_;
}

uint32_t TetrisApp::nextWakeMs(uint32_t nowMs) {
  if (wantsFrame(nowMs)) return nowMs;
  uint32_t t = game_.nextEventAt();
  auto take = [&](uint32_t c) {
    if (t == 0xFFFFFFFFu || (int32_t)(c - t) < 0) t = c;
  };
  uint32_t f = TetrisView::nextFlashAt(game_, nowMs);
  if (f != 0xFFFFFFFFu) take(f);
  if (longPending_) take(mainDownAt_ + LONG_PRESS_MS);
  return t;
}

void TetrisApp::draw(Bitmap1& bm, uint32_t nowMs) {
  view_.draw(bm, game_, nowMs);
  drawnVersion_ = game_.version();
  drawnPhase_ = TetrisView::flashPhase(game_, nowMs);
  everDrawn_ = true;
}
