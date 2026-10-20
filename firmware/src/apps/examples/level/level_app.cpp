#include "apps/examples/level/level_app.h"
#include <math.h>
#include <stdio.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

namespace examples {

void LevelApp::begin(uint32_t nowMs) {
  hold_ = have_ = false;
  battery_.update(nowMs);
  battPct_ = battery_.percent();
  battAt_ = nowMs;
  motion_.start();
}

void LevelApp::tick(uint32_t nowMs) {                  // every host iteration (~10 ms): read the sensor, nothing else
  motion_.poll(nowMs);
  if (nowMs - battAt_ >= 5000) { battAt_ = nowMs; battery_.update(nowMs); battPct_ = battery_.percent(); }
}

void LevelApp::step(uint32_t, uint32_t) {              // one fixed 20 ms step: the physics of the bubble
  float r, p;
  if (hold_ || !motion_.tilt(&r, &p)) return;
  if (!have_) { roll_ = r; pitch_ = p; have_ = true; }
  else { roll_ += kAlpha * (r - roll_); pitch_ += kAlpha * (p - pitch_); }
}

static void readout(const Bitmap1& bm, const Font1& f, int cx, int y, const char* label, float deg, bool have) {
  char b[24];
  if (have && isfinite(deg)) snprintf(b, sizeof b, "%s %+.1f", label, (double)deg); else snprintf(b, sizeof b, "%s --", label);
  ui::centered(bm, f, cx, y, b, DRAW_CLEAR);
}

void LevelApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t) {
  fonts_.ensure(bm.rotation);
  ui::titleBar(bm, fonts_, "LEVEL", battPct_, hold_ ? "HOLD" : nullptr, true);
  const int cx = ui::W / 2, cy = 170, R = 96, rb = 18;                    // vial centre and radius, bubble radius
  fillDisc(bm, cx, cy, R, Gray(13));                                      // the liquid: light grey dither (0 = black, 16 = white)
  circle(bm, cx, cy, R, DRAW_CLEAR);
  circle(bm, cx, cy, R - 1, DRAW_CLEAR);
  circle(bm, cx, cy, 2 * rb + 4, DRAW_CLEAR);
  circle(bm, cx, cy, 2 * rb + 3, DRAW_CLEAR);                             // the target ring: level when the bubble sits in it
  bm.hline(cx - R, cx + R, cy, DRAW_CLEAR);
  bm.vline(cx, cy - R, cy + R, DRAW_CLEAR);
  // The bubble floats to the HIGH side: right edge down (roll > 0) moves it left, top edge up (pitch > 0) moves it up the screen.
  float bx = have_ ? -roll_ : 0, by = have_ ? -pitch_ : 0;
  const float len = sqrtf(bx * bx + by * by), lim = kFullScaleDeg;
  if (len > lim) { bx *= lim / len; by *= lim / len; }
  const int px = cx + (int)(bx / lim * (R - rb - 3)), py = cy + (int)(by / lim * (R - rb - 3));
  fillDisc(bm, px, py, rb, DRAW_SET);
  circle(bm, px, py, rb, DRAW_CLEAR);
  circle(bm, px, py, rb - 1, DRAW_CLEAR);
  readout(bm, fonts_.mid, cx, 310, "ROLL", roll_, have_);
  readout(bm, fonts_.mid, cx, 345, "PITCH", pitch_, have_);
  const bool six = nav() && nav()->sixButtons();                          // the hint names the control used last
  ui::hintLine(bm, fonts_, hold_ ? (six ? "A: release" : "press: release") : (six ? "A: hold reading" : "press: hold reading"));
}

}  // namespace examples
