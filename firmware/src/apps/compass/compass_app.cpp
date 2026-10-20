#include "apps/compass/compass_app.h"
#include <math.h>
#include <string.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"
#include "platform/gfx/widgets/compass_dial.h"
#include "platform/power/clock_view.h"
#include "platform/util/numfmt.h"

namespace compass {

static float angDiffDeg(float a, float b) {      // absolute shortest angle between two headings
  float d = fabsf(fmodf(a - b, 360.0f));
  return d > 180.0f ? 360.0f - d : d;
}

void CompassApp::begin(uint32_t nowMs) {
  flat_ = false;                                     // always opens on the dome
  view_.reset();
  smooth_.reset();
  smoothTuned_ = false;
  drawnHeading_ = NAN;
  haveAmbient_ = false;
  redraw_ = true;
  nowMs_ = nowMs;
  battery_.update(nowMs);
  battPct_ = battery_.percent();
  motion_.start();
}

void CompassApp::end() {
  motion_.stop();                                    // sensors off
}

void CompassApp::tick(uint32_t now) {
  nowMs_ = now;
  battery_.update(now);
  battPct_ = battery_.percent();
  float tc, rh;
  if (thermo_.read(tc, rh)) { ambientC_ = tc; ambientRh_ = rh; ambientMs_ = now; haveAmbient_ = true; }
  motion_.poll(now);
  const bool fused = motion_.fused();
  if (!smoothTuned_ || smoothFused_ != fused) {      // the fused heading is smooth: no deadband and no snapping; the compass alone is noisy
    smoothTuned_ = true;
    smoothFused_ = fused;
    smooth_.setTau(fused ? kFusedTauMs : kMagTauMs);
    smooth_.setDeadband(fused ? 0.0f : kDeadbandDeg, kDeadbandHoldMs);
    smooth_.setSettle(fused ? 0.01f : HeadingSmoother::SETTLE_DEG);
  }
  smooth_.update(motion_.valid() ? motion_.heading() : NAN, now);   // once per tick: draw only reads it
}

void CompassApp::toggle() {
  flat_ = !flat_;
  redraw_ = true;
}

void CompassApp::onInput(Key key, ButtonEvent ev) {
  if (key == KEY_MAIN && ev == BTN_SHORT) toggle();
}

// The six buttons: down / right / A swap the pages like a short press of the switch.
void CompassApp::onKey(Key key, bool down, uint32_t) {
  if (down && (key == KEY_DOWN || key == KEY_RIGHT || key == KEY_ACTION)) toggle();
}

bool CompassApp::wantsFrame(uint32_t now) {
  if (!flat_) return false;                          // the dome is driven by the host's frame clock
  if (redraw_) return true;
  uint32_t every = kFlatEveryMs;
  bool moving;
  if (smoothFused_) {
    const float v = smooth_.value();
    moving = isfinite(v) && (!isfinite(drawnHeading_) || angDiffDeg(v, drawnHeading_) > kHalfPixelDeg);
  } else {
    moving = !smooth_.settled();
  }
  if (moving) every = kFlatAnimMs;
  return now - lastDraw_ >= every;
}

void CompassApp::step(uint32_t simMs, uint32_t dtMs) {
  compass3d::Input in;
  in.valid = motion_.fused() && !motion_.gyroFailed();
  if (in.valid) {
    motion_.quaternion(in.q);
    in.headingDeg = motion_.heading();
    in.trust = motion_.trust();
  }
  in.calibrated = motion_.calibrated();
  in.tempC = ambientC(nowMs_);
  in.rhPct = ambientRh(nowMs_);
  if (weather_) {
    in.pressure10 = weather_->latest();
    in.trendOk = weather_->tendency(in.trendCls);
  }
  view_.step(in, dtMs, simMs);
}

void CompassApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t now) {
  if (!flat_) { view_.draw(bm, now); return; }
  lastDraw_ = now;
  redraw_ = false;
  drawFlat(bm, now);
}

// A labelled frame of the weather strip: the label inside the top edge, the value is drawn by the caller.
void CompassApp::cell(const Bitmap1& bm, int x, int y, int w, const char* label) {
  ui::roundFrame(bm, x, y, w, 62, 4, 2, DRAW_CLEAR);
  ui::centered(bm, fonts_.small, x + w / 2, y + 19, label, DRAW_CLEAR);
}

static const char* num(char* b, size_t n, float v, int decimals) {
  if (!isfinite(v) || !fmtFixed(b, n, v, decimals, false)) snprintf(b, n, "--");
  return b;
}

void CompassApp::drawFlat(Bitmap1& bm, uint32_t now) {
  fonts_.ensure(bm.rotation);
  if (!big_.isBuilt(&FreeSansBold18pt7b, bm.rotation)) big_.build(&FreeSansBold18pt7b, bm.rotation);
  ui::titleBar(bm, fonts_, "COMPASS", battPct_);
  const int cx = ui::W / 2, cy = 152, R = 104;
  fillRing(bm, cx, cy, R - 1, R - 12, Gray(13));      // the light ring under the ticks, before any drawing over it
  const float hdg = smooth_.value();
  drawnHeading_ = hdg;
  widgets::BitmapDialPen pen(bm, fonts_, big_);
  widgets::drawCompassDial(pen, cx, cy, R, hdg, motion_.valid() ? widgets::DIAL_SRC_MAG : widgets::DIAL_SRC_NONE);

  // Weather strip: temperature, humidity, pressure with its 3 h tendency.
  const int sy = 296, cw = 78;
  char b[16];
  cell(bm, 3, sy, cw, "TEMP C");
  num(b, sizeof b, ambientC(now), 1);
  ui::centered(bm, fonts_.mid, 42, sy + 52, b, DRAW_CLEAR);
  cell(bm, 81, sy, cw, "RH %");
  num(b, sizeof b, ambientRh(now), 0);
  ui::centered(bm, fonts_.mid, 120, sy + 52, b, DRAW_CLEAR);
  cell(bm, 159, sy, cw, "hPa");
  const int p10 = weather_ ? weather_->latest() : 0;
  int cls = 0;
  const bool arrow = p10 > 0 && weather_ && weather_->tendency(cls);
  num(b, sizeof b, p10 > 0 ? p10 / 10.0f : NAN, 0);
  const Font1& pf = fonts_.mid;
  const int tw = textWidth(b, pf), total = tw + (arrow ? 18 : 0), x0 = 198 - total / 2 - (arrow ? 3 : 0);
  drawText(bm, x0, sy + 52, b, pf, DRAW_CLEAR);
  if (arrow) clockview::trend(bm, x0 + tw + 10, sy + 43, cls);
  ui::hintLine(bm, fonts_, "press: 3D compass");
}

}  // namespace compass
