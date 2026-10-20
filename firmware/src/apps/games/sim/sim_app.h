#pragma once
// The flight simulator as an IApp: keys -> controls -> World at a fixed 20 ms step, two views of it.
//
// Game mode: frameIntervalMs() = 20, so the host steps the physics once per 20 ms of sim time and draws one frame
// after each (a steady 50 fps). Input is read in onKey(); the controls are ramped in step().
//
// Keys (PC player / the console's D-pad and A / B):
//   LEFT / RIGHT   bank (ramps to full over 0.4 s, back to level when released)
//   UP             speed bar (accelerate)      DOWN   brake (slow down; with a stall warning at the end)
//   A              toggle top-down map / 3D chase view
//   B or MAIN      pause menu: Resume, View, Assist (thermals shown: on the map, in 3D as columns / bubbles / both), Site, Controls, Zero tilt, Speed
//                  (x1 / x3 / x5), New flight, Home
// Time scale: the world (glider, air, flight clock, trail) runs `scale` x faster than real time, in 20 ms substeps so the
//   physics is exactly what x1 would compute; rendering stays 50 fps. The player's own response (button and tilt ramps)
//   stays in real time, the wing's lags (roll, speed, stall) are physics and run in sim time.
//   after landing: A new flight, B Home
// One button (setOneButton, the box without front buttons; MAIN = the board's switch): every list is a menu of the vario's kind,
//   short press = next row (wraps), long press = act on the row (A). Day screen: Site .. Start, Home. Flying: short = pause menu,
//   long = map / 3D. Pause menu: as above, Controls always tilt (the row re-opens the calibration). End screen: short = New flight /
//   Home, long = do it. Calibration page: long press = zero and fly. Starts with Control::TILT and opens the calibration page
//   before the first flight (hold the box the way you fly).
// Controls (pause menu: Controls Buttons / Tilt; the PC player starts with Buttons):
//   Tilt:    roll of the box = bank, pitch = speed bar (top away) / brakes (top toward you), through TiltMap
//            (deadzone, expo, zero offset). Choosing Tilt, or "Zero tilt", opens a calibration page: hold the box the way
//            you will fly and press A. In the 3D view the horizon counter-rotates by the box roll, so it stays level
//            to the real world (a window into the sky).
//   Buttons: as above (D-pad). The tilt source is an ITilt: the AHRS on the device, the mouse in the PC player (T).
#include <new>
#include <stdint.h>
#include "platform/gfx3d/chase.h"
#include "apps/games/sim/gfx3d_use.h"
#include "apps/games/sim/sim_hud.h"
#include "platform/gfx3d/sky3d.h"
#include "platform/gfx3d/terrain_source.h"
#include "apps/games/sim/tilt.h"
#include "apps/games/sim/topdown.h"
#include "platform/gfx3d/voxel.h"
#include "platform/gfx3d/world.h"
#include "platform/app_host.h"

namespace sim {

class SimApp : public IApp {
 public:
  static constexpr uint32_t kFrameMs = 20;
  enum class View : uint8_t { TOP, CHASE };
  enum class Mode : uint8_t { NOMAP, FLYING, PAUSED, ENDED, CALIB, DAY };
  enum class Control : uint8_t { BUTTONS, TILT };
  static constexpr int kMenuRows = 12;
  static constexpr int kDayRows = 5;                   // Site, Thermals, Wind from, Wind speed, Start
  static constexpr int kWindSpeeds = 6;
  static constexpr float kWindKmh[kWindSpeeds] = {0, 10, 15, 20, 25, 30};

  SimApp() {}
  ~SimApp() { dropViews(); }
  SimApp(const SimApp&) = delete;
  SimApp& operator=(const SimApp&) = delete;
  // The map pack (platform Terrain); without it the app shows a message instead of flying.
  void setTerrain(terrain::Terrain* t) { terrain_ = t; }
  void setTilt(ITilt* t) { tilt_ = t; }
  void setOneButton(bool on) { oneButton_ = on; }
  bool oneButton() const { return oneButton_; }
  int endRow() const { return endSel_; }
  Control control() const { return ctl_; }
  // Time acceleration x1 / x3 / x5 (default x3): each 20 ms frame runs that many 20 ms physics substeps.
  int timeScale() const { return scale_; }
  void setTimeScale(int s) { scale_ = (s >= 5) ? 5 : (s >= 3 ? 3 : 1); }
  void cycleTimeScale() { setTimeScale(scale_ == 1 ? 3 : (scale_ == 3 ? 5 : 1)); }
  void setControl(Control c);                         // Tilt opens the calibration page
  void zeroTilt();                                    // take the current tilt as level
  // Profiling for the game status line (platform/game_probe.h): with a microsecond clock set, step() and draw() time their parts and
  // gameInfo() reports them (view, scale, tile decodes, then avg/max ms of world physics, height-cache refresh, map update, 3D
  // render, sky, glider + HUD) for the second since the last call. No clock: gameInfo() still gives view, scale and decodes.
  typedef uint32_t (*ClockUs)();
  void setClock(ClockUs c) { clk_ = c; world_.setClock(c); }
  int gameInfo(char* dst, size_t cap) override;

  const char* name() const override { return "flightsim"; }
  void begin(uint32_t nowMs) override;
  void end() override { if (tilt_) tilt_->enable(false); }
  void tick(uint32_t nowMs) override { (void)nowMs; }
  void onKey(Key key, bool down, uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  uint32_t frameIntervalMs() const override { return kFrameMs; }
  void step(uint32_t simMs, uint32_t dtMs) override;
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;
  bool comboExit() const override { return true; }
  // The system sheet opened: a flight in the air stands still on its pause menu (the host has already stopped stepping).
  void onSheet(bool open, uint32_t) override {
    if (open && mode_ == Mode::FLYING) mode_ = Mode::PAUSED, menuSel_ = 0;
    left_ = right_ = up_ = down_ = false;
  }

  // For the tests, the shots and the player.
  World& world() { return world_; }
  const World& world() const { return world_; }
  // The two views share one block of RAM (only one is drawn at a time): asking for the one that is not alive builds it and
  // destroys the other. The map picture is rebuilt on every switch to the map anyway (setView), the 3D view keeps no state.
  TopDownView& top();
  VoxelView& voxel();
  const ChaseCam& chase() const { return chase_; }
  Mode mode() const { return mode_; }
  View view() const { return view_; }
  void setView(View v);
  void setAssist(bool a) { assist_ = a; }
  bool assist() const { return assist_; }
  void setThermalStyle(SkyFx::Style st) { style_ = st; }
  SkyFx::Style thermalStyle() const { return style_; }
  SkyFx& sky();
  int site() const { return site_; }
  void setSite(int i) { site_ = (i % kTakeoffCount + kTakeoffCount) % kTakeoffCount; }
  uint32_t frames() const { return frame_; }
  Controls controls() const { return c_; }
  // Start (or restart) a flight at the chosen site; fills the height cache (blocking).
  void startFlight();
  // Start at an explicit spot (tests, scenarios).
  void startAt(float x, float y, float aglStart, float headingDeg, float windFromDeg, float windSpeed, uint32_t seed,
               float timeOfDay);
  void setSource(IHeightSource* s) { source_ = s; }   // tests: a synthetic surface instead of the pack
  uint32_t seed() const { return seed_; }
  // The day: how thermic (0 weak .. 3 epic, 1 = medium, the tuned good day), wind direction (0 = the site's usual, 1..8 = N, NE, ..
  // NW) and wind speed (0 = usual, 1..6 = 0 / 10 / 15 / 20 / 25 / 30 km/h). Site and thermals apply when a flight starts; the wind
  // applies at once (the air eases into it over about 15 s) when changed in the pause menu.
  int level() const { return level_; }
  int windDir() const { return dirIdx_; }
  int windSpeedIdx() const { return spdIdx_; }
  void setDay(int level, int dirIdx, int spdIdx);
  void showDayScreen(bool on) { dayScreen_ = on; }       // begin() opens the Day screen (default) or flies at once
  int dayRow() const { return dayRow_; }
  static const char* dirName(int idx);
  void setSeed(uint32_t s) { seed_ = s; }

 private:
  IHeightSource* source();
  void applyKeys(float dt);
  void menuAct();
  void oneButtonInput(ButtonEvent ev);
  void dayKey(Key key);
  void applyWind();
  void resolveWind(float* fromDeg, float* speed) const;     // -1 = the site's usual
  void drawFlight(Bitmap1& bm);
  HudData hud() const;

  terrain::Terrain* terrain_ = nullptr;
  TerrainSource tsrc_;
  IHeightSource* source_ = nullptr;
  ITilt* tilt_ = nullptr;
  Control ctl_ = Control::BUTTONS;
  float zeroRoll_ = 0, zeroPitch_ = 0, boxRoll_ = 0, rawRoll_ = 0, rawPitch_ = 0;
  World world_;
  struct ChaseViews { VoxelView voxel; SkyFx sky; };
  static constexpr size_t kViewBytes = sizeof(TopDownView) > sizeof(ChaseViews) ? sizeof(TopDownView) : sizeof(ChaseViews);
  void dropViews();
  ChaseViews& chaseViews();
  alignas(8) unsigned char views_[kViewBytes];
  TopDownView* topV_ = nullptr;
  ChaseViews* chaseV_ = nullptr;
  ChaseCam chase_;
  SkyFx::Style style_ = SkyFx::Style::BUBBLES;      // the default: cheapest and cleanest, strength shows as motion
  SimHud hud_;
  Mode mode_ = Mode::NOMAP;
  View view_ = View::TOP;
  bool assist_ = false;
  bool dayScreen_ = true, oneButton_ = false, calibrated_ = false;
  int endSel_ = 0;
  int level_ = 1, dirIdx_ = 0, spdIdx_ = 0, dayRow_ = 0, flownSite_ = 0, flownLevel_ = 1;
  float usualFrom_ = 0, usualSpeed_ = 5.0f;
  int scale_ = 3;
  int site_ = 0, menuSel_ = 0, flights_ = 0;
  uint32_t seed_ = 7;
  uint32_t frame_ = 0;
  bool left_ = false, right_ = false, up_ = false, down_ = false;
  Controls c_;
  struct Acc {
    uint32_t sum = 0, max = 0, n = 0;
    void add(uint32_t us) { sum += us; if (us > max) max = us; n++; }
  };
  enum { P_WORLD, P_TOPUPD, P_CHASEUPD, P_BLIT, P_VOXEL, P_SKY, P_SPRITE, P_HUD, P_ASSIST, P_TRAIL, P_SYMBOL, P_NEARBY, P_RINGS, P_COUNT };
  uint32_t t() const { return clk_ ? clk_() : 0; }
  void lap(int part, uint32_t t0) { if (clk_) prof_[part].add(clk_() - t0); }
  ClockUs clk_ = nullptr;
  Acc prof_[P_COUNT];
  uint32_t lastDecodes_ = 0, rings_ = 0, ringR_ = 0;
};

}  // namespace sim
