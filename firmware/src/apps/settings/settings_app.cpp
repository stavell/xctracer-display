#include "apps/settings/settings_app.h"
#include <stdio.h>
#include <string.h>
#include "platform/sensors/mount.h"

void SettingsApp::begin(uint32_t nowMs) {
  screen_ = S_LIST;
  sel_ = 0;
  heldMs_ = 0;
  model_.count = ROW_COUNT;
  model_.title[ROW_COMPASS] = "Compass";
  model_.title[ROW_MOUNT] = "Board mounting";
  model_.title[ROW_STARTUP] = "Start-up app";
  model_.title[ROW_INTRO] = "Intro";
  model_.title[ROW_TZ] = "Time zone";
  model_.title[ROW_ABOUT] = "About";
  model_.title[ROW_HOME] = "Home";
  battery_.update(nowMs);
  refresh();
  dirty_ = true;
}

void SettingsApp::end() {
  stopCal(false);                                          // leaving mid-calibration cancels it and switches the sensors off
}

// The value shown under each row.
void SettingsApp::refresh() {
  snprintf(model_.sub[ROW_COMPASS], sizeof model_.sub[0], "%s", cal_.stored() ? "calibrated, hold to redo" : "not calibrated, hold to start");
  snprintf(model_.sub[ROW_MOUNT], sizeof model_.sub[0], "position %d of 0-%d", settings_.mountIndex(), MOUNT_COUNT - 1);
  const int cur = apps_.startupApp(keys_);
  const AppEntry* e = apps_.find(cur);
  snprintf(model_.sub[ROW_STARTUP], sizeof model_.sub[0], "now: %s", e ? e->title : "-");
  snprintf(model_.sub[ROW_INTRO], sizeof model_.sub[0], "%s", settings_.introEnabled() ? "on: animation at power-on" : "off: straight to the app");
  const int tz = settings_.tzMinutes();
  const int a = tz < 0 ? -tz : tz;
  snprintf(model_.sub[ROW_TZ], sizeof model_.sub[0], "UTC%c%02d:%02d", tz < 0 ? '-' : '+', a / 60, a % 60);
  snprintf(model_.sub[ROW_ABOUT], sizeof model_.sub[0], "version, map pack, credits");
  snprintf(model_.sub[ROW_HOME], sizeof model_.sub[0], "back to the home screen");
}

void SettingsApp::startCal() {
  mount_ = settings_.mountIndex();
  cal_.start();
  screen_ = S_CAL;
  dirty_ = true;
}

void SettingsApp::stopCal(bool keep) {
  if (cal_.active()) cal_.finish(keep);
  if (sensorsOn_) { compass_.enable(false); sensorsOn_ = false; }
  if (screen_ == S_CAL) screen_ = S_LIST;
  refresh();
  dirty_ = true;
}

void SettingsApp::goHome() {
  if (nav()) nav()->openLauncher();
}

void SettingsApp::readSensors(uint32_t nowMs) {
  if (!sensorsOn_) { compass_.enable(true); sensorsOn_ = true; }
  Vec3 a, m, g;
  bool hg = false;
  if (compass_.read(a, m, g, hg)) cal_.feed(mountApply(mount_, a), mountApply(mount_, m), nowMs);
}

void SettingsApp::tick(uint32_t nowMs) {
  if (screen_ == S_CAL) readSensors(nowMs);
  battery_.update(nowMs);
}

void SettingsApp::move(int d) {
  sel_ = ((sel_ + d) % ROW_COUNT + ROW_COUNT) % ROW_COUNT;
  dirty_ = true;
}

// dir +1 / -1. With the single switch only +1 happens (long press), wrapping around.
void SettingsApp::change(int dir) {
  switch (sel_) {
    case ROW_MOUNT:
      settings_.setMountIndex((settings_.mountIndex() + dir + MOUNT_COUNT) % MOUNT_COUNT);
      break;
    case ROW_STARTUP:
      if (dir > 0) keys_.set(SLOT_BOOT_APP, (uint32_t)apps_.nextStartup(apps_.startupApp(keys_)));
      break;
    case ROW_INTRO:
      settings_.setIntroEnabled(!settings_.introEnabled());
      break;
    case ROW_TZ: {
      int tz = settings_.tzMinutes();
      const int step = dpadStep_ ? 30 : 60;
      tz += dir * step;
      if (tz > 14 * 60) tz = -12 * 60;                     // wrap: the switch only goes forward
      if (tz < -12 * 60) tz = 14 * 60;
      settings_.setTzMinutes(tz);
      break;
    }
    default: return;
  }
  refresh();
  dirty_ = true;
}

void SettingsApp::activate() {
  switch (sel_) {
    case ROW_COMPASS: startCal(); break;
    case ROW_ABOUT: screen_ = S_ABOUT; dirty_ = true; break;
    case ROW_HOME: goHome(); break;
    default: dpadStep_ = false; change(1); break;
  }
}

void SettingsApp::onKey(Key key, bool down, uint32_t) {
  if (!down) return;
  if (screen_ == S_CAL) {
    if (key == KEY_ACTION) stopCal(true);
    else if (key == KEY_B) stopCal(false);
    return;
  }
  if (screen_ == S_ABOUT) {
    if (key == KEY_ACTION || key == KEY_B) { screen_ = S_LIST; dirty_ = true; }
    return;
  }
  dpadStep_ = true;
  if (key == KEY_UP) move(-1);
  else if (key == KEY_DOWN) move(1);
  else if (key == KEY_LEFT) change(-1);
  else if (key == KEY_RIGHT) change(1);
  else if (key == KEY_ACTION) { dpadStep_ = true; activate(); }
  else if (key == KEY_B) goHome();
}

void SettingsApp::onInput(Key key, ButtonEvent ev) {
  if (key != KEY_MAIN) return;
  if (screen_ == S_CAL) {                                  // turn the unit in every direction; long = keep, short = cancel
    if (ev == BTN_LONG) stopCal(true);
    else if (ev == BTN_SHORT) stopCal(false);
    return;
  }
  if (screen_ == S_ABOUT) {
    if (ev == BTN_SHORT || ev == BTN_LONG) { screen_ = S_LIST; dirty_ = true; }
    return;
  }
  dpadStep_ = false;
  if (ev == BTN_SHORT) move(1);
  else if (ev == BTN_LONG) activate();
}

void SettingsApp::onHeld(uint32_t heldMs) {
  if ((heldMs >= 1500) != (heldMs_ >= 1500)) dirty_ = true;
  heldMs_ = heldMs;
}

bool SettingsApp::wantsFrame(uint32_t nowMs) {
  if (dirty_) return true;
  return screen_ == S_CAL && nowMs - lastDraw_ >= 200;
}

void SettingsApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t nowMs) {
  lastDraw_ = nowMs;
  dirty_ = false;
  const int batt = battery_.percent();
  if (screen_ == S_CAL) {
    CalModel c;
    cal_.span(c.span);
    c.fieldUt = cal_.fieldUt();
    c.stored = cal_.stored();
    c.battPct = batt;
    c.heldMs = heldMs_;
    c.holdWarning = "keep holding: cancel, home";
    drawCompassCal(bm, view_.fonts(), c);
  } else if (screen_ == S_ABOUT) {
    AboutModel a;
    a.version = version_;
    a.battPct = batt;
    a.packOk = pack_ && pack_->packOk();
    if (a.packOk) {
      a.packBytes = pack_->packBytes();
      pack_->packArea(a.area, sizeof a.area);
      pack_->packAttribution(0, a.credit1, sizeof a.credit1);
      pack_->packAttribution(2, a.credit2, sizeof a.credit2);   // the ASCII form: the glyph atlas has no (c) sign
    }
    view_.drawAbout(bm, a);
  } else {
    model_.selected = sel_;
    model_.battPct = batt;
    model_.heldMs = heldMs_;
    view_.drawList(bm, model_);
  }
}
