#pragma once
// The launcher: lists the apps of the image and starts the chosen one.
//
// Input: with the single user switch a short press moves to the next row, a long press (0.7 .. 1.5 s)
// starts it; with the six buttons UP / DOWN move and A starts. The last row, "Start-up", picks the app the
// box starts when it is switched on (default: the first app, the vario) and is changed with the same
// start gesture. Holding the switch 3 s powers the box off, as in the vario.
//
// The launcher is never started by itself: the box boots into the start-up app, and an app opens the
// launcher only on a deliberate gesture (see platform/app_host.h and README.md, "Box").
#include <stdint.h>
#include "apps/launcher/launcher_view.h"
#include "platform/app_host.h"

class LauncherApp : public IApp {
 public:
  LauncherApp(IBattery& battery, IPower& power, IKeyStore& store, const char* version, bool dpad = false)
      : battery_(battery), power_(power), store_(store), version_(version) { model_.dpad = dpad; }

  // Add an app to the list; appIndex is its index in the AppHost. Call before the host starts.
  bool addEntry(const char* title, const char* sub, int appIndex, LauncherIcon icon);
  // The app the box starts at power-on: the stored choice if it is one of the entries, else the first entry.
  int startupApp() const;

  const char* name() const override { return "launcher"; }
  void begin(uint32_t nowMs) override;
  void end() override {}
  void tick(uint32_t nowMs) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  void onHeld(uint32_t heldMs) override;
  bool wantsFrame(uint32_t nowMs) override { (void)nowMs; return dirty_; }
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;
  bool handlesHold() const override { return true; }      // power off
  bool comboExit() const override { return false; }      // already there

  int selected() const { return sel_; }

 private:
  static const int MAX_ENTRIES = LauncherModel::MAX_ROWS - 1;
  void move(int d);
  void activate();
  void refreshStartupRow();

  IBattery& battery_;
  IPower& power_;
  IKeyStore& store_;
  const char* version_;
  struct Entry { const char* title; const char* sub; int app; LauncherIcon icon; };
  Entry entries_[MAX_ENTRIES];
  int n_ = 0;
  int sel_ = 0;
  char startupSub_[32] = "";
  LauncherModel model_;
  LauncherView view_;
  bool dirty_ = true;
  int drawnBatt_ = -2;
  uint32_t battAt_ = 0;
};
