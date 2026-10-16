#include "apps/launcher/launcher_app.h"

void LauncherApp::begin(uint32_t nowMs) {
  count_ = apps_.count() < HomeModel::MAX_TILES ? apps_.count() : HomeModel::MAX_TILES;
  model_.count = count_;
  for (int i = 0; i < count_; i++) model_.tiles[i] = apps_.at(i);
  if (sel_ >= count_) sel_ = 0;
  model_.heldMs = 0;
  statusAt_ = nowMs;
  battery_.update(nowMs);
  dirty_ = true;
}

// Battery, clock and map pack change slowly: look twice a second, redraw only when what is shown would differ.
void LauncherApp::tick(uint32_t nowMs) {
  battery_.update(nowMs);
  if (nowMs - statusAt_ < 500) return;
  statusAt_ = nowMs;
  const int minute = clock_.localMinutes(settings_.tzMinutes(), nowMs);
  const bool map = pack_ && pack_->packOk();
  if (battery_.percent() != drawnBatt_ || minute != drawnMinute_ || map != drawnMap_) dirty_ = true;
}

void LauncherApp::move(int d) {
  if (count_ == 0) return;
  sel_ = ((sel_ + d) % count_ + count_) % count_;
  dirty_ = true;
}

void LauncherApp::activate() {
  if (sel_ < count_ && nav()) nav()->openApp(apps_.at(sel_).app);
}

void LauncherApp::onKey(Key key, bool down, uint32_t) {
  if (!down) return;
  const int c = LauncherView::COLS;
  if (key == KEY_LEFT) move(-1);
  else if (key == KEY_RIGHT) move(1);
  else if (key == KEY_UP) { if (sel_ - c >= 0) move(-c); }
  else if (key == KEY_DOWN) { if (sel_ + c < count_) move(c); }
  else if (key == KEY_ACTION) activate();
}

void LauncherApp::onInput(Key key, ButtonEvent ev) {
  if (key != KEY_MAIN) return;
  if (ev == BTN_SHORT) move(1);
  else if (ev == BTN_LONG) activate();
  else if (ev == BTN_HOLD) power_.shutdown();
}

void LauncherApp::onHeld(uint32_t heldMs) {
  const bool warn = heldMs >= 1500;                        // from here the footer says "power off"
  if (warn != (model_.heldMs >= 1500)) dirty_ = true;
  model_.heldMs = heldMs;
}

void LauncherApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t nowMs) {
  model_.selected = sel_;
  drawnBatt_ = battery_.percent();
  model_.battPct = drawnBatt_;
  drawnMinute_ = clock_.localMinutes(settings_.tzMinutes(), nowMs);
  model_.localMinutes = drawnMinute_;
  drawnMap_ = pack_ && pack_->packOk();
  model_.mapOk = drawnMap_;
  view_.draw(bm, model_);
  dirty_ = false;
}
