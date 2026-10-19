#pragma once
// Settings: the settings that belong to the box, not to one app. It is an app of the box image (apps/settings),
// built on the platform pieces that hold the actual logic and storage so any app can use them:
//   platform/settings/box_settings.h   time zone, mounting, magnetometer calibration (key store)
//   platform/settings/compass_cal.h    the calibration flow, platform/settings/cal_view.h its screen
//   platform/app_registry.h            the start-up app
//
// Rows: Compass calibration, Board mounting, Start-up app, Intro (on / off), Time zone, Power off (clock / off), About, Home.
//
// Input, single switch: short = next row, long = change the value / open (calibration: short cancels, long saves);
// 3 s hold = back to the home screen (the host does that). Six buttons: UP / DOWN move, LEFT / RIGHT change a value
// (mounting +-1, time zone +-30 min), A opens or changes, B goes back (a sub-screen, then home).
//
// Flight safety: the compass sensors run only while the calibration screen is open and are switched off when it
// closes or the app is left.
#include <stdint.h>
#include "apps/settings/settings_view.h"
#include "platform/app_host.h"
#include "platform/app_registry.h"
#include "platform/settings/box_settings.h"
#include "platform/settings/cal_view.h"
#include "platform/settings/compass_cal.h"
#include "platform/terrain/pack_status.h"

class SettingsApp : public IApp {
 public:
  SettingsApp(IBattery& battery, ICompass& compass, IKeyStore& keys, BoxSettings& settings,
              const AppRegistry& apps, const char* version, bool dpad = false)
      : battery_(battery), compass_(compass), keys_(keys), settings_(settings), apps_(apps), version_(version), cal_(settings) {
    model_.dpad = dpad;
  }
  void setPack(const terrain::IPackStatus* pack) { pack_ = pack; }

  enum Row { ROW_COMPASS, ROW_MOUNT, ROW_STARTUP, ROW_INTRO, ROW_TZ, ROW_POWER, ROW_ABOUT, ROW_HOME, ROW_COUNT };
  enum Screen { S_LIST, S_CAL, S_ABOUT };

  const char* name() const override { return "settings"; }
  void begin(uint32_t nowMs) override;
  void end() override;
  void tick(uint32_t nowMs) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  void onHeld(uint32_t heldMs) override;
  bool wantsFrame(uint32_t nowMs) override;
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  Screen screen() const { return screen_; }
  int selected() const { return sel_; }
  bool calibrating() const { return cal_.active(); }

 private:
  void refresh();
  void move(int d);
  void change(int dir);               // value rows: mounting, start-up app, time zone
  void activate();
  void startCal();
  void stopCal(bool keep);
  void goHome();
  void readSensors(uint32_t nowMs);

  IBattery& battery_;
  ICompass& compass_;
  IKeyStore& keys_;
  BoxSettings& settings_;
  const AppRegistry& apps_;
  const char* version_;
  const terrain::IPackStatus* pack_ = nullptr;
  CompassCal cal_;
  Screen screen_ = S_LIST;
  int sel_ = 0;
  int mount_ = 0;                     // mounting in use while calibrating
  bool sensorsOn_ = false;
  bool dirty_ = true;
  bool dpadStep_ = false;             // the last change came from the arrows (finer time zone steps)
  uint32_t lastDraw_ = 0, heldMs_ = 0;
  SettingsModel model_;
  SettingsView view_;
};
