#include "apps/games/sim/world.h"

namespace sim {

void World::place(float x, float y, float aglStart, float headingDeg) {
  float gh = 0;
  if (!cache_.height(x, y, &gh)) gh = 0;
  g_.reset(x, y, gh + aglStart, headingDeg * kDeg2Rad);
  status_ = Status::FLYING;
  vario_ = 0;
  ground_ = gh;
  agl_ = aglStart;
  maxAlt_ = startAlt_ = g_.z;
  dist_ = 0;
  trailN_ = trailHead_ = 0;
  trailT_ = 0;
}

void World::beginAt(IHeightSource* src, uint32_t seed, float x, float y, float aglStart, float headingDeg,
                    float windFromDeg, float windSpeed, float timeOfDay) {
  cache_.attach(src);
  cache_.fill(x, y);
  float gh = 0;
  cache_.height(x, y, &gh);
  air_.setSeed(seed);
  air_.setField(&cache_);
  air_.setWind(windFromDeg, windSpeed, gh + aglStart);
  windFrom_ = windFromDeg;
  t_ = timeOfDay;
  place(x, y, aglStart, headingDeg);
  air_.sample(g_.x, g_.y, g_.z, t_, &s_);
}

void World::begin(IHeightSource* src, uint32_t seed, const Takeoff& to, float windSpeed) {
  // The takeoff is the origin of the local frame; fly into the wind, 3 minutes into the sim day so thermals
  // are already working.
  beginAt(src, seed, 0.0f, 0.0f, 30.0f, to.windFromDeg, to.windFromDeg, windSpeed, 180.0f);
}

void World::step(const Controls& c, float dt) {
  if (status_ != Status::FLYING || !(dt > 0)) return;
  cache_.setCentre(g_.x, g_.y);
  cache_.refresh(48);
  air_.sample(g_.x, g_.y, g_.z, t_, &s_);
  const float px = g_.x, py = g_.y;
  g_.step(c, s_.wx, s_.wy, s_.wz, dt);
  t_ += dt;
  dist_ += sqrtf((g_.x - px) * (g_.x - px) + (g_.y - py) * (g_.y - py));
  vario_ += (g_.vz - vario_) * lagFactor(dt, 0.8f);
  if (g_.z > maxAlt_) maxAlt_ = g_.z;

  float gh;
  if (cache_.height(g_.x, g_.y, &gh)) {
    ground_ = gh;
    agl_ = g_.z - gh;
    if (g_.z <= gh + 0.3f) {
      float sx = 0, sy = 0;
      cache_.gradient(g_.x, g_.y, 100.0f, &sx, &sy);
      const float slope = sqrtf(sx * sx + sy * sy);
      status_ = (-g_.vz <= kTouchSpeed && slope <= kMaxLandSlope) ? Status::LANDED : Status::CRASHED;
      g_.z = gh;
      agl_ = 0;
      g_.vz = 0;
    }
  } else {
    ground_ = NAN;
    agl_ = NAN;
  }

  trailT_ += dt;
  if (trailT_ >= 1.0f) {
    trailT_ -= 1.0f;
    trail_[trailHead_] = TrailPt{g_.x, g_.y, g_.z};
    trailHead_ = (trailHead_ + 1) % kTrailMax;
    if (trailN_ < kTrailMax) trailN_++;
  }
}

}  // namespace sim
