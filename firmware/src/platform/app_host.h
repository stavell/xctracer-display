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
// instead of step 4. The host keeps a frame clock: each time it is due the app's
// step(simMs, dtMs) runs once per elapsed interval (normally once; at most kMaxCatchUp when frames were late, so
// the simulation stays in real time), then ONE frame is cleared, drawn and presented. The simulation therefore advances
// in equal steps whatever the draw costs, and the picture is always the newest state. frameIntervalMs() == 0 (the
// default: the vario, Tetris) leaves everything above exactly as it was. The host counts frames per second and the
// longest gap between two presented frames (FrameStats), for a title bar or a status line.
#include <Adafruit_GFX.h>
#include <stddef.h>
#include "platform/power/pwr_log.h"
#include <stdint.h>
#include <string.h>
#include "platform/input/combo_hold.h"
#include "platform/ports.h"
#include "platform/ui/system_sheet.h"

class IAppNav {                                  // what an app may ask of the host
 public:
  virtual ~IAppNav() {}
  virtual bool hasLauncher() const = 0;
  virtual void openLauncher() = 0;               // takes effect after the current tick
  virtual void openApp(int index) = 0;           // same
  virtual int appCount() const = 0;
  virtual const char* appName(int index) const = 0;
  // True while the six buttons are the input in use (the last key edge came from one of them), false for the single switch (the
  // default). Screens word their hints by it, so a hint never names a control that was not touched. Both stay fitted and usable.
  virtual bool sixButtons() const { return false; }
  // True when the host's system sheet is on (box image): the 3 s hold then opens it, so an app must not announce its own power-off.
  virtual bool hasSystemSheet() const { return false; }
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

  // --- System sheet (box image; see platform/ui/system_sheet.h) ---
  // True when the hold and the combo must reach the app instead of opening the sheet (the start-up animation: they only skip it).
  virtual bool sheetBlocked() const { return false; }
  // The row the sheet's cursor starts on (Home unless the app is one the user usually wants Back from: the vario).
  virtual SystemSheet::Row sheetStart() const { return SystemSheet::R_HOME; }
  // The sheet opened (true) or closed (false) over this app. Opened: games pause here; the host has stopped stepping game-mode apps.
  virtual void onSheet(bool open, uint32_t nowMs) { (void)open; (void)nowMs; }

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
  uint32_t panelBusy = 0;       // frames whose present() found the previous SPI transfer still in flight (it waits for it)
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

namespace ButtonFsmMs { const uint32_t kLong = 700, kHold = 3000; }   // ButtonFsm::LONG_MS / HOLD_MS (this header does not include the fsm)

class AppHost : public IAppNav {
 public:
  static const int MAX_APPS = 10;
  // Simulation steps per game frame at most: the late frame's own step plus one extra. More would make a slow frame slower (every
  // extra step costs time the next frame does not have: measured on the device, 4 x a 16 ms step = 65 ms, 10 fps); a game that
  // cannot keep up runs in slow motion instead (the missed slots count as dropped).
  static const int kMaxCatchUp = 2;
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
  void setLinkGate(ILinkGate* g) { gate_ = g; }            // laptop link off while a game runs (null = never touched)
  void setComboHold(uint32_t ms) { combo_ = ComboHold(ms); comboMs_ = (uint16_t)ms; }
  void setRotation(uint8_t r) { rotation_ = r; }           // the panel rotation an app starts with (apps may change it)
  // Box image: turns on the system sheet. The 3 s hold and the A + B combo then open it over any app (not the ones that say
  // sheetBlocked()), and the sheet's Sleep / Power off call the port. Without this call (the vario-only image) the hold and the
  // combo behave as before: the app's own power-off, or back to the launcher.
  void setSystemSheet(IPower* power, SystemSheet* sheet) { power_ = power; sheet_ = sheet; }
  bool sheetOpen() const { return sheetOn(); }
  const SystemSheet& sheet() const { return *sheet_; }

  bool start(int index, uint32_t nowMs) {
    if (index < 0 || index >= n_) return false;
    if (active_) active_->end();
    active_ = apps_[index];
    pwrlog::note(nowMs, pwrlog::APP, index);
    activeIdx_ = index;
    pending_ = -1;
    aDown_ = bDown_ = false;
    keysDown_ = 0;
    if (sheet_) sheet_->close();
    forceFrame_ = barDirty_ = false;
    barStep_ = 0;
    combo_.reset();
    panel_.setRotation(rotation_);
    panel_.invalidate();                                    // the first frame of every app is sent in full
    gameClock_ = false;
    stats_ = FrameStats();
    active_->begin(nowMs);
    if (gate_) gate_->setLinksOn(active_->frameIntervalMs() == 0);   // after begin(): lazy apps know their mode only then
    return true;
  }

  void tick(uint32_t nowMs) {
    if (!active_) return;
    ButtonEvent ev = input_.poll(nowMs);
    if (ev != BTN_NONE) pwrlog::note(nowMs, pwrlog::SWITCH, (int32_t)ev, (int32_t)input_.heldMs(nowMs));
    KeyEdge e;
    while (input_.popEdge(e)) {
      if (e.down) six_ = e.key != KEY_MAIN;                 // the hint follows the control used last
      if (e.key == KEY_ACTION) aDown_ = e.down;
      else if (e.key == KEY_B) bDown_ = e.down;
      if (e.key < 8) keysDown_ = (uint8_t)(e.down ? (keysDown_ | (1u << e.key)) : (keysDown_ & ~(1u << e.key)));
      if (sheetOn()) { if (sheetAct(sheet_->key(e.key, e.down, e.ms), nowMs)) return; }
      else active_->onKey(e.key, e.down, e.ms);
    }
    const uint32_t held = input_.heldMs(nowMs);
    if (sheetOn()) {                                  // input goes to the sheet only
      if (ev != BTN_NONE && sheetAct(sheet_->input(ev, nowMs), nowMs)) return;
      if (sheetOn() && sheetAct(sheet_->tick(nowMs, held), nowMs)) return;
    } else {
      active_->onHeld(held);
      if (power_ && ev == BTN_HOLD && !active_->sheetBlocked()) openSheet(nowMs);
      else if (!power_ && ev == BTN_HOLD && canLeave() && !active_->handlesHold()) openLauncher();
      else if (ev != BTN_NONE) active_->onInput(KEY_MAIN, ev);
    }
    if (combo_.feed(aDown_, bDown_, nowMs)) {
      if (power_) { if (!sheetOn() && !active_->sheetBlocked()) openSheet(nowMs); }
      else if (canLeave() && active_->comboExit()) openLauncher();
    }

    updateHoldBar(nowMs, held);
    active_->tick(nowMs);
    if (pending_ >= 0) {                                   // switch before drawing: the new app draws first
      int p = pending_;
      pending_ = -1;
      start(p, nowMs);
      return;                                              // its first frame comes on the next iteration
    }
    const uint32_t fi = active_->frameIntervalMs();
    if (sheetOn()) {                                 // the app keeps ticking underneath; game-mode apps are not stepped
      const bool owns = active_->ownsPanel(nowMs);
      const bool changed = sheet_->takeDirty();
      if (changed || (fi == 0 && !owns && active_->wantsFrame(nowMs))) presentFrame(nowMs, !owns);
      return;
    }
    if (forceFrame_ || barDirty_) {                        // the sheet just closed / the hold bar moved: the app repaints in full
      forceFrame_ = barDirty_ = false;
      if (fi == 0 && !active_->ownsPanel(nowMs)) { presentFrame(nowMs, true); return; }
    }
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
    drawHoldBar(bm);
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
  // The index of the app with this name (what name() returns; "sim" is accepted for the flight sim), -1 when there is none.
  int indexOf(const char* name) const {
    if (!name) return -1;
    if (!strcmp(name, "sim")) name = "flightsim";
    for (int i = 0; i < n_; i++) if (!strcmp(apps_[i]->name(), name)) return i;
    return -1;
  }
  bool sixButtons() const override { return six_; }
  bool hasSystemSheet() const override { return power_ != nullptr; }

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
    // The panel may still be sending the previous frame: step and draw anyway. The transfer reads the packed tx buffer, not the frame
    // buffer, so the CPU work overlaps it; present() waits for the rest only when the transfer is still running by then.
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
    drawHoldBar(bm);
    if (probe_) probe_->draw(false);
    if (!panel_.ready()) stats_.panelBusy++;
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

  // The cue while the switch (or A + B) is held toward the sheet: a thin bar along the bottom edge from a bit past a normal press
  // (0.7 s) to the 3 s hold (1.5 s for the combo), drawn over whatever the app drew, in 16 steps. Visuals only: the app still sees
  // its long press on a release in between. A step change asks for one more frame (change-driven apps redraw, games draw anyway).
  static const int kBarSteps = 16;
  void updateHoldBar(uint32_t nowMs, uint32_t held) {
    int step = 0;
    if (power_ && !sheetOn() && !active_->sheetBlocked()) {
      if (held >= ButtonFsmMs::kLong && held < ButtonFsmMs::kHold)
        step = 1 + (int)((held - ButtonFsmMs::kLong) * (kBarSteps - 1) / (ButtonFsmMs::kHold - ButtonFsmMs::kLong));
      const uint32_t c = combo_.heldMs(nowMs);
      if (c >= ButtonFsmMs::kLong && c < comboMs_) {
        const int cs = 1 + (int)((c - ButtonFsmMs::kLong) * (kBarSteps - 1) / (comboMs_ - ButtonFsmMs::kLong));
        if (cs > step) step = cs;
      }
    }
    if (step != barStep_) { barStep_ = step; barDirty_ = true; }
  }
  void drawHoldBar(const Bitmap1& bm) {
    if (barStep_ <= 0) return;
    const int x = 10, w = ui::W - 20, y = ui::H - 7;           // 7 px strip at the very bottom edge, below the hint lines
    bm.fillRect(x, y, w, 7, DRAW_CLEAR);
    bm.fillRect(x + 1, y + 1, w - 2, 5, DRAW_SET);
    bm.fillRect(x + 2, y + 2, (w - 4) * barStep_ / kBarSteps, 3, DRAW_CLEAR);
    bm.fillRect(x + w, y - 4, 2, 11, DRAW_CLEAR);                // the mark: the menu opens where the bar ends
  }

  void openSheet(uint32_t nowMs) {
    const bool inLauncher = activeIdx_ == launcher_;
    sheet_->open(inLauncher ? SystemSheet::R_SLEEP : active_->sheetStart(), canLeave(), nowMs);
    pwrlog::note(nowMs, pwrlog::SHEET_OPEN, (int32_t)activeIdx_);
    active_->onHeld(0);
    active_->onSheet(true, nowMs);
    for (int k = 0; k < KEY_COUNT; k++)                    // keys down now would never see their release: end them for the app
      if (keysDown_ & (1u << k)) active_->onKey((Key)k, false, nowMs);
  }
  // True when the act ended this iteration (the box went to sleep or off).
  bool sheetAct(SystemSheet::Act a, uint32_t nowMs) {
    if (a == SystemSheet::A_NONE || !power_) return false;
    pwrlog::note(nowMs, pwrlog::SHEET_ACT, (int32_t)a);
    sheet_->close();
    panel_.invalidate();
    active_->onSheet(false, nowMs);
    gameClock_ = false;                                    // a game restarts its frame clock, no catch-up for the time it stood
    forceFrame_ = true;
    if (a == SystemSheet::A_HOME) { openLauncher(); return false; }
    if (a == SystemSheet::A_SLEEP) { power_->shutdown(); return true; }
    if (a == SystemSheet::A_OFF) { power_->powerOff(); return true; }
    return false;
  }
  // One frame with the sheet on top: the app's picture (unless the laptop owns the panel), then the sheet.
  void presentFrame(uint32_t nowMs, bool drawApp) {
    panel_.clear();
    Bitmap1 bm = panel_.bitmap();
    if (drawApp) active_->draw(bm, panel_.gfx(), nowMs);
    if (sheetOn()) sheet_->draw(bm, six_);
    else drawHoldBar(bm);
    panel_.present();
  }

  bool sheetOn() const { return sheet_ && sheet_->isOpen(); }
  bool canLeave() const { return hasLauncher() && activeIdx_ != launcher_; }

  IPanel& panel_;
  IInput& input_;
  IFrameProbe* probe_ = nullptr;
  ILinkGate* gate_ = nullptr;
  IApp* apps_[MAX_APPS] = {};
  int n_ = 0;
  IApp* active_ = nullptr;
  int activeIdx_ = -1;
  int launcher_ = -1;
  int pending_ = -1;
  uint8_t rotation_ = 1;
  bool aDown_ = false, bDown_ = false;
  ComboHold combo_;
  IPower* power_ = nullptr;                  // set: the system sheet is on (box image)
  SystemSheet* sheet_ = nullptr;             // owned by the box image (a vario-only image pays nothing for it)
  bool six_ = false, forceFrame_ = false, barDirty_ = false;
  int8_t barStep_ = 0;
  uint16_t comboMs_ = 1500;
  uint8_t keysDown_ = 0;                     // bit per Key: debounced state of every key, to release them when the sheet opens
  // game mode
  bool gameClock_ = false;
  uint32_t nextFrame_ = 0, simMs_ = 0, statAt_ = 0, lastPresent_ = 0;
  int winFrames_ = 0;
  uint32_t winWorst_ = 0;
  FrameStats stats_;
};
