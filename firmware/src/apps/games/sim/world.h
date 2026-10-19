#pragma once
// SimWorld: glider + air + terrain, stepped at a fixed dt. Pure C++ (no hardware, no drawing), one global object
// (it owns the 48 kB height cache). Time is the sim clock `t` in seconds (the host's fixed steps), never the wall clock.
//
//   static World world;
//   world.begin(&source, seed, kTakeoffs[0]);         // fills the height cache (blocking), places the glider
//   world.step(controls, 0.02f);                      // physics; also feeds the cache a few cells per step
//   world.status(), world.glider(), world.sample()    // for the views and the HUD
//
// Landing: the glider touches the terrain model (z <= ground + 0.3 m). LANDED when it comes down at <= 4.5 m/s
// on ground not steeper than 35 degrees, CRASHED otherwise; the flight is over then (step() does nothing more).
#include <stdint.h>
#include "apps/games/sim/air.h"
#include "apps/games/sim/glider.h"
#include "apps/games/sim/height_cache.h"
#include "apps/games/sim/takeoffs.h"

namespace sim {

enum class Status : uint8_t { FLYING, LANDED, CRASHED };

class World {
 public:
  static constexpr int kTrailMax = 240;             // one point per second: the last 4 minutes
  static constexpr float kTouchSpeed = 4.5f;        // m/s, hardest survivable landing
  static constexpr float kMaxLandSlope = 0.70f;     // rise / run

  struct TrailPt { float x, y; };                // the map draws the track; the height is not needed

  World() : air_(1) {}
  // Start a flight. The cache window is filled completely before returning. windSpeed < 0 = the usual flying wind (5 m/s,
  // 18 km/h, a speed a pilot flying a 37 km/h trim can still make progress against).
  void begin(IHeightSource* src, uint32_t seed, const Takeoff& to, float windSpeed = -1.0f, float timeOfDay = 180.0f,
             float windFromOverride = -1.0f);
  // Same with explicit values (tests, scenarios): glider at (x, y) at altitude z flying heading deg, wind.
  void beginAt(IHeightSource* src, uint32_t seed, float x, float y, float aglStart, float headingDeg, float windFromDeg,
               float windSpeed, float timeOfDay = 0);

  // cacheBudget: cells of the height cache to load in this step (a run of substeps shares one budget, see SimApp::step).
  void step(const Controls& c, float dt, int cacheBudget = kCacheBudget);
  static constexpr int kCacheBudget = 48;
  // A start over the map pack shows the cells around the glider at once (kNearCells each way) and fills the rest of the window in the
  // first frames, kWarmBudget cells per step (the full fill took 1 to 2.7 s on the device).
  static constexpr int kNearCells = 24, kWarmBudget = 128;
  void setProgressive(bool on) { progressive_ = on; }
  // The day: how thermic (Air::setLevel, applies at the next begin) and the wind. A new wind takes effect at once but
  // eases in at 0.6 m/s per second (a 30 km/h change in about 14 s), so the air never jumps under the glider.
  void setLevel(int l) { level_ = l; }
  int level() const { return level_; }
  void setWindTarget(float fromDeg, float speed);
  float windSpeed() const { return tgtSpeed_; }
  // The usual flying wind of a site: the way its takeoff slope faces (direction) and 5 m/s (speed).
  static void usualWind(IHeightSource* src, const Takeoff& to, float* fromDeg, float* speed);
  // Roll feel: the wing tip on the stronger-lifting side rises, so the glider banks away from it by up to about 8 degrees; the
  // pilot turns toward the high wing. Rad of bank per m/s of lift difference across the span.
  static constexpr float kSpan = 10.0f;
  static constexpr float kKickPerMs = 0.35f;           // rad per (m/s): 0.26 m/s at a thermal's edge = 5 degrees
  static constexpr float kKickMax = 8.0f * 0.0174533f;
  float rollKick() const { return kick_; }
  static constexpr float kLaunchAgl = 150.0f;          // m above the takeoff at the start: the first seconds after leaving the ground

  // Profiling (see SimApp::setClock): per physics substep, microseconds spent in the air model's sample() and in the glider step.
  typedef uint32_t (*ClockUs)();
  struct StepProf { uint32_t airUs = 0, airMax = 0, gliderUs = 0, n = 0; };
  void setClock(ClockUs c) { clk_ = c; cache_.setClock(c); }
  StepProf takeProf() { StepProf p = prof_; prof_ = StepProf(); return p; }

  const Glider& glider() const { return g_; }
  Glider& gliderMut() { return g_; }
  const Air& air() const { return air_; }
  Air& airMut() { return air_; }
  HeightCache& cache() { return cache_; }
  const HeightCache& cache() const { return cache_; }
  const AirSample& sample() const { return s_; }
  Status status() const { return status_; }
  float time() const { return t_; }
  float vario() const { return vario_; }            // m/s, lagged like an instrument (0.8 s)
  float groundHeight() const { return ground_; }    // under the glider; NaN when the terrain is not loaded
  float agl() const { return agl_; }                // NaN when unknown
  float maxAltitude() const { return maxAlt_; }
  float startAltitude() const { return startAlt_; }
  float distanceFlown() const { return dist_; }
  float flightTime() const { return t_ - startT_; }  // s of sim clock since the start of this flight (x3 time counts 3 per second)
  // Straight line from the takeoff point (where this flight began), m.
  float distanceFromStart() const { return sqrtf((g_.x - startX_) * (g_.x - startX_) + (g_.y - startY_) * (g_.y - startY_)); }
  int trailCount() const { return trailN_; }
  TrailPt trailAt(int i) const { return trail_[(trailHead_ - trailN_ + i + kTrailMax * 2) % kTrailMax]; }
  float windFromDeg() const { return windFrom_; }
  void windAt(float z, float* wx, float* wy) const { air_.wind(z, wx, wy); }

 private:
  void place(float x, float y, float agl, float headingDeg);

  Glider g_;
  Air air_;
  HeightCache cache_;
  AirSample s_;
  Status status_ = Status::FLYING;
  float kick_ = 0;
  int level_ = 1;
  float windRef_ = 0, curE_ = 0, curN_ = 0, tgtE_ = 0, tgtN_ = 0, tgtSpeed_ = 0;
  float startX_ = 0, startY_ = 0, startT_ = 0;
  float t_ = 0, vario_ = 0, ground_ = 0, agl_ = 0, maxAlt_ = 0, startAlt_ = 0, dist_ = 0, trailT_ = 0, windFrom_ = 0;
  TrailPt trail_[kTrailMax];
  int trailN_ = 0, trailHead_ = 0;
  ClockUs clk_ = nullptr;
  bool progressive_ = false;
  StepProf prof_;
};

}  // namespace sim
