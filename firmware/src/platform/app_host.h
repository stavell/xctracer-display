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
//
// Game mode: an app whose frameIntervalMs() is > 0 (games use 20 ms: a steady 50 fps) gets a fixed-timestep loop
// instead of step 4. The host keeps a frame clock: each time it is due (and the panel can take a frame) the app's
// step(simMs, dtMs) runs once per elapsed interval (normally once; at most kMaxCatchUp when frames were late, so
// the simulation stays in real time), then ONE frame is cleared, drawn and presented. The simulation therefore advances
// in equal steps whatever the draw costs, and the picture is always the newest state. frameIntervalMs() == 0 (the
// default: the vario, Tetris) leaves everything above exactly as it was. The host counts frames per second and the
// longest gap between two presented frames (FrameStats), for a title bar or a status line.
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

  // --- Game mode (fixed frame rate), see the top of this file ---
  // Frame interval in ms; 0 = change-driven (wantsFrame / nextWakeMs decide), > 0 = a frame every interval.
  virtual uint32_t frameIntervalMs() const { return 0; }
  // One fixed simulation step of dtMs (= frameIntervalMs()); simMs is the app's own simulation clock (starts
  // at the nowMs of the first step, advances by exactly dtMs). Physics belongs here, input handling in onKey().
  virtual void step(uint32_t simMs, uint32_t dtMs) { (void)simMs; (void)dtMs; }
  // App-specific part of the once-a-second game status line (platform/game_probe.h): "key=value ..." appended to dst, returns the
  // length written (0 = nothing). Called once per second, so it may reset per-second counters.
  virtual int gameInfo(char* dst, size_t cap) { (void)dst; (void)cap; return 0; }

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

// Frame statistics of the game mode, refreshed once a second.
struct FrameStats {
  int fps = 0;                  // frames presented in the last full second
  uint32_t worstGapMs = 0;      // longest time between two presented frames in that second
  uint32_t steps = 0;           // simulation steps since the app started
  uint32_t frames = 0;          // frames presented since the app started
  uint32_t dropped = 0;         // intervals skipped because the host fell behind by more than kMaxCatchUp
  uint32_t catchUp = 0;         // extra steps run for a frame that was late (slots whose picture was never drawn, but simulated)
  uint32_t panelBusy = 0;       // host iterations where a frame was due but the previous SPI transfer was still in flight
};

// Brackets the clear and the present of a frame (the draw-time profiler hooks in here); optional.
class IFrameProbe {
 public:
  virtual ~IFrameProbe() {}
  virtual void clear(bool begin) = 0;
  virtual void present(bool begin) = 0;
  // Game mode only: the whole catch-up loop of step() calls (one pair per step), and the app's draw().
  virtual void step(bool begin) { (void)begin; }
  virtual void draw(bool begin) { (void)begin; }
};

class AppHost : public IAppNav {
 public:
  static const int MAX_APPS = 8;
  static const int kMaxCatchUp = 4;                        // simulation steps per frame at most
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
    gameClock_ = false;
    stats_ = FrameStats();
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
    const uint32_t fi = active_->frameIntervalMs();
    if (fi > 0) {
      if (!active_->ownsPanel(nowMs)) gameFrame(nowMs, fi);
      return;
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

  // Earliest time the active app needs the host again. A game needs it at its next frame.
  uint32_t nextWakeMs(uint32_t nowMs) {
    if (!active_) return 0xFFFFFFFFu;
    return active_->frameIntervalMs() > 0 ? (gameClock_ ? nextFrame_ : nowMs) : active_->nextWakeMs(nowMs);
  }
  // How long the main loop may sleep between host iterations: 10 ms normally, 1 ms in game mode so the frame
  // clock is hit within a millisecond instead of within a loop period.
  uint32_t loopDelayMs() const { return active_ && active_->frameIntervalMs() > 0 ? 1 : 10; }
  bool gameMode() const { return active_ && active_->frameIntervalMs() > 0; }
  const FrameStats& stats() const { return stats_; }

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
  // One iteration of the game-mode loop: if a frame is due and the panel can take it, step the simulation for the
  // elapsed intervals and draw one frame.
  void gameFrame(uint32_t nowMs, uint32_t fi) {
    if (!gameClock_) {
      gameClock_ = true;
      nextFrame_ = nowMs;
      simMs_ = nowMs;
      statAt_ = nowMs;
      lastPresent_ = 0;
      winFrames_ = 0;
      winWorst_ = 0;
    }
    if ((int32_t)(nowMs - nextFrame_) < 0) return;
    if (!panel_.ready()) { stats_.panelBusy++; return; }
    int n = 0;
    while ((int32_t)(nowMs - nextFrame_) >= 0 && n < kMaxCatchUp) {
      simMs_ += fi;
      if (probe_) probe_->step(true);
      active_->step(simMs_, fi);
      if (probe_) probe_->step(false);
      nextFrame_ += fi;
      n++;
    }
    stats_.steps += (uint32_t)n;
    stats_.catchUp += (uint32_t)(n - 1);
    if ((int32_t)(nowMs - nextFrame_) >= 0) {              // still behind: skip what cannot be caught up
      const uint32_t late = nowMs - nextFrame_;
      stats_.dropped += late / fi + 1;
      nextFrame_ = nowMs + fi;                             // the next frame one interval from now
    }
    if (probe_) probe_->clear(true);
    panel_.clear();
    if (probe_) probe_->clear(false);
    Bitmap1 bm = panel_.bitmap();
    if (probe_) probe_->draw(true);
    active_->draw(bm, panel_.gfx(), nowMs);
    if (probe_) probe_->draw(false);
    if (probe_) probe_->present(true);
    panel_.present();
    if (probe_) probe_->present(false);
    stats_.frames++;
    winFrames_++;
    if (lastPresent_ != 0) {
      const uint32_t gap = nowMs - lastPresent_;
      if (gap > winWorst_) winWorst_ = gap;
    }
    lastPresent_ = nowMs ? nowMs : 1;
    if ((uint32_t)(nowMs - statAt_) >= 1000) {
      stats_.fps = winFrames_;
      stats_.worstGapMs = winWorst_;
      winFrames_ = 0;
      winWorst_ = 0;
      statAt_ += 1000 * ((nowMs - statAt_) / 1000);
    }
  }

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
  // game mode
  bool gameClock_ = false;
  uint32_t nextFrame_ = 0, simMs_ = 0, statAt_ = 0, lastPresent_ = 0;
  int winFrames_ = 0;
  uint32_t winWorst_ = 0;
  FrameStats stats_;
};
