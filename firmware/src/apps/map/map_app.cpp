#include "apps/map/map_app.h"
#include <math.h>
#include <stdio.h>
#include "platform/gfx/shapes.h"
#include "platform/gfx/strokes.h"

namespace mapapp {

namespace {

const float kScaleM[MapApp::kScales] = {25, 50, 100, 200, 400};

float angDiff(float a, float b) {
  float d = fabsf(fmodf(a - b, 360.0f));
  return d > 180.0f ? 360.0f - d : d;
}

bool isPad(Key k) { return k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT; }

void padDir(Key k, float* dx, float* dy) {
  *dx = k == KEY_LEFT ? -1.0f : k == KEY_RIGHT ? 1.0f : 0.0f;
  *dy = k == KEY_UP ? -1.0f : k == KEY_DOWN ? 1.0f : 0.0f;
}

}  // namespace

float MapApp::scaleAt(int i) { return kScaleM[i < 0 ? 0 : i >= kScales ? kScales - 1 : i]; }

void MapApp::begin(uint32_t now) {
  open_ = pack_ && terr_.open(pack_) == terrain::Terrain::OK;
  if (open_) {
    vec_.open(pack_, terr_.info());                     // towns and roads are optional
    view_.attach(&terr_, &vec_);
    view_.setArea(kW, kH);
    view_.setScale(scaleAt(scale_));
    view_.setHeading(0);
    view_.centreOn(startLat_, startLon_);
    view_.snap();
  }
  if (fix_) fix_->start();
  follow_ = true;
  haveFix_ = false;
  hintUntil_ = now + kHintMs;
  dirty_ = true;
  lastTickMs_ = now;
}

void MapApp::end() {
  if (fix_) fix_->stop();
  motion_.stop();
}

void MapApp::toast(const char* s, uint32_t now) {
  toast_ = s;
  toastUntil_ = now + kToastMs;
  hintUntil_ = 0;
  dirty_ = true;
}

void MapApp::zoom(int dir, bool wrap, uint32_t now) {
  int s = scale_ + dir;
  if (s < 0) s = wrap ? kScales - 1 : 0;
  if (s >= kScales) s = wrap ? 0 : kScales - 1;
  if (s == scale_) return;
  scale_ = s;
  view_.setScale(scaleAt(s));
  if (!compass_) view_.snap();
  static char t[16];
  const float m = scaleAt(s);
  snprintf(t, sizeof t, "%d m / px", (int)m);
  toast(t, now);
}

void MapApp::toggleCompass(uint32_t now) {
  compass_ = !compass_;
  if (compass_) {
    motion_.start();
    smooth_.reset();
    smoothTuned_ = false;
  } else {
    motion_.stop();
    view_.setHeading(0);
    view_.snap();
    shownHdg_ = 0;
  }
  toast(compass_ ? "Compass up" : "North up", now);
}

void MapApp::recentre(uint32_t now) {
  if (!haveFix_) { toast("no GPS position", now); return; }
  follow_ = true;
  view_.centreOn(fix0_.lat, fix0_.lon);
  if (!compass_) view_.snap();
  toast("on the GPS", now);
}

void MapApp::onInput(Key key, ButtonEvent ev) {
  if (key != KEY_MAIN || !open_) return;
  if (ev == BTN_SHORT) zoom(-1, true, lastTickMs_);
  else if (ev == BTN_LONG) toggleCompass(lastTickMs_);
}

void MapApp::onKey(Key key, bool down, uint32_t now) {
  if (key == KEY_MAIN || key >= KEY_COUNT || !open_) return;
  if (down) {
    down_[key] = true;
    downMs_[key] = now;
    held_[key] = false;
    if (down_[KEY_ACTION] && down_[KEY_B]) held_[KEY_ACTION] = held_[KEY_B] = true;   // A + B is the sheet's combo: no zoom
    if (isPad(key)) {
      float dx, dy;
      padDir(key, &dx, &dy);
      follow_ = false;
      view_.pan(dx * kStepPx, dy * kStepPx);
      dirty_ = true;
    }
    return;
  }
  const bool fired = held_[key];
  down_[key] = false;
  held_[key] = false;
  if (fired) return;
  if (key == KEY_ACTION) zoom(-1, false, now);
  else if (key == KEY_B) zoom(1, false, now);
}

// D-pad held past kRepeatMs: a steady glide in screen directions; north up moves in whole pixels.
void MapApp::glide(uint32_t now) {
  const float dt = (now - lastTickMs_) * 0.001f;
  float vx = 0, vy = 0;
  for (int k = KEY_UP; k <= KEY_RIGHT; k++) {
    if (!down_[k] || (int32_t)(now - downMs_[k]) < (int32_t)kRepeatMs) continue;   // an edge may carry a time after this tick's
    float dx, dy;
    padDir((Key)k, &dx, &dy);
    vx += dx;
    vy += dy;
  }
  if (vx == 0 && vy == 0) { panX_ = panY_ = 0; return; }
  panX_ += vx * kGlidePxS * dt;
  panY_ += vy * kGlidePxS * dt;
  float mx = panX_, my = panY_;
  if (!compass_) { mx = truncf(panX_); my = truncf(panY_); }
  if (mx == 0 && my == 0) return;
  panX_ -= mx;
  panY_ -= my;
  view_.pan(mx, my);
  dirty_ = true;
}

void MapApp::tick(uint32_t now) {
  battery_.update(now);
  const int b = battery_.percent();
  if (b != battPct_) { battPct_ = b; dirty_ = true; }
  if (!open_) { lastTickMs_ = now; return; }
  if (fix_) {
    fix_->poll(now);
    GeoFix f;
    if (fix_->latest(&f) && (!haveFix_ || f.atMs != fix0_.atMs)) {
      fix0_ = f;
      haveFix_ = true;
      if (follow_) {
        view_.centreOn(f.lat, f.lon);
        if (!compass_) view_.snap();
      }
      dirty_ = true;
    }
  }
  if (compass_) {
    motion_.poll(now);
    const bool fused = motion_.fused();
    if (!smoothTuned_ || smoothFused_ != fused) {         // as the compass app: the fused heading is smooth, the compass alone noisy
      smoothTuned_ = true;
      smoothFused_ = fused;
      smooth_.setTau(fused ? 30.0f : 90.0f);
      smooth_.setDeadband(fused ? 0.0f : 0.4f, 300);
      smooth_.setSettle(fused ? 0.01f : HeadingSmoother::SETTLE_DEG);
    }
    const float h = smooth_.update(motion_.valid() ? motion_.heading() : NAN, now);
    if (isfinite(h) && angDiff(h, shownHdg_) >= kTurnDeg) {
      shownHdg_ = h;
      view_.setHeading(h);
      dirty_ = true;
    }
  }
  for (int k = KEY_ACTION; k <= KEY_B; k++) {
    if (!down_[k] || held_[k] || (int32_t)(now - downMs_[k]) < (int32_t)kHoldMs) continue;
    held_[k] = true;
    if (k == KEY_ACTION) recentre(now);
    else toggleCompass(now);
  }
  glide(now);
  if (view_.step(kStepUs)) tilePending_ = true;
  if (tilePending_ && now - lastFrameMs_ >= kTileFrameMs) dirty_ = true;
  if ((toastUntil_ && now >= toastUntil_) || (hintUntil_ && now >= hintUntil_)) {
    if (now >= toastUntil_) toastUntil_ = 0;
    if (now >= hintUntil_) hintUntil_ = 0;
    dirty_ = true;
  }
  lastTickMs_ = now;
}

bool MapApp::wantsFrame(uint32_t now) { return dirty_ && (lastFrameMs_ == 0 || now - lastFrameMs_ >= kFrameMs); }

void MapApp::drawNote(const Bitmap1& bm, const char* s, int y) {
  const int tw = textWidth(s, fonts_.bold), w = tw + 20, x = (kW - w) / 2;
  ui::roundFill(bm, x, y, w, 24, 6, DRAW_SET);
  ui::roundFrame(bm, x, y, w, 24, 6, 2, DRAW_CLEAR);
  drawText(bm, x + 10, y + 17, s, fonts_.bold, DRAW_CLEAR);
}

// The pilot: an arrow along the GPS course (a dot when standing), hollow when the fix is old.
void MapApp::drawPilot(const Bitmap1& bm, uint32_t now) {
  if (!haveFix_) return;
  const int cx = (int)lroundf(pilotSx_), cy = (int)lroundf(pilotSy_) + kTop;
  if (cx < -12 || cx > kW + 12 || cy < kTop - 12 || cy > 400 + 12) return;
  const bool stale = now - fix0_.atMs > kFixStaleMs;
  const bool moving = isfinite(fix0_.courseDeg) && isfinite(fix0_.speedKmh) && fix0_.speedKmh > 3;
  if (!moving) {
    fillDisc(bm, cx, cy, 8, DRAW_SET);
    fillDisc(bm, cx, cy, 6, DRAW_CLEAR);
    if (stale) fillDisc(bm, cx, cy, 3, DRAW_SET);
    return;
  }
  const float a = (fix0_.courseDeg - view_.heading()) * 0.017453292f;
  const float ux = sinf(a), uy = -cosf(a);                  // along the course on the screen
  auto tri = [&](float s, DrawMode m) {
    const int pt[3][2] = {{(int)lroundf(cx + ux * 13 * s), (int)lroundf(cy + uy * 13 * s)},
                          {(int)lroundf(cx - ux * 8 * s - uy * 8 * s), (int)lroundf(cy - uy * 8 * s + ux * 8 * s)},
                          {(int)lroundf(cx - ux * 8 * s + uy * 8 * s), (int)lroundf(cy - uy * 8 * s - ux * 8 * s)}};
    strokes::polyFill(bm, pt, 3, m);
  };
  tri(1.4f, DRAW_SET);
  tri(1.0f, DRAW_CLEAR);
  if (stale) tri(0.5f, DRAW_SET);
}

// Compass mode: where north is, black half pointing to it.
void MapApp::drawNorth(const Bitmap1& bm) {
  const int cx = kW - 22, cy = kTop + 22;
  fillDisc(bm, cx, cy, 17, DRAW_CLEAR);
  fillDisc(bm, cx, cy, 15, DRAW_SET);
  const float a = -view_.heading() * 0.017453292f;
  const float nx = sinf(a), ny = -cosf(a), px = -ny, py = nx;
  const int tip[3][2] = {{(int)lroundf(cx + nx * 13), (int)lroundf(cy + ny * 13)},
                         {(int)lroundf(cx + px * 5), (int)lroundf(cy + py * 5)},
                         {(int)lroundf(cx - px * 5), (int)lroundf(cy - py * 5)}};
  strokes::polyFill(bm, tip, 3, DRAW_CLEAR);
  const int tail[3][2] = {{(int)lroundf(cx - nx * 13), (int)lroundf(cy - ny * 13)}, {tip[1][0], tip[1][1]}, {tip[2][0], tip[2][1]}};
  for (int i = 0; i < 3; i++) line(bm, tail[i][0], tail[i][1], tail[(i + 1) % 3][0], tail[(i + 1) % 3][1], DRAW_CLEAR);
}

void MapApp::drawScale(const Bitmap1& bm) {
  static const int kLen[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
  const float m = view_.scale();
  int len = kLen[0];
  for (int l : kLen)
    if (l / m <= 90) len = l;
  const int px = (int)lroundf(len / m);
  char t[12];
  if (len >= 1000) snprintf(t, sizeof t, "%d km", len / 1000);
  else snprintf(t, sizeof t, "%d m", len);
  const int tw = textWidth(t, fonts_.bold), x = 6, y = 400 - 24, w = px + tw + 22;
  ui::roundFill(bm, x - 2, y - 2, w, 22, 5, DRAW_SET);
  bm.fillRect(x + 4, y + 10, px, 3, DRAW_CLEAR);
  bm.fillRect(x + 4, y + 5, 2, 8, DRAW_CLEAR);
  bm.fillRect(x + 4 + px - 2, y + 5, 2, 8, DRAW_CLEAR);
  drawText(bm, x + px + 12, y + 14, t, fonts_.bold, DRAW_CLEAR);
}

void MapApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t now) {
  dirty_ = false;
  tilePending_ = false;
  lastFrameMs_ = now ? now : 1;
  fonts_.ensure(bm.rotation);
  if (!open_) {
    ui::titleBar(bm, fonts_, "Map", battPct_);
    ui::centered(bm, fonts_.mid, kW / 2, 200, "No map pack", DRAW_CLEAR);
    ui::centered(bm, fonts_.small, kW / 2, 224, "load one with host/load_pack.py", DRAW_CLEAR);
    return;
  }
  float gx, gy;
  if (haveFix_ && view_.toGlobal(fix0_.lat, fix0_.lon, &gx, &gy)) view_.toScreen(gx, gy, &pilotSx_, &pilotSy_);
  mapdraw::Rect keep[3] = {{0, kH - 28, 130, kH}, {kW - 44, 0, kW, 44}, {0, 0, 0, 0}};
  int nKeep = 2;
  if (haveFix_) keep[nKeep++] = mapdraw::Rect{(int)pilotSx_ - 18, (int)pilotSy_ - 18, (int)pilotSx_ + 18, (int)pilotSy_ + 18};
  view_.draw(bm, 0, kTop, &fonts_.small, keep, nKeep);
  if (!follow_ || !haveFix_) {                             // the point the view is centred on
    const int cx = kW / 2, cy = kTop + kH / 2;
    bm.fillRect(cx - 7, cy - 1, 15, 3, DRAW_SET);
    bm.fillRect(cx - 1, cy - 7, 3, 15, DRAW_SET);
    bm.fillRect(cx - 6, cy, 13, 1, DRAW_CLEAR);
    bm.fillRect(cx, cy - 6, 1, 13, DRAW_CLEAR);
  }
  drawPilot(bm, now);
  if (compass_) drawNorth(bm);
  drawScale(bm);
  ui::titleBar(bm, fonts_, "Map", battPct_, compass_ ? "compass" : "north up");
  if (toastUntil_) drawNote(bm, toast_, kTop + 10);
  else if (hintUntil_) {
    const bool six = nav() && nav()->sixButtons();
    drawNote(bm, six ? "A/B zoom, hold B: compass" : "press: zoom, long: compass", 400 - 58);
  }
}

}  // namespace mapapp
