#pragma once
// The start-up animation of the box image: about 4.5 s at 50 fps (game frame mode) between power-on and the start-up app.
//
// Storyboard: the flight sim's chase camera behind a paraglider thermaling a full turn and a bit just under cloudbase over the real terrain
// of the map pack, climbing into the cumulus above; the cloud fills the screen as dithered white and "XC Display" with "stavl.com" under it
// shows through it as the end frame; the white is the transition into the start-up app. Without a map pack (or one that does not
// cover a takeoff of the sim) a 2D version: layered contour lines falling away under growing clouds, the same white-out and name.
//
// Everything is scripted from the step counter (20 ms per step), so the animation is the same on every run and at any frame
// rate; the host's catch-up keeps its length in real time when a frame is late. Any press skips it: the press is used up here
// (the start-up app never sees it), and the 3 s hold, which in other apps goes to the home screen, only skips.
//
// It reuses the sim's renderer pieces through their public interfaces and does not change them: World (only for the height cache
// and the air model, never stepped), VoxelView, SkyFx, ChaseCam. The height cache window is filled in begin() from the pack
// within a radius of the takeoff (cells beyond it are a flat skirt that costs no pack access), see kFillRadiusM.
//
// Memory: an arena app (platform/lazy_app.h) of about the sim's size; destroyed (and its fonts freed) before the start-up app is built.
#include "platform/gfx3d/chase.h"
#include "platform/gfx3d/sky3d.h"
#include "platform/gfx3d/terrain_source.h"
#include "platform/gfx3d/voxel.h"
#include "platform/gfx3d/world.h"
#include "platform/app_host.h"
#include "platform/gfx/font.h"
#include "platform/terrain/terrain.h"

namespace intro {

// Heights of the sim's local frame inside a square of half-width `r` around the takeoff from `inner`, a flat `skirt` outside.
class WindowSource : public gfx3d::IHeightSource {
 public:
  void set(gfx3d::IHeightSource* inner, float r, float skirt) { in_ = inner; r_ = r; skirt_ = skirt; }
  bool at(float x, float y, float* h) override {
    if (x > r_ || x < -r_ || y > r_ || y < -r_) { *h = skirt_; return true; }
    return in_ && in_->at(x, y, h);
  }
  uint32_t cost() const override { return in_ ? in_->cost() : 0; }

 private:
  gfx3d::IHeightSource* in_ = nullptr;
  float r_ = 0, skirt_ = 0;
};

class IntroApp : public IApp {
 public:
  static constexpr uint32_t kStepMs = 20;
  static constexpr int kSteps = 225;                       // 4.5 s
  static constexpr uint32_t kTotalMs = kSteps * kStepMs;
  static constexpr int kTiles = 4;                         // pack tiles kept decoded while the window is filled (the sim's 2 thrash on a fill)
  static constexpr float kFillRadiusM = 6000.0f;           // the window is real terrain this far from the takeoff (a skirt beyond)
  static constexpr uint32_t kSeed = 13;                     // the sim day the scene is picked from (deterministic)

  enum class Mode : uint8_t { TERRAIN, FLAT };             // FLAT: the 2D version (no pack, or no takeoff of the sim is covered)

  // pack: the map pack's reader or nullptr. next: the host index of the app to open when the animation ends or is skipped.
  explicit IntroApp(terrain::IBlockReader* pack);
  void setNext(int appIndex) { next_ = appIndex; }
  typedef uint32_t (*ClockUs)();
  void setClock(ClockUs c) { clk_ = c; }                   // microseconds, for the begin() timings in gameInfo()

  const char* name() const override { return "intro"; }
  void begin(uint32_t nowMs) override;
  void end() override;
  void tick(uint32_t nowMs) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  uint32_t frameIntervalMs() const override { return kStepMs; }
  void step(uint32_t simMs, uint32_t dtMs) override;
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;
  bool handlesHold() const override { return true; }      // the hold only skips; it must not take the box to the home screen
  bool comboExit() const override { return false; }
  bool sheetBlocked() const override { return true; }      // the hold and the combo only skip the animation
  int gameInfo(char* dst, size_t cap) override;

  Mode mode() const { return mode_; }
  int stepsDone() const { return steps_; }
  bool finished() const { return done_; }
  bool skipped() const { return skipped_; }
  int site() const { return site_; }                       // index in gfx3d::kTakeoffs, -1 in FLAT mode
  uint32_t beginUs() const { return beginUs_; }            // time spent in begin() (fill + scene search), 0 without a clock
  uint32_t fillUs() const { return fillUs_; }
  // Scripted state, for tests and the contact sheet.
  const gfx3d::Glider& glider() const { return g_; }
  const gfx3d::World& world() const { return world_; }
  float cloudBaseM() const { return world_.air().cloudBase(); }

 private:
  void startTerrainScene();
  bool pickThermal();
  void pose(uint32_t ms);
  void finish(bool skipped);
  void drawTerrainScene(const Bitmap1& bm);
  void drawFlatScene(const Bitmap1& bm, uint32_t ms);
  void cloudBank(const Bitmap1& bm, uint32_t ms);
  void whiteout(const Bitmap1& bm, float p);
  void drawLogo(const Bitmap1& bm, int level);

  terrain::TerrainWith<kTiles, false> terrain_;
  bool terrainOk_ = false;
  Mode mode_ = Mode::FLAT;
  int next_ = 0;
  ClockUs clk_ = nullptr;

  // scene
  gfx3d::TerrainSource src_;
  WindowSource win_;
  gfx3d::World world_;                    // the height cache and the air only
  gfx3d::VoxelView voxel_;
  gfx3d::SkyFx sky_;
  gfx3d::ChaseCam chase_;
  gfx3d::Glider g_;
  gfx3d::ColumnInfo col_;
  gfx3d::ColumnInfo cand_[gfx3d::Air::kMaxInfo];     // the thermals found while picking the scene
  int site_ = -1;
  float tScene_ = 0;                    // sim day time at step 0
  float zStart_ = 0, zEnd_ = 0, theta0_ = 0;

  // frame state
  int steps_ = 0;
  bool done_ = false, skipped_ = false;
  uint32_t beginUs_ = 0, fillUs_ = 0, searchUs_ = 0;
  Font1 title_, sub_;                   // "XC Display" and "stavl.com" on the end frame
  uint8_t white_[65][8];                // dither bytes: bit k of [level][x & 7] is white where Bayer(x, 7 - k) < level
  int8_t wob_[240];                     // the ragged front of the white-out, per screen column
};

}  // namespace intro
