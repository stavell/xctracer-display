#pragma once
// The seam between the platform and an application. An app is one self-contained program (the flight
// display, games, tools); the host owns the active one, feeds it time and input, and runs the frame loop:
// when the app says it wants a frame the host clears the panel, lets the app draw, and presents.
// Pure C++: compiles on the PC (the emulator and the PC player run the very same host).
//
// One host iteration (AppHost::tick, every ~10 ms):
//   1. poll the input: key edges -> onKey(), the switch's short / long / hold -> onInput(), held time -> onHeld()
//   2. active->tick(now)
//   3. apply a pending app switch (an app asked for the launcher, or the hold / combo gesture did)
//   4. if the app does not own the panel and wantsFrame(now): clear, draw(bitmap, gfx, now), present
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/input/combo_hold.h"
#include "platform/ports.h"

class IAppNav {                                  // what an app may ask of the host
 public:
  virtual ~IAppNav() {}
  virtual bool hasLauncher() const = 0;
  virtual void openLauncher() = 0;               // takes effect after the current tick
  virtual void openApp(int index) = 0;           // same
  virtual int appCount() const = 0;
  virtual const char* appName(int index) const = 0;
};

class IApp {
 public:
  virtual ~IApp() {}
  virtual const char* name() const = 0;
  virtual void begin(uint32_t nowMs) = 0;   // become the active app
  virtual void end() = 0;                   // stop being the active app: release what begin() took
  virtual void tick(uint32_t nowMs) = 0;    // called every host iteration (about every 10 ms)

  // --- Input, delivered by the host before tick(). All default to "nothing". ---
  // Raw key edge: down = true on press, false on release, with the time of the edge. Apps that need
  // held keys (auto-repeat, soft drop) use this.
  virtual void onKey(Key key, bool down, uint32_t nowMs) { (void)key; (void)down; (void)nowMs; }
  // A recognised press of the user switch: BTN_SHORT / BTN_LONG on release, BTN_HOLD after 3 s while held
  // (not delivered when handlesHold() is false: the host takes it).
  virtual void onInput(Key key, ButtonEvent ev) { (void)key; (void)ev; }
  // How long the user switch has been down (0 when up), every iteration.
  virtual void onHeld(uint32_t mainHeldMs) { (void)mainHeldMs; }

  // --- Frames ---
  // True when a new frame should be drawn and presented now.
  virtual bool wantsFrame(uint32_t nowMs) { (void)nowMs; return false; }
  // Absolute time (ms) at which the app next needs tick() or a frame; UINT32_MAX when it only waits for
  // input. Lets the device sleep between frames.
  virtual uint32_t nextWakeMs(uint32_t nowMs) { (void)nowMs; return 0xFFFFFFFFu; }
  // Draw the whole frame onto the white canvas (the host cleared it first); bm and gfx are the same
  // buffer, the vario draws through gfx (+ bm for the fast paths), games through bm. Clears the
  // "wants frame" state.
  virtual void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) { (void)bm; (void)gfx; (void)nowMs; }
  // True while something else draws on the panel (the laptop's "host drawing" mode): the host then
  // neither clears nor presents.
  virtual bool ownsPanel(uint32_t nowMs) { (void)nowMs; return false; }

  // --- Gestures ---
  // True when the app wants the 3 s hold of the user switch itself (the vario: power off). Otherwise the
  // host takes it and returns to the launcher.
  virtual bool handlesHold() const { return false; }
  // True when holding A + B for 1.5 s may take the user back to the launcher from this app.
  virtual bool comboExit() const { return true; }

  void attach(IAppNav* nav) { nav_ = nav; }

 protected:
  IAppNav* nav() const { return nav_; }

 private:
  IAppNav* nav_ = nullptr;
};

// Brackets the clear and the present of a frame (the draw-time profiler hooks in here); optional.
class IFrameProbe {
 public:
  virtual ~IFrameProbe() {}
  virtual void clear(bool begin) = 0;
  virtual void present(bool begin) = 0;
};

class AppHost : public IAppNav {
 public:
  static const int MAX_APPS = 8;
  AppHost(IPanel& panel, IInput& input) : panel_(panel), input_(input) {}

  bool add(IApp& app) {
    if (n_ >= MAX_APPS) return false;
    apps_[n_++] = &app;
    app.attach(this);
    return true;
  }
  int count() const { return n_; }
  IApp* active() const { return active_; }
  int activeIndex() const { return activeIdx_; }
  void setLauncher(int index) { launcher_ = index; }       // the app that lists the others; -1 = none
  void setProbe(IFrameProbe* p) { probe_ = p; }
  void setComboHold(uint32_t ms) { combo_ = ComboHold(ms); }
  void setRotation(uint8_t r) { rotation_ = r; }           // the panel rotation an app starts with (apps may change it)

  bool start(int index, uint32_t nowMs) {
    if (index < 0 || index >= n_) return false;
    if (active_) active_->end();
    active_ = apps_[index];
    activeIdx_ = index;
    pending_ = -1;
    aDown_ = bDown_ = false;
    combo_.reset();
    panel_.setRotation(rotation_);
    panel_.invalidate();                                    // the first frame of every app is sent in full
    active_->begin(nowMs);
    return true;
  }

  void tick(uint32_t nowMs) {
    if (!active_) return;
    ButtonEvent ev = input_.poll(nowMs);
    KeyEdge e;
    while (input_.popEdge(e)) {
      if (e.key == KEY_ACTION) aDown_ = e.down;
      else if (e.key == KEY_B) bDown_ = e.down;
      active_->onKey(e.key, e.down, e.ms);
    }
    active_->onHeld(input_.heldMs(nowMs));
    if (ev == BTN_HOLD && canLeave() && !active_->handlesHold()) openLauncher();
    else if (ev != BTN_NONE) active_->onInput(KEY_MAIN, ev);
    if (combo_.feed(aDown_, bDown_, nowMs) && canLeave() && active_->comboExit()) openLauncher();

    active_->tick(nowMs);
    if (pending_ >= 0) {                                   // switch before drawing: the new app draws first
      int p = pending_;
      pending_ = -1;
      start(p, nowMs);
      return;                                              // its first frame comes on the next iteration
    }
    if (active_->ownsPanel(nowMs) || !active_->wantsFrame(nowMs)) return;
    if (probe_) probe_->clear(true);
    panel_.clear();
    if (probe_) probe_->clear(false);
    Bitmap1 bm = panel_.bitmap();
    active_->draw(bm, panel_.gfx(), nowMs);
    if (probe_) probe_->present(true);
    panel_.present();
    if (probe_) probe_->present(false);
  }

  // Earliest time the active app needs the host again.
  uint32_t nextWakeMs(uint32_t nowMs) { return active_ ? active_->nextWakeMs(nowMs) : 0xFFFFFFFFu; }

  // Forward input without polling (tests, scripted players).
  void input(Key key, ButtonEvent ev) {
    if (active_) active_->onInput(key, ev);
  }
  void key(Key k, bool down, uint32_t nowMs) {
    if (active_) active_->onKey(k, down, nowMs);
  }

  // IAppNav
  bool hasLauncher() const override { return launcher_ >= 0 && launcher_ < n_; }
  void openLauncher() override {
    if (hasLauncher() && activeIdx_ != launcher_) pending_ = launcher_;
  }
  void openApp(int index) override {
    if (index >= 0 && index < n_) pending_ = index;
  }
  int appCount() const override { return n_; }
  const char* appName(int index) const override { return index >= 0 && index < n_ ? apps_[index]->name() : ""; }

 private:
  bool canLeave() const { return hasLauncher() && activeIdx_ != launcher_; }

  IPanel& panel_;
  IInput& input_;
  IFrameProbe* probe_ = nullptr;
  IApp* apps_[MAX_APPS] = {};
  int n_ = 0;
  IApp* active_ = nullptr;
  int activeIdx_ = -1;
  int launcher_ = -1;
  int pending_ = -1;
  uint8_t rotation_ = 1;
  bool aDown_ = false, bDown_ = false;
  ComboHold combo_;
};
