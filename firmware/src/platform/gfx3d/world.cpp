#include "platform/gfx3d/world.h"

namespace gfx3d {

void World::place(float x, float y, float aglStart, float headingDeg) {
  float gh = 0;
  if (!cache_.height(x, y, &gh)) gh = 0;
  g_.reset(x, y, gh + aglStart, headingDeg * kDeg2Rad);
  status_ = Status::FLYING;
  vario_ = 0;
  kick_ = 0;
  ground_ = gh;
  agl_ = aglStart;
  maxAlt_ = startAlt_ = g_.z;
  dist_ = 0;
  startX_ = x; startY_ = y; startT_ = t_;
  trailN_ = trailHead_ = 0;
  trailT_ = 0;
}

void World::beginAt(IHeightSource* src, uint32_t seed, float x, float y, float aglStart, float headingDeg,
                    float windFromDeg, float windSpeed, float timeOfDay) {
  cache_.attach(src);
  if (progressive_) cache_.fillNear(x, y, kNearCells);
  else cache_.fill(x, y);
  float gh = 0;
  cache_.height(x, y, &gh);
  air_.setLevel(level_);
  air_.setSeed(seed);
  air_.setField(&cache_);
  air_.setWind(windFromDeg, windSpeed, gh + aglStart);
  air_.setHome(x, y);
  windFrom_ = windFromDeg;
  windRef_ = gh + aglStart;
  {
    const float to = (windFromDeg + 180.0f) * kDeg2Rad, sp = windSpeed < 0 ? 0 : windSpeed;
    curE_ = tgtE_ = sinf(to) * sp;
    curN_ = tgtN_ = cosf(to) * sp;
    tgtSpeed_ = sp;
  }
  t_ = timeOfDay;
  place(x, y, aglStart, headingDeg);
  air_.sample(g_.x, g_.y, g_.z, t_, &s_);
}

void World::usualWind(IHeightSource* src, const Takeoff& to, float* fromDeg, float* speed) {
  // The wind blows onto the slope the takeoff is on: the direction comes from the terrain model (the way the slope faces,
  // 600 m scale) when it is clear, else from the table.
  float from = to.windFromDeg;
  float h0, a, b, c, d;
  if (src->at(0, 0, &h0) && src->at(600, 0, &a) && src->at(-600, 0, &b) && src->at(0, 600, &c) && src->at(0, -600, &d)) {
    const float gx = (a - b) / 1200.0f, gy = (c - d) / 1200.0f;
    if (sqrtf(gx * gx + gy * gy) > 0.12f) from = atan2f(-gx, -gy) * kRad2Deg;     // downhill side: the wind comes up the slope
    if (from < 0) from += 360.0f;
  }
  *fromDeg = from;
  *speed = 5.0f;
}

void World::begin(IHeightSource* src, uint32_t seed, const Takeoff& to, float windSpeed, float timeOfDay, float windFromOverride) {
  // The takeoff is the origin of the local frame; fly into the wind, 3 minutes into the sim day so thermals are already working.
  float from, speed;
  usualWind(src, to, &from, &speed);
  if (windFromOverride >= 0) from = windFromOverride;
  if (windSpeed >= 0) speed = windSpeed;                  // explicit (0 = calm); negative = the site's usual
  beginAt(src, seed, 0.0f, 0.0f, kLaunchAgl, from, from, speed, timeOfDay);
}

void World::setWindTarget(float fromDeg, float speed) {
  const float to = (fromDeg + 180.0f) * kDeg2Rad, sp = speed < 0 ? 0 : speed;
  tgtE_ = sinf(to) * sp;
  tgtN_ = cosf(to) * sp;
  tgtSpeed_ = sp;
  windFrom_ = fromDeg;
}

void World::step(const Controls& c, float dt, int cacheBudget) {
  if (status_ != Status::FLYING || !(dt > 0)) return;
  if (curE_ != tgtE_ || curN_ != tgtN_) {                // ease the wind toward its target
    const float dx = tgtE_ - curE_, dy = tgtN_ - curN_, d = sqrtf(dx * dx + dy * dy), st = 0.6f * dt;
    if (d <= st) { curE_ = tgtE_; curN_ = tgtN_; }
    else { curE_ += dx / d * st; curN_ += dy / d * st; }
    air_.setWind(atan2f(-curE_, -curN_) * kRad2Deg, sqrtf(curE_ * curE_ + curN_ * curN_), windRef_);
  }
  cache_.setCentre(g_.x, g_.y);
  if (cache_.warming()) {                                // a progressive start: the window fills in blocks, refresh() waits for it
    if (cacheBudget > 0) cache_.warmStep(kWarmBudget);
  } else {
    cache_.refresh(cacheBudget);
  }
  const uint32_t ta = clk_ ? clk_() : 0;
  air_.sample(g_.x, g_.y, g_.z, t_, &s_);
  const uint32_t tg = clk_ ? clk_() : 0;
  const float px = g_.x, py = g_.y;
  // Tip lift: lift difference between the right and left wing tip along the wing, from the thermal's gradient.
  const float rx = cosf(g_.heading), ry = -sinf(g_.heading);
  const float dw = (s_.gx * rx + s_.gy * ry) * kSpan;
  kick_ += (clampf(-dw * kKickPerMs, -kKickMax, kKickMax) - kick_) * lagFactor(dt, 0.5f);
  g_.step(c, s_.wx, s_.wy, s_.wz, dt, kick_);
  if (clk_) {
    const uint32_t te = clk_();
    prof_.airUs += tg - ta; prof_.gliderUs += te - tg; prof_.n++;
    if (tg - ta > prof_.airMax) prof_.airMax = tg - ta;
  }
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
    trail_[trailHead_] = TrailPt{g_.x, g_.y};
    trailHead_ = (trailHead_ + 1) % kTrailMax;
    if (trailN_ < kTrailMax) trailN_++;
  }
}

}  // namespace gfx3d
