#include "apps/games/tetris/tetris.h"
#include <string.h>

namespace tetris {

// ---- shapes: spawn orientation per piece (blocks in a box of 3x3, or 4x4 for I and O, y down), the
// other three rotations derived by turning the box clockwise: (x, y) -> (n - 1 - y, x).
static Block gShape[8][4][4];
static bool gShapeInit = false;

static void initShapes() {
  if (gShapeInit) return;
  static const Block spawn[8][4] = {
      {},
      {{0, 1}, {1, 1}, {2, 1}, {3, 1}},   // I
      {{1, 0}, {2, 0}, {1, 1}, {2, 1}},   // O
      {{1, 0}, {0, 1}, {1, 1}, {2, 1}},   // T
      {{1, 0}, {2, 0}, {0, 1}, {1, 1}},   // S
      {{0, 0}, {1, 0}, {1, 1}, {2, 1}},   // Z
      {{0, 0}, {0, 1}, {1, 1}, {2, 1}},   // J
      {{2, 0}, {0, 1}, {1, 1}, {2, 1}},   // L
  };
  for (int t = 1; t < 8; t++) {
    int n = Game::boxSize((Piece)t);
    for (int i = 0; i < 4; i++) gShape[t][0][i] = spawn[t][i];
    for (int r = 1; r < 4; r++)
      for (int i = 0; i < 4; i++) {
        if (t == P_O) { gShape[t][r][i] = gShape[t][0][i]; continue; }   // the O never moves when turned
        const Block& b = gShape[t][r - 1][i];
        gShape[t][r][i] = Block{(int8_t)(n - 1 - b.y), b.x};
      }
  }
  gShapeInit = true;
}

const Block* Game::shape(Piece t, int rot) {
  initShapes();
  return gShape[t & 7][rot & 3];
}

// ---- SRS wall kicks (from the Tetris wiki, y up there; converted to y down where they are applied).
// Transition index: 0>>1, 1>>0, 1>>2, 2>>1, 2>>3, 3>>2, 3>>0, 0>>3.
static const int8_t kKickJLSTZ[8][5][2] = {
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
};
static const int8_t kKickI[8][5][2] = {
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
};

// Guideline gravity (seconds per row = (0.8 - (L-1) * 0.007)^(L-1)), rounded to ms.
uint32_t Game::gravityMsForLevel(int level) {
  static const uint16_t t[] = {1000, 793, 618, 473, 355, 262, 190, 135, 94, 64, 43, 28, 18, 11, 7, 4, 3, 2, 1};
  if (level < 1) level = 1;
  if (level > 19) level = 19;
  return t[level - 1];
}
uint32_t Game::gravityMs() const { return gravityMsForLevel(level_); }

Game::Game() {
  initShapes();
  memset(board_, 0, sizeof board_);
  memset(queue_, 0, sizeof queue_);
  memset(bag_, 0, sizeof bag_);
}

uint32_t Game::rnd() {            // xorshift32
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rng_ = x;
}

Piece Game::pull() {              // 7-bag: every piece once per seven, Fisher-Yates shuffle
  if (bagPos_ >= 7) {
    for (int i = 0; i < 7; i++) bag_[i] = (Piece)(i + 1);
    for (int i = 6; i > 0; i--) {
      int j = (int)(rnd() % (uint32_t)(i + 1));
      Piece t = bag_[i];
      bag_[i] = bag_[j];
      bag_[j] = t;
    }
    bagPos_ = 0;
  }
  return bag_[bagPos_++];
}

void Game::fillQueue() {
  while (qn_ < NEXT_N + 1) queue_[qn_++] = pull();
}

// ---- lifecycle
void Game::newGame(uint32_t seed, uint32_t now) {
  memset(board_, 0, sizeof board_);
  rng_ = seed ? seed : 0x9E3779B9u;
  for (int i = 0; i < 4; i++) rnd();
  bagPos_ = 7;
  qn_ = 0;
  hold_ = P_NONE;
  holdUsed_ = false;
  score_ = 0;
  lines_ = 0;
  level_ = 1;
  newBest_ = false;
  clearMask_ = 0;
  dasDir_ = 0;
  for (bool& h : held_) h = false;
  state_ = ST_PLAYING;
  fillQueue();
  spawnNext(now);
  bump();
}

void Game::toTitle() {
  state_ = ST_TITLE;
  cur_ = P_NONE;
  dasDir_ = 0;
  bump();
}

void Game::gameOver() {
  state_ = ST_OVER;
  if (score_ > high_) {
    high_ = score_;
    newBest_ = true;
  }
  bump();
}

void Game::shiftTimes(uint32_t d) {
  gravAt_ += d;
  softAt_ += d;
  dasAt_ += d;
  if (lockAt_) lockAt_ += d;
  clearEnd_ += d;
}

void Game::pause(uint32_t now) {
  if (state_ != ST_PLAYING && state_ != ST_CLEARING) return;
  pausedFrom_ = state_;
  pausedAt_ = now;
  state_ = ST_PAUSED;
  bump();
}

void Game::resume(uint32_t now) {
  if (state_ != ST_PAUSED) return;
  shiftTimes(now - pausedAt_);
  state_ = pausedFrom_;
  bump();
}

// ---- geometry
bool Game::collides(Piece t, int rot, int x, int y) const {
  const Block* b = shape(t, rot);
  for (int i = 0; i < 4; i++) {
    int cx = x + b[i].x, cy = y + b[i].y;
    if (cx < 0 || cx >= W || cy >= H) return true;
    if (cy >= 0 && board_[cy][cx]) return true;
  }
  return false;
}

void Game::curBlocks(Block out[4]) const {
  if (cur_ == P_NONE) {
    for (int i = 0; i < 4; i++) out[i] = Block{-1, -1};
    return;
  }
  const Block* b = shape(cur_, rot_);
  for (int i = 0; i < 4; i++) out[i] = Block{(int8_t)(px_ + b[i].x), (int8_t)(py_ + b[i].y)};
}

int Game::ghostY() const {
  if (cur_ == P_NONE) return 0;
  int y = py_;
  while (!collides(cur_, rot_, px_, y + 1)) y++;
  return y;
}

bool Game::tryMove(int dx, int dy) {
  if (collides(cur_, rot_, px_ + dx, py_ + dy)) return false;
  px_ += dx;
  py_ += dy;
  bump();
  return true;
}

bool Game::tryRotate(int dir) {
  if (cur_ == P_O) return false;                  // nothing changes, nothing to reset
  int to = (rot_ + dir) & 3;
  // Transition index: clockwise from r is 2r, counter-clockwise from r is 2 * ((r + 3) & 3) + 1.
  int idx = dir > 0 ? rot_ * 2 : ((rot_ + 3) & 3) * 2 + 1;
  const int8_t(*k)[2] = cur_ == P_I ? kKickI[idx] : kKickJLSTZ[idx];
  for (int i = 0; i < 5; i++) {
    int nx = px_ + k[i][0], ny = py_ - k[i][1];   // wiki kicks are y up
    if (!collides(cur_, to, nx, ny)) {
      px_ = nx;
      py_ = ny;
      rot_ = to;
      bump();
      return true;
    }
  }
  return false;
}

// ---- piece flow
void Game::spawn(Piece t, uint32_t now) {
  cur_ = t;
  rot_ = 0;
  px_ = 3;
  py_ = 1;
  if (collides(cur_, rot_, px_, py_)) {            // block out
    gameOver();
    return;
  }
  if (!collides(cur_, rot_, px_, py_ + 1)) py_++;  // guideline: drops one row at once
  lowest_ = py_;
  resets_ = 0;
  grounded_ = false;
  lockAt_ = 0;
  gravAt_ = now + gravityMs();
  softAt_ = now + cfg.softMs;
  if (dasDir_ && (int32_t)(dasAt_ - now) < 0) dasAt_ = now;   // a held direction goes on repeating
  landed(now, false);
  bump();
}

void Game::spawnNext(uint32_t now) {
  Piece t = queue_[0];
  for (int i = 1; i < qn_; i++) queue_[i - 1] = queue_[i];
  qn_--;
  fillQueue();
  spawn(t, now);
}

// Ground contact after a piece change. A landing or a move on the ground restarts the lock delay unless
// the piece used up its resets; reaching a lower row than before gives the resets back.
void Game::landed(uint32_t now, bool moved) {
  bool newLow = py_ > lowest_;
  if (newLow) {
    lowest_ = py_;
    resets_ = 0;
  }
  bool g = collides(cur_, rot_, px_, py_ + 1);
  if (!g) {
    if (grounded_) gravAt_ = now + gravityMs();    // slid off a ledge: a full interval before it falls
    grounded_ = false;
    lockAt_ = 0;
    return;
  }
  if (!grounded_ || moved) {
    if (resets_ < cfg.lockResets) {
      lockAt_ = now + cfg.lockMs;
      if (!newLow && (grounded_ || moved)) resets_++;
    } else if (!grounded_) {
      lockAt_ = now;                               // out of resets: locks as soon as it touches down
    }
  }
  grounded_ = true;
}

void Game::lock(uint32_t now) {
  Block b[4];
  curBlocks(b);
  bool anyVisible = false;
  for (int i = 0; i < 4; i++) {
    if (b[i].y >= HIDDEN) anyVisible = true;
    if (b[i].y >= 0) board_[b[i].y][b[i].x] = (uint8_t)cur_;
  }
  cur_ = P_NONE;
  holdUsed_ = false;
  grounded_ = false;
  lockAt_ = 0;
  if (!anyVisible) {                               // lock out
    gameOver();
    return;
  }
  uint32_t mask = 0;
  int n = 0;
  for (int y = 0; y < H; y++) {
    bool full = true;
    for (int x = 0; x < W; x++)
      if (!board_[y][x]) { full = false; break; }
    if (full) {
      mask |= 1u << y;
      n++;
    }
  }
  if (!n) {
    spawnNext(now);
    return;
  }
  static const uint16_t pts[5] = {0, 100, 300, 500, 800};
  score_ += (uint32_t)pts[n] * (uint32_t)level_;
  lines_ += n;
  level_ = 1 + lines_ / 10;
  clearMask_ = mask;
  clearEnd_ = now + cfg.clearMs;
  state_ = ST_CLEARING;
  bump();
}

void Game::finishClear(uint32_t now) {
  int dst = H - 1;
  for (int src = H - 1; src >= 0; src--) {
    if (clearMask_ & (1u << src)) continue;
    if (dst != src) memcpy(board_[dst], board_[src], W);
    dst--;
  }
  for (; dst >= 0; dst--) memset(board_[dst], 0, W);
  clearMask_ = 0;
  state_ = ST_PLAYING;
  spawnNext(now);
  bump();
}

void Game::hardDrop(uint32_t now) {
  int y = ghostY();
  score_ += 2u * (uint32_t)(y - py_);
  py_ = y;
  lock(now);
  bump();
}

void Game::doHold(uint32_t now) {
  if (holdUsed_ || cur_ == P_NONE) return;
  Piece c = cur_;
  if (hold_ == P_NONE) {
    hold_ = c;
    spawnNext(now);
  } else {
    Piece h = hold_;
    hold_ = c;
    spawn(h, now);
  }
  if (state_ == ST_PLAYING) holdUsed_ = true;
  bump();
}

void Game::moveSide(int dir, uint32_t now) {
  if (tryMove(dir, 0)) landed(now, true);
}

void Game::dasStart(int dir, uint32_t now) {
  dasDir_ = dir;
  dasAt_ = now + cfg.dasMs;
}

// ---- input
void Game::press(Act a, uint32_t now) {
  if (a >= ACT_COUNT) return;
  bool wasHeld = held_[a];
  held_[a] = true;
  if (wasHeld) return;                             // key auto-repeat from a host: ignore
  if (state_ != ST_PLAYING && state_ != ST_CLEARING) return;
  // Directions are tracked while a line clears too, so a key held through it keeps repeating.
  if (a == ACT_LEFT || a == ACT_RIGHT) {
    int dir = a == ACT_LEFT ? -1 : 1;
    if (state_ == ST_PLAYING && cur_ != P_NONE) moveSide(dir, now);
    dasStart(dir, now);
    return;
  }
  if (state_ != ST_PLAYING || cur_ == P_NONE) return;
  switch (a) {
    case ACT_SOFT:
      if (tryMove(0, 1)) {
        score_++;
        landed(now, true);
      }
      softAt_ = now + cfg.softMs;
      break;
    case ACT_CW:
      if (tryRotate(1)) landed(now, true);
      break;
    case ACT_CCW:
      if (tryRotate(-1)) landed(now, true);
      break;
    case ACT_HARD:
      hardDrop(now);
      break;
    case ACT_HOLD:
      doHold(now);
      break;
    default:
      break;
  }
}

void Game::release(Act a, uint32_t now) {
  if (a >= ACT_COUNT) return;
  held_[a] = false;
  if ((a == ACT_LEFT || a == ACT_RIGHT) && dasDir_ == (a == ACT_LEFT ? -1 : 1)) {
    Act other = a == ACT_LEFT ? ACT_RIGHT : ACT_LEFT;
    if (held_[other]) {                            // the other direction takes over, already charged
      dasDir_ = a == ACT_LEFT ? 1 : -1;
      dasAt_ = now + cfg.arrMs;
    } else {
      dasDir_ = 0;
    }
  }
}

// ---- time
void Game::tick(uint32_t now) {
  if (state_ == ST_CLEARING && (int32_t)(now - clearEnd_) >= 0) finishClear(clearEnd_);
  // Run every deadline that has passed, earliest first, so the result does not depend on tick spacing.
  for (int guard = 0; guard < 400 && state_ == ST_PLAYING && cur_ != P_NONE; guard++) {
    uint32_t t = nextEventAt();
    if (t == 0xFFFFFFFFu || (int32_t)(t - now) > 0) break;
    if (dasDir_ && t == dasAt_) {
      dasAt_ += cfg.arrMs;
      moveSide(dasDir_, t);
    } else if (held_[ACT_SOFT] && !grounded_ && t == softAt_) {
      softAt_ += cfg.softMs;
      if (tryMove(0, 1)) {
        score_++;
        landed(t, true);
      }
    } else if (lockAt_ && t == lockAt_) {
      lock(t);
    } else if (!grounded_ && t == gravAt_) {
      gravAt_ += gravityMs();
      if (tryMove(0, 1)) landed(t, false);
    } else {
      break;
    }
  }
}

uint32_t Game::nextEventAt() const {
  if (state_ == ST_CLEARING) return clearEnd_;
  if (state_ != ST_PLAYING || cur_ == P_NONE) return 0xFFFFFFFFu;
  uint32_t t = 0xFFFFFFFFu;
  auto take = [&](uint32_t c) {
    if (t == 0xFFFFFFFFu || (int32_t)(c - t) < 0) t = c;
  };
  if (!grounded_) take(gravAt_);
  if (lockAt_) take(lockAt_);
  if (dasDir_) take(dasAt_);
  if (held_[ACT_SOFT] && !grounded_) take(softAt_);
  return t;
}

void Game::testSetPiece(Piece t, int rot, int x, int y, uint32_t now) {
  cur_ = t;
  rot_ = rot & 3;
  px_ = x;
  py_ = y;
  lowest_ = y;
  resets_ = 0;
  grounded_ = false;
  lockAt_ = 0;
  gravAt_ = now + gravityMs();
  softAt_ = now + cfg.softMs;
  landed(now, false);
  bump();
}

}  // namespace tetris
