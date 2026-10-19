#include "apps/games/sim/sim_app.h"
#include <stdio.h>
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

namespace sim {

namespace {
constexpr float kMapMPerPx = 40.0f;
// The map is north-up: a heading-up picture has to be re-rendered whenever the glider turns, and at x3 a circle takes 5 s (70 degrees per
// second): the 90 000-pixel picture (about 0.3 s of CPU on the device) could not keep up, so it lagged and jumped on every swap (measured:
// choppy). North-up needs a new picture only when the glider has moved a few pixels (about 1 px/s at x3); the glider symbol turns.
constexpr bool kMapHeadingUp = false;
constexpr int kRenderBudgetPx = 1500;          // top-down picture pixels per frame (about 60 frames per picture, ~5 ms a frame)

float approach(float v, float target, float rateUp, float rateDown, float dt) {
  const float rate = (fabsf(target) > fabsf(v) || (target * v < 0)) ? rateUp : rateDown;
  const float d = target - v, s = rate * dt;
  return fabsf(d) <= s ? target : v + (d > 0 ? s : -s);
}
char gRows[SimApp::kMenuRows][28];
}  // namespace

constexpr float SimApp::kWindKmh[SimApp::kWindSpeeds];

const char* SimApp::dirName(int idx) {
  static const char* n[9] = {"usual", "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return n[idx < 0 ? 0 : (idx > 8 ? 8 : idx)];
}

void SimApp::setDay(int level, int dirIdx, int spdIdx) {
  level_ = level < 0 ? 0 : (level >= Air::kLevels ? Air::kLevels - 1 : level);
  dirIdx_ = dirIdx < 0 ? 0 : (dirIdx > 8 ? 8 : dirIdx);
  spdIdx_ = spdIdx < 0 ? 0 : (spdIdx > kWindSpeeds ? kWindSpeeds : spdIdx);
}

void SimApp::resolveWind(float* fromDeg, float* speed) const {
  *fromDeg = dirIdx_ == 0 ? usualFrom_ : (dirIdx_ - 1) * 45.0f;
  *speed = spdIdx_ == 0 ? usualSpeed_ : kWindKmh[spdIdx_ - 1] / 3.6f;
}

void SimApp::applyWind() {
  float from, sp;
  resolveWind(&from, &sp);
  world_.setWindTarget(from, sp);
}

void SimApp::begin(uint32_t nowMs) {
  (void)nowMs;
  left_ = right_ = up_ = down_ = false;
  c_ = Controls();
  frame_ = 0;
  if (tilt_) tilt_->enable(true);
  if (oneButton_ && tilt_) ctl_ = Control::TILT;      // the only way to steer
  calibrated_ = false;
  if (dayScreen_ && source()) { mode_ = Mode::DAY; dayRow_ = 0; return; }
  startFlight();
}

IHeightSource* SimApp::source() {
  if (source_) return source_;
  if (!terrain_ || !terrain_->isOpen()) return nullptr;
  tsrc_.set(terrain_, kTakeoffs[site_].lat, kTakeoffs[site_].lon);
  return &tsrc_;
}

void SimApp::dropViews() {
  if (topV_) topV_->~TopDownView();
  if (chaseV_) chaseV_->~ChaseViews();
  topV_ = nullptr;
  chaseV_ = nullptr;
}

TopDownView& SimApp::top() {
  if (!topV_) {
    dropViews();
    topV_ = new (views_) TopDownView();
  }
  return *topV_;
}

SimApp::ChaseViews& SimApp::chaseViews() {
  if (!chaseV_) {
    dropViews();
    chaseV_ = new (views_) ChaseViews();
  }
  return *chaseV_;
}

VoxelView& SimApp::voxel() { return chaseViews().voxel; }
SkyFx& SimApp::sky() { return chaseViews().sky; }

void SimApp::startFlight() {
  IHeightSource* src = source();
  const Takeoff& to = kTakeoffs[site_];
  if (!src) { mode_ = Mode::NOMAP; return; }
  float uf, us;
  World::usualWind(src, to, &uf, &us);
  usualFrom_ = uf; usualSpeed_ = us;
  float from, sp;
  resolveWind(&from, &sp);
  world_.setLevel(level_);
  world_.setProgressive(!source_);                        // over the map pack: show the surroundings at once, fill the window as it flies
  world_.begin(src, seed_ + (uint32_t)flights_ * 7919u, to, sp, 180.0f, from);
  flownSite_ = site_; flownLevel_ = level_;
  flights_++;
  mode_ = Mode::FLYING;
  menuSel_ = 0;
  c_ = Controls();
  if (topV_) topV_->reset();
  chase_.reset(world_.glider());
  setView(view_);
  endSel_ = 0;
  if (oneButton_ && tilt_ && ctl_ == Control::TILT && !calibrated_) mode_ = Mode::CALIB;   // hold the box as you fly, then go
}

void SimApp::startAt(float x, float y, float aglStart, float headingDeg, float windFromDeg, float windSpeed, uint32_t seed,
                     float timeOfDay) {
  IHeightSource* src = source();
  if (!src) { mode_ = Mode::NOMAP; return; }
  usualFrom_ = windFromDeg; usualSpeed_ = windSpeed;
  world_.setLevel(level_);
  world_.setProgressive(!source_);
  flownSite_ = site_; flownLevel_ = level_;
  world_.beginAt(src, seed, x, y, aglStart, headingDeg, windFromDeg, windSpeed, timeOfDay);
  mode_ = Mode::FLYING;
  c_ = Controls();
  if (topV_) topV_->reset();
  chase_.reset(world_.glider());
  setView(view_);
}

void SimApp::setView(View v) {
  view_ = v;
  if (v == View::CHASE && mode_ != Mode::NOMAP) chase_.reset(world_.glider());
  if (v == View::TOP && mode_ != Mode::NOMAP) {
    const Glider& g = world_.glider();
    top().renderNow(world_.cache(), g.x, g.y, kMapHeadingUp ? g.heading : 0.0f, kMapMPerPx);
  }
}

void SimApp::oneButtonInput(ButtonEvent ev) {
  if (ev != BTN_SHORT && ev != BTN_LONG) return;
  const bool act = ev == BTN_LONG;
  switch (mode_) {
    case Mode::DAY: {
      const int rows = kDayRows + 1;                    // + Home
      if (!act) dayRow_ = (dayRow_ + 1) % rows;
      else if (dayRow_ == kDayRows) { if (nav()) nav()->openLauncher(); }
      else dayKey(KEY_ACTION);
      break;
    }
    case Mode::PAUSED:
      if (!act) menuSel_ = (menuSel_ + 1) % kMenuRows;
      else menuAct();
      break;
    case Mode::CALIB:
      if (act) { zeroTilt(); mode_ = Mode::FLYING; }
      break;
    case Mode::FLYING:
      if (!act) mode_ = Mode::PAUSED, menuSel_ = 0;
      else setView(view_ == View::TOP ? View::CHASE : View::TOP);
      break;
    case Mode::ENDED:
      if (!act) endSel_ = 1 - endSel_;
      else if (endSel_ == 0) startFlight();
      else if (nav()) nav()->openLauncher();
      break;
    case Mode::NOMAP:
      if (act && nav()) nav()->openLauncher();
      break;
  }
}

void SimApp::onInput(Key key, ButtonEvent ev) {
  if (key != KEY_MAIN) return;
  if (oneButton_) { oneButtonInput(ev); return; }
  if (ev != BTN_SHORT) return;
  if (mode_ == Mode::FLYING) mode_ = Mode::PAUSED, menuSel_ = 0;
  else if (mode_ == Mode::PAUSED) mode_ = Mode::FLYING;
}

void SimApp::setControl(Control c) {
  ctl_ = c;
  c_ = Controls();
  if (c == Control::TILT && tilt_) mode_ = Mode::CALIB;
  else if (c == Control::TILT) ctl_ = Control::BUTTONS;       // no tilt source: stay on the buttons
  boxRoll_ = 0;
}

void SimApp::zeroTilt() {
  float r, p;
  if (tilt_ && tilt_->read(&r, &p)) { zeroRoll_ = r; zeroPitch_ = p; }
  calibrated_ = true;
}

void SimApp::menuAct() {
  switch (menuSel_) {
    case 0: mode_ = Mode::FLYING; break;
    case 1: setView(view_ == View::TOP ? View::CHASE : View::TOP); break;
    case 2: assist_ = !assist_; break;
    case 3: style_ = (SkyFx::Style)(((int)style_ + 1) % SkyFx::kStyles); break;
    case 4: setSite(site_ + 1); break;
    case 5: if (oneButton_) { setControl(Control::TILT); break; }        // no buttons to fall back on: re-calibrate
            setControl(ctl_ == Control::TILT ? Control::BUTTONS : Control::TILT); break;
    case 6: if (ctl_ == Control::TILT) mode_ = Mode::CALIB; break;
    case 7: cycleTimeScale(); break;
    case 8: level_ = (level_ + 1) % Air::kLevels; break;                         // applies at the next flight
    case 9: dirIdx_ = (dirIdx_ + 1) % 9; applyWind(); break;
    case 10: spdIdx_ = (spdIdx_ + 1) % (kWindSpeeds + 1); applyWind(); break;
    case 11: startFlight(); break;
    default: if (nav()) nav()->openLauncher(); break;
  }
}

// The Day screen: Site, Thermals, Wind from, Wind speed, Start. UP / DOWN pick a row, LEFT / RIGHT (or A) change the value, A on
// Start launches, B goes home.
void SimApp::dayKey(Key key) {
  if (key == KEY_UP) dayRow_ = (dayRow_ + kDayRows - 1) % kDayRows;
  else if (key == KEY_DOWN) dayRow_ = (dayRow_ + 1) % kDayRows;
  else if (key == KEY_B) { if (nav()) nav()->openLauncher(); }
  else if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_ACTION) {
    const int d = key == KEY_LEFT ? -1 : 1;
    switch (dayRow_) {
      case 0: setSite(site_ + d); break;
      case 1: level_ = (level_ + d + Air::kLevels) % Air::kLevels; break;
      case 2: dirIdx_ = (dirIdx_ + d + 9) % 9; break;
      case 3: spdIdx_ = (spdIdx_ + d + kWindSpeeds + 1) % (kWindSpeeds + 1); break;
      default: if (key == KEY_ACTION) startFlight(); break;
    }
  }
}

void SimApp::onKey(Key key, bool down, uint32_t nowMs) {
  (void)nowMs;
  switch (key) {
    case KEY_LEFT: if (mode_ != Mode::DAY) left_ = down; break;
    case KEY_RIGHT: if (mode_ != Mode::DAY) right_ = down; break;
    case KEY_UP: if (mode_ != Mode::DAY) up_ = down; break;
    case KEY_DOWN: if (mode_ != Mode::DAY) down_ = down; break;
    default: break;
  }
  if (!down) return;
  if (mode_ == Mode::DAY) { dayKey(key); return; }
  if (mode_ == Mode::PAUSED) {
    if (key == KEY_UP) menuSel_ = (menuSel_ + kMenuRows - 1) % kMenuRows;
    else if (key == KEY_DOWN) menuSel_ = (menuSel_ + 1) % kMenuRows;
    else if (key == KEY_ACTION) menuAct();
    else if (key == KEY_B) mode_ = Mode::FLYING;
    return;
  }
  if (mode_ == Mode::CALIB) {
    if (key == KEY_ACTION) { zeroTilt(); mode_ = Mode::FLYING; }
    else if (key == KEY_B) { ctl_ = Control::BUTTONS; mode_ = Mode::FLYING; }
    return;
  }
  if (mode_ == Mode::FLYING) {
    if (key == KEY_ACTION) setView(view_ == View::TOP ? View::CHASE : View::TOP);
    else if (key == KEY_B) mode_ = Mode::PAUSED, menuSel_ = 0;
  } else if (mode_ == Mode::ENDED) {
    if (key == KEY_ACTION) startFlight();
    else if (key == KEY_B && nav()) nav()->openLauncher();
  } else if (mode_ == Mode::NOMAP) {
    if (key == KEY_B && nav()) nav()->openLauncher();
  }
}

void SimApp::applyKeys(float dt) {
  float r, p;
  if (ctl_ == Control::TILT && tilt_ && tilt_->read(&r, &p)) {
    rawRoll_ = r; rawPitch_ = p;
    boxRoll_ = r - zeroRoll_;
    c_.bank = TiltMap::bank(r, zeroRoll_);
    TiltMap::pitch(p, zeroPitch_, &c_.bar, &c_.brake);
    return;
  }
  const float tb = (right_ ? 1.0f : 0.0f) - (left_ ? 1.0f : 0.0f);
  c_.bank = approach(c_.bank, tb, 3.0f, 5.0f, dt);
  c_.bar = approach(c_.bar, up_ ? 1.0f : 0.0f, 1.5f, 3.0f, dt);
  c_.brake = approach(c_.brake, down_ ? 1.0f : 0.0f, 1.5f, 3.0f, dt);
}

void SimApp::step(uint32_t simMs, uint32_t dtMs) {
  (void)simMs;
  if (mode_ != Mode::FLYING) return;
  const float dt = dtMs * 0.001f;
  applyKeys(dt);                                      // the pilot's hands: real time
  // The world: scale_ substeps of one frame's dt each (never one big step), so x5 is 5 x the x1 steps and stable.
  // The height cache loads its cells (a frame's worth: kCacheBudget per substep) in the first substep, where it stops after the
  // first tile decode of the map pack: a frame never pays for several.
  const uint32_t tw = t();
  for (int i = 0; i < scale_ && world_.status() == Status::FLYING; i++) world_.step(c_, dt, i == 0 ? World::kCacheBudget * scale_ : 0);
  lap(P_WORLD, tw);
  if (world_.status() != Status::FLYING) {
    mode_ = Mode::ENDED;
    c_ = Controls();
  }
  if (view_ == View::CHASE) {
    const uint32_t tc = t();
    const float nan = NAN;
    chase_.update(world_.glider(), ctl_ == Control::TILT ? boxRoll_ * kDeg2Rad : nan, dt * scale_);
    lap(P_CHASEUPD, tc);
  }
  if (view_ == View::TOP) {
    const uint32_t tu = t();
    const Glider& g = world_.glider();
    top().update(world_.cache(), g.x, g.y, kMapHeadingUp ? g.heading : 0.0f, kMapMPerPx, kRenderBudgetPx);
    lap(P_TOPUPD, tu);
  }
}

HudData SimApp::hud() const {
  const Glider& g = world_.glider();
  HudData d;
  d.alt = g.z;
  d.vario = world_.vario();
  d.agl = world_.agl();
  d.speedKmh = g.airspeedKmh();
  float wx, wy;
  world_.windAt(g.z, &wx, &wy);
  d.windKmh = sqrtf(wx * wx + wy * wy) * 3.6f;
  d.windRel = wrapPi(atan2f(wx, wy) - g.heading);
  d.bankDeg = g.bank * kRad2Deg;
  d.stalled = g.stalled;
  d.assist = assist_;
  d.flightS = world_.flightTime();
  d.fromStartM = world_.distanceFromStart();
  d.timeScale = scale_;
  return d;
}

namespace {
// Local point (right, ahead) of the glider symbol -> screen, symbol turned by a (rad, clockwise).
struct Sym {
  float cx, cy, c, s;
  void pt(float lx, float ly, int* x, int* y) const {
    *x = (int)lroundf(cx + lx * c + ly * s);
    *y = (int)lroundf(cy + lx * s - ly * c);
  }
};
void gliderSymbol(const Bitmap1& bm, float cx, float cy, float a, DrawMode m, int grow) {
  const float k = 1.5f;                                   // symbol scale
  Sym sy{cx, cy, cosf(a) * k, sinf(a) * k};
  static const float wing[5][2] = {{-15, -5}, {-8, 1}, {0, 3}, {8, 1}, {15, -5}};
  int px[5], py[5];
  for (int i = 0; i < 5; i++) sy.pt(wing[i][0], wing[i][1], &px[i], &py[i]);
  int hx, hy;
  sy.pt(0, -8, &hx, &hy);
  for (int i = 0; i + 1 < 5; i++) lineGrown(bm, px[i], py[i], px[i + 1], py[i + 1], grow, m);   // (grow 2: 150 line() calls before, 2.9 ms)
  lineGrown(bm, px[0], py[0], hx, hy, grow, m);
  lineGrown(bm, px[4], py[4], hx, hy, grow, m);
  fillDisc(bm, hx, hy, 2 + grow, m);
}
}  // namespace

void SimApp::drawFlight(Bitmap1& bm) {
  const Glider& g = world_.glider();
  if (view_ == View::CHASE) {
    // Chase camera behind and above the glider; with tilt steering the horizon counter-rotates by the box roll (see ChaseCam).
    const Camera cam = chase_.camera(g, world_.cache());
    uint32_t t0 = t();
    voxel().render(bm, world_.cache(), cam);
    lap(P_VOXEL, t0);
    t0 = t();
    sky().draw(bm, voxel(), world_.air(), g, cam, world_.time(), assist_, style_);     // clouds always; thermals with the assist
    lap(P_SKY, t0);
    t0 = t();
    chase_.drawGlider(bm, g, world_.cache(), cam);
    lap(P_SPRITE, t0);
  } else {
    const uint32_t tb = t();
    top().blit(bm, g.x, g.y);
    lap(P_BLIT, tb);
    const ClipBox clip(0, TopDownView::kTopY, 239, 399);
    float sx, sy, px, py;
    top().worldToScreen(g.x, g.y, &sx, &sy);
    // Thermals the pilot cannot see: only with the assist on, as soft dithered rings.
    uint32_t ta = t();
    if (assist_) {
      static ThermalInfo th[Air::kMaxInfo];                 // static: the loop task has 4 KB of stack
      const uint32_t tn = t();
      const int n = world_.air().nearby(g.x, g.y, g.z, world_.time(), 4500.0f, th, Air::kMaxInfo);
      lap(P_NEARBY, tn);
      for (int i = 0; i < n; i++) {
        if (th[i].strength < 1.2f) continue;                 // still building up or dying: not worth a ring
        top().worldToScreen(th[i].x, th[i].y, &px, &py);
        const int r = 6 + (int)(th[i].strength * 2.5f);
        rings_++; ringR_ += (uint32_t)r;
        if (px + r < 0 || px - r > 239 || py + r < (float)TopDownView::kTopY || py - r > 399) continue;   // off the map: nothing to draw
        const uint32_t tr = t();
        if (th[i].strength >= 2.5f) fillRing(bm, (int)lroundf(px), (int)lroundf(py), r, r - 3, DRAW_CLEAR, clip);   // strong: a black ring
        else fillRing(bm, (int)lroundf(px), (int)lroundf(py), r, r - 3, Gray(7), clip);
        lap(P_RINGS, tr);
      }
    }
    lap(P_ASSIST, ta);
    // Trail.
    ta = t();
    float lx = 0, ly = 0;
    bool have = false;
    for (int i = 0; i < world_.trailCount(); i++) {
      const World::TrailPt p = world_.trailAt(i);
      top().worldToScreen(p.x, p.y, &px, &py);
      if (have) line(bm, (int)lroundf(lx), (int)lroundf(ly), (int)lroundf(px), (int)lroundf(py), DRAW_CLEAR);
      lx = px; ly = py; have = true;
    }
    if (have) line(bm, (int)lroundf(lx), (int)lroundf(ly), (int)lroundf(sx), (int)lroundf(sy), DRAW_CLEAR);
    lap(P_TRAIL, ta);
    ta = t();
    const float a = wrapPi(g.heading - top().mapHeading());
    gliderSymbol(bm, sx, sy, a, DRAW_SET, 2);
    gliderSymbol(bm, sx, sy, a, DRAW_CLEAR, 0);
    lap(P_SYMBOL, ta);
  }
  const uint32_t th = t();
  hud_.draw(bm, hud(), frame_);
  lap(P_HUD, th);
}

int SimApp::gameInfo(char* dst, size_t cap) {
  const uint32_t dec = terrain_ ? terrain_->tileDecodes() : 0;
  static const char* const modes[] = {"nomap", "flying", "paused", "ended", "calib", "day"};
  int n = snprintf(dst, cap, "view=%s mode=%s scale=%d cache_decodes=%lu", view_ == View::TOP ? "map" : "3d", modes[(int)mode_], scale_,
                   (unsigned long)(dec - lastDecodes_));
  lastDecodes_ = dec;
  if (clk_ && n > 0 && (size_t)n < cap) {
    const HeightCache::RefreshStats rs = world_.cache().takeRefreshStats();
    struct P { const char* name; Acc a; } ps[] = {{"world", prof_[P_WORLD]}, {"topupd", prof_[P_TOPUPD]}, {"chaseupd", prof_[P_CHASEUPD]},
                                                  {"blit", prof_[P_BLIT]}, {"voxel", prof_[P_VOXEL]}, {"sky", prof_[P_SKY]}, {"sprite", prof_[P_SPRITE]},
                                                  {"hud", prof_[P_HUD]},
                                                  {"assist", prof_[P_ASSIST]}, {"trail", prof_[P_TRAIL]}, {"symbol", prof_[P_SYMBOL]}, {"nearby", prof_[P_NEARBY]}, {"ring", prof_[P_RINGS]}};
    n += snprintf(dst + n, cap - n, " cache_refresh_avg/max=%.1f/%.1f cells=%lu", rs.calls ? rs.sumUs / 1000.0 / rs.calls : 0.0,
                  rs.maxUs / 1000.0, (unsigned long)rs.cells);
    const World::StepProf wp = world_.takeProf();
    if (wp.n && n > 0 && (size_t)n < cap)
      n += snprintf(dst + n, cap - n, " sub_air_avg/max=%.2f/%.2f sub_glider_avg=%.2f", wp.airUs / 1000.0 / wp.n, wp.airMax / 1000.0, wp.gliderUs / 1000.0 / wp.n);
    for (const P& p : ps)
      if (p.a.n && n > 0 && (size_t)n < cap) n += snprintf(dst + n, cap - n, " %s_avg/max=%.1f/%.1f", p.name, p.a.sum / 1000.0 / p.a.n, p.a.max / 1000.0);
    n += snprintf(dst + n, cap - n, " rings=%lu ringR=%lu", (unsigned long)rings_, (unsigned long)ringR_);
    rings_ = ringR_ = 0;
    for (Acc& a : prof_) a = Acc();
  }
  return n < 0 ? 0 : (n >= (int)cap ? (int)cap - 1 : n);
}

void SimApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t nowMs) {
  (void)nowMs;
  frame_++;
  if (mode_ == Mode::NOMAP) {
    hud_.note(bm, "needs the map pack", "B: back to the home screen");
    return;
  }
  if (mode_ == Mode::DAY) {
    char r0[28], r1[28], r2[28], r3[28];
    snprintf(r0, sizeof r0, "Site: %s", kTakeoffs[site_].name);
    snprintf(r1, sizeof r1, "Thermals: %s", Air::levelName(level_));
    snprintf(r2, sizeof r2, "Wind from: %s", dirName(dirIdx_));
    if (spdIdx_ == 0) snprintf(r3, sizeof r3, "Wind: usual");
    else snprintf(r3, sizeof r3, "Wind: %.0f km/h", (double)kWindKmh[spdIdx_ - 1]);
    const char* rows[kDayRows + 1] = {r0, r1, r2, r3, "Start", "Home"};
    hud_.menu(bm, "TODAY", rows, oneButton_ ? kDayRows + 1 : kDayRows, dayRow_);
    return;
  }
  drawFlight(bm);
  if (mode_ == Mode::PAUSED) {
    snprintf(gRows[0], sizeof gRows[0], "Resume");
    snprintf(gRows[1], sizeof gRows[1], "View: %s", view_ == View::TOP ? "map" : "3D");
    snprintf(gRows[2], sizeof gRows[2], "Assist: %s", assist_ ? "on" : "off");
    snprintf(gRows[3], sizeof gRows[3], "Thermals view: %s", SkyFx::styleName(style_));
    snprintf(gRows[4], sizeof gRows[4], "Site: %s%s", kTakeoffs[site_].name, site_ != flownSite_ ? "*" : "");
    snprintf(gRows[5], sizeof gRows[5], "Controls: %s", ctl_ == Control::TILT ? "tilt" : "buttons");
    snprintf(gRows[6], sizeof gRows[6], "Zero tilt");
    snprintf(gRows[7], sizeof gRows[7], "Speed: x%d", scale_);
    snprintf(gRows[8], sizeof gRows[8], "Thermals: %s%s", Air::levelName(level_), level_ != flownLevel_ ? "*" : "");
    snprintf(gRows[9], sizeof gRows[9], "Wind from: %s", dirName(dirIdx_));
    if (spdIdx_ == 0) snprintf(gRows[10], sizeof gRows[10], "Wind: usual");
    else snprintf(gRows[10], sizeof gRows[10], "Wind: %.0f km/h", (double)kWindKmh[spdIdx_ - 1]);
    snprintf(gRows[11], sizeof gRows[11], "New flight");
    snprintf(gRows[12], sizeof gRows[12], "Home");
    const char* rows[kMenuRows];
    for (int i = 0; i < kMenuRows; i++) rows[i] = gRows[i];
    hud_.menu(bm, "PAUSED", rows, kMenuRows, menuSel_);
  } else if (mode_ == Mode::CALIB) {
    float r = 0, p = 0;
    if (tilt_) tilt_->read(&r, &p);
    char l1[28], l2[28];
    snprintf(l1, sizeof l1, "roll %+.0f  pitch %+.0f", (double)r, (double)p);
    snprintf(l2, sizeof l2, "hold the box as you fly");
    hud_.banner(bm, "ZERO TILT", l2, l1, oneButton_ ? "long press: set" : "A: set   B: buttons");
  } else if (mode_ == Mode::ENDED) {
    char l1[28], l2[28];
    snprintf(l1, sizeof l1, "flew %.1f km", (double)(world_.distanceFlown() / 1000.0f));
    snprintf(l2, sizeof l2, "max %.0f m", (double)world_.maxAltitude());
    const char* t = world_.status() == Status::LANDED ? "LANDED" : "CRASHED";
    hud_.banner(bm, t, l1, l2, !oneButton_ ? "A: new flight   B: home" : (endSel_ == 0 ? "[New flight]  Home" : "New flight  [Home]"));
  }
}

}  // namespace sim
