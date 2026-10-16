#include "apps/launcher/launcher_app.h"
#include <stdio.h>
#include <string.h>

bool LauncherApp::addEntry(const char* title, const char* sub, int appIndex, LauncherIcon icon) {
  if (n_ >= MAX_ENTRIES) return false;
  entries_[n_++] = Entry{title, sub, appIndex, icon};
  return true;
}

int LauncherApp::startupApp() const {
  if (n_ == 0) return 0;
  uint32_t v = store_.get(SLOT_BOOT_APP);
  for (int i = 0; i < n_; i++)
    if ((uint32_t)entries_[i].app == v) return entries_[i].app;
  return entries_[0].app;                                  // anything unknown: the first app (the vario)
}

// The "Start-up" row shows which app the box starts with.
void LauncherApp::refreshStartupRow() {
  const int cur = startupApp();
  const char* nm = "";
  for (int i = 0; i < n_; i++)
    if (entries_[i].app == cur) nm = entries_[i].title;
  snprintf(startupSub_, sizeof startupSub_, "now: %s", nm);
}

void LauncherApp::begin(uint32_t nowMs) {
  refreshStartupRow();
  model_.count = 0;
  for (int i = 0; i < n_; i++) model_.rows[model_.count++] = LauncherRow{entries_[i].title, entries_[i].sub, entries_[i].icon};
  model_.rows[model_.count++] = LauncherRow{"Start-up", startupSub_, ICON_GEAR};
  model_.version = version_;
  if (sel_ >= model_.count) sel_ = 0;
  model_.heldMs = 0;
  battAt_ = nowMs;
  battery_.update(nowMs);
  dirty_ = true;
}

void LauncherApp::tick(uint32_t nowMs) {
  battery_.update(nowMs);
  if (nowMs - battAt_ >= 5000) {                           // the percentage changes slowly
    battAt_ = nowMs;
    if (battery_.percent() != drawnBatt_) dirty_ = true;
  }
}

void LauncherApp::move(int d) {
  const int rows = n_ + 1;
  sel_ = ((sel_ + d) % rows + rows) % rows;
  dirty_ = true;
}

void LauncherApp::activate() {
  if (sel_ < n_) {
    if (nav()) nav()->openApp(entries_[sel_].app);
    return;
  }
  // "Start-up": cycle to the next app and remember it.
  const int cur = startupApp();
  int idx = 0;
  for (int i = 0; i < n_; i++)
    if (entries_[i].app == cur) idx = i;
  store_.set(SLOT_BOOT_APP, (uint32_t)entries_[(idx + 1) % n_].app);
  refreshStartupRow();
  dirty_ = true;
}

void LauncherApp::onKey(Key key, bool down, uint32_t) {
  if (!down) return;
  if (key == KEY_UP) move(-1);
  else if (key == KEY_DOWN) move(1);
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

void LauncherApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t) {
  model_.selected = sel_;
  drawnBatt_ = battery_.percent();
  model_.battPct = drawnBatt_;
  view_.draw(bm, model_);
  dirty_ = false;
}
