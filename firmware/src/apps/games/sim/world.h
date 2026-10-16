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

  struct TrailPt { float x, y, z; };

  World() : air_(1) {}
  // Start a flight. The cache window is filled completely before returning.
  void begin(IHeightSource* src, uint32_t seed, const Takeoff& to, float windSpeed = 5.0f);
  // Same with explicit values (tests, scenarios): glider at (x, y) at altitude z flying heading deg, wind.
  void beginAt(IHeightSource* src, uint32_t seed, float x, float y, float aglStart, float headingDeg, float windFromDeg,
               float windSpeed, float timeOfDay = 0);

  void step(const Controls& c, float dt);

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
  float t_ = 0, vario_ = 0, ground_ = 0, agl_ = 0, maxAlt_ = 0, startAlt_ = 0, dist_ = 0, trailT_ = 0, windFrom_ = 0;
  TrailPt trail_[kTrailMax];
  int trailN_ = 0, trailHead_ = 0;
};

}  // namespace sim
