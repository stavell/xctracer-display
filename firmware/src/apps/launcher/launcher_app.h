#pragma once
// The launcher: the box's home screen. Lists the registered apps as a grid and starts the chosen one.
//
// Input: with the single user switch a short press moves to the next tile, a long press (0.7 .. 1.5 s) opens it;
// with the six buttons the arrows move over the grid and A opens (both work at any time, the hint follows the one used last).
// The 3 s hold and A + B open the host's system sheet (cursor on Sleep). The start-up app is chosen in Settings; the launcher only reads it (AppRegistry::startupApp).
//
// The launcher is never started by itself: the box boots into the start-up app, and an app opens the
// launcher only on a deliberate gesture (see platform/app_host.h and README.md, "Box").
#include <stdint.h>
#include "apps/launcher/launcher_view.h"
#include "platform/app_host.h"
#include "platform/app_registry.h"
#include "platform/clock.h"
#include "platform/settings/box_settings.h"
#include "platform/terrain/pack_status.h"

class LauncherApp : public IApp {
 public:
  LauncherApp(IBattery& battery, IPower& power, const AppRegistry& apps, const BoxSettings& settings,
              const WallClock& clock)
      : battery_(battery), apps_(apps), settings_(settings), clock_(clock) { (void)power; }
  void setPack(const terrain::IPackStatus* pack) { pack_ = pack; }

  const char* name() const override { return "launcher"; }
  void begin(uint32_t nowMs) override;
  void end() override {}
  void tick(uint32_t nowMs) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  bool wantsFrame(uint32_t nowMs) override;
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  int selected() const { return sel_; }
  int columns() const { return LauncherView::COLS; }

 private:
  void move(int d);
  void activate();

  IBattery& battery_;
  const AppRegistry& apps_;
  const BoxSettings& settings_;
  const WallClock& clock_;
  const terrain::IPackStatus* pack_ = nullptr;
  int count_ = 0;
  int sel_ = 0;
  HomeModel model_;
  LauncherView view_;
  bool dirty_ = true;
  int drawnBatt_ = -2, drawnMinute_ = -2;
  bool drawnMap_ = false;
  uint32_t statusAt_ = 0;
};
