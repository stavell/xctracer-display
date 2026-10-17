#include "apps/games/sim/sim_app.h"
#include <stdio.h>
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

namespace sim {

namespace {
constexpr float kMapMPerPx = 40.0f;
constexpr int kRenderBudgetPx = 4500;          // top-down picture pixels per frame (about 20 frames per picture)

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
  if (dayScreen_ && source()) { mode_ = Mode::DAY; dayRow_ = 0; return; }
  startFlight();
}

IHeightSource* SimApp::source() {
  if (source_) return source_;
  if (!terrain_ || !terrain_->isOpen()) return nullptr;
  tsrc_.set(terrain_, kTakeoffs[site_].lat, kTakeoffs[site_].lon);
  return &tsrc_;
}

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
  world_.begin(src, seed_ + (uint32_t)flights_ * 7919u, to, sp, 180.0f, from);
  flownSite_ = site_; flownLevel_ = level_;
  flights_++;
  mode_ = Mode::FLYING;
  menuSel_ = 0;
  c_ = Controls();
  top_.reset();
  chase_.reset(world_.glider());
  setView(view_);
}

void SimApp::startAt(float x, float y, float aglStart, float headingDeg, float windFromDeg, float windSpeed, uint32_t seed,
                     float timeOfDay) {
  IHeightSource* src = source();
  if (!src) { mode_ = Mode::NOMAP; return; }
  usualFrom_ = windFromDeg; usualSpeed_ = windSpeed;
  world_.setLevel(level_);
  flownSite_ = site_; flownLevel_ = level_;
  world_.beginAt(src, seed, x, y, aglStart, headingDeg, windFromDeg, windSpeed, timeOfDay);
  mode_ = Mode::FLYING;
  c_ = Controls();
  top_.reset();
  chase_.reset(world_.glider());
  setView(view_);
}

void SimApp::setView(View v) {
  view_ = v;
  if (v == View::CHASE && mode_ != Mode::NOMAP) chase_.reset(world_.glider());
  if (v == View::TOP && mode_ != Mode::NOMAP) {
    const Glider& g = world_.glider();
    top_.renderNow(world_.cache(), g.x, g.y, g.heading, kMapMPerPx);
  }
}

void SimApp::onInput(Key key, ButtonEvent ev) {
  if (key != KEY_MAIN || ev != BTN_SHORT) return;
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
}

void SimApp::menuAct() {
  switch (menuSel_) {
    case 0: mode_ = Mode::FLYING; break;
    case 1: setView(view_ == View::TOP ? View::CHASE : View::TOP); break;
    case 2: assist_ = !assist_; break;
    case 3: setSite(site_ + 1); break;
    case 4: setControl(ctl_ == Control::TILT ? Control::BUTTONS : Control::TILT); break;
    case 5: if (ctl_ == Control::TILT) mode_ = Mode::CALIB; break;
    case 6: cycleTimeScale(); break;
    case 7: level_ = (level_ + 1) % Air::kLevels; break;                         // applies at the next flight
    case 8: dirIdx_ = (dirIdx_ + 1) % 9; applyWind(); break;
    case 9: spdIdx_ = (spdIdx_ + 1) % (kWindSpeeds + 1); applyWind(); break;
    case 10: startFlight(); break;
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
  for (int i = 0; i < scale_ && world_.status() == Status::FLYING; i++) world_.step(c_, dt);
  if (world_.status() != Status::FLYING) {
    mode_ = Mode::ENDED;
    c_ = Controls();
  }
  if (view_ == View::CHASE) {
    const float nan = NAN;
    chase_.update(world_.glider(), ctl_ == Control::TILT ? boxRoll_ * kDeg2Rad : nan, dt * scale_);
  }
  if (view_ == View::TOP) {
    const Glider& g = world_.glider();
    top_.update(world_.cache(), g.x, g.y, g.heading, kMapMPerPx, kRenderBudgetPx);
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
  for (int oy = -grow; oy <= grow; oy++)
    for (int ox = -grow; ox <= grow; ox++) {
      for (int i = 0; i + 1 < 5; i++) line(bm, px[i] + ox, py[i] + oy, px[i + 1] + ox, py[i + 1] + oy, m);
      line(bm, px[0] + ox, py[0] + oy, hx + ox, hy + oy, m);
      line(bm, px[4] + ox, py[4] + oy, hx + ox, hy + oy, m);
    }
  fillDisc(bm, hx, hy, 2 + grow, m);
}
}  // namespace

void SimApp::drawFlight(Bitmap1& bm) {
  const Glider& g = world_.glider();
  if (view_ == View::CHASE) {
    // Chase camera behind and above the glider; with tilt steering the horizon counter-rotates by the box roll (see ChaseCam).
    const Camera cam = chase_.camera(g, world_.cache());
    voxel_.render(bm, world_.cache(), cam);
    chase_.drawGlider(bm, g, world_.cache(), cam);
  } else {
    top_.blit(bm, g.x, g.y);
    const ClipBox clip(0, TopDownView::kTopY, 239, 399);
    float sx, sy, px, py;
    top_.worldToScreen(g.x, g.y, &sx, &sy);
    // Thermals the pilot cannot see: only with the assist on, as soft dithered rings.
    if (assist_) {
      static ThermalInfo th[Air::kMaxInfo];                 // static: the loop task has 4 KB of stack
      const int n = world_.air().nearby(g.x, g.y, g.z, world_.time(), 4500.0f, th, Air::kMaxInfo);
      for (int i = 0; i < n; i++) {
        if (th[i].strength < 1.2f) continue;                 // still building up or dying: not worth a ring
        top_.worldToScreen(th[i].x, th[i].y, &px, &py);
        const int r = 6 + (int)(th[i].strength * 2.5f);
        if (th[i].strength >= 2.5f) fillRing(bm, (int)lroundf(px), (int)lroundf(py), r, r - 3, DRAW_CLEAR, clip);   // strong: a black ring
        else fillRing(bm, (int)lroundf(px), (int)lroundf(py), r, r - 3, Gray(7), clip);
      }
    }
    // Trail.
    float lx = 0, ly = 0;
    bool have = false;
    for (int i = 0; i < world_.trailCount(); i++) {
      const World::TrailPt p = world_.trailAt(i);
      top_.worldToScreen(p.x, p.y, &px, &py);
      if (have) line(bm, (int)lroundf(lx), (int)lroundf(ly), (int)lroundf(px), (int)lroundf(py), DRAW_CLEAR);
      lx = px; ly = py; have = true;
    }
    if (have) line(bm, (int)lroundf(lx), (int)lroundf(ly), (int)lroundf(sx), (int)lroundf(sy), DRAW_CLEAR);
    const float a = wrapPi(g.heading - top_.mapHeading());
    gliderSymbol(bm, sx, sy, a, DRAW_SET, 2);
    gliderSymbol(bm, sx, sy, a, DRAW_CLEAR, 0);
  }
  hud_.draw(bm, hud(), frame_);
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
    const char* rows[kDayRows] = {r0, r1, r2, r3, "Start"};
    hud_.menu(bm, "TODAY", rows, kDayRows, dayRow_);
    return;
  }
  drawFlight(bm);
  if (mode_ == Mode::PAUSED) {
    snprintf(gRows[0], sizeof gRows[0], "Resume");
    snprintf(gRows[1], sizeof gRows[1], "View: %s", view_ == View::TOP ? "map" : "3D");
    snprintf(gRows[2], sizeof gRows[2], "Assist: %s", assist_ ? "on" : "off");
    snprintf(gRows[3], sizeof gRows[3], "Site: %s%s", kTakeoffs[site_].name, site_ != flownSite_ ? "*" : "");
    snprintf(gRows[4], sizeof gRows[4], "Controls: %s", ctl_ == Control::TILT ? "tilt" : "buttons");
    snprintf(gRows[5], sizeof gRows[5], "Zero tilt");
    snprintf(gRows[6], sizeof gRows[6], "Speed: x%d", scale_);
    snprintf(gRows[7], sizeof gRows[7], "Thermals: %s%s", Air::levelName(level_), level_ != flownLevel_ ? "*" : "");
    snprintf(gRows[8], sizeof gRows[8], "Wind from: %s", dirName(dirIdx_));
    if (spdIdx_ == 0) snprintf(gRows[9], sizeof gRows[9], "Wind: usual");
    else snprintf(gRows[9], sizeof gRows[9], "Wind: %.0f km/h", (double)kWindKmh[spdIdx_ - 1]);
    snprintf(gRows[10], sizeof gRows[10], "New flight");
    snprintf(gRows[11], sizeof gRows[11], "Home");
    const char* rows[kMenuRows];
    for (int i = 0; i < kMenuRows; i++) rows[i] = gRows[i];
    hud_.menu(bm, "PAUSED", rows, kMenuRows, menuSel_);
  } else if (mode_ == Mode::CALIB) {
    float r = 0, p = 0;
    if (tilt_) tilt_->read(&r, &p);
    char l1[28], l2[28];
    snprintf(l1, sizeof l1, "roll %+.0f  pitch %+.0f", (double)r, (double)p);
    snprintf(l2, sizeof l2, "hold the box as you fly");
    hud_.banner(bm, "ZERO TILT", l2, l1, "A: set   B: buttons");
  } else if (mode_ == Mode::ENDED) {
    char l1[28], l2[28];
    snprintf(l1, sizeof l1, "flew %.1f km", (double)(world_.distanceFlown() / 1000.0f));
    snprintf(l2, sizeof l2, "max %.0f m", (double)world_.maxAltitude());
    hud_.banner(bm, world_.status() == Status::LANDED ? "LANDED" : "CRASHED", l1, l2, "A: new flight   B: home");
  }
}

}  // namespace sim
