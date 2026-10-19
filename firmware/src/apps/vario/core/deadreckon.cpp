#include "apps/vario/core/deadreckon.h"
#include <math.h>

static const float kDeg2Rad = 0.017453292f;
static const double kMPerDeg = 111194.9266;           // 6371 km * pi / 180

void DeadReckoner::setWind(bool valid, float eastMs, float northMs) {
  wx_ = valid && isfinite(eastMs) && isfinite(northMs) ? eastMs : 0;
  wy_ = valid && isfinite(eastMs) && isfinite(northMs) ? northMs : 0;
}

void DeadReckoner::reset() {
  haveFix_ = false;
  haveYaw_ = false;
  n_ = head_ = 0;
}

// Turn the air velocity by rate*dt (clockwise), moving along the arc: half the turn, move, half the turn.
void DeadReckoner::advance(State& s, uint32_t t, float rateDps, float wx, float wy) {
  const float dt = (float)(int32_t)(t - s.t) * 0.001f;
  if (dt <= 0) return;
  const float h = 0.5f * rateDps * kDeg2Rad * dt;
  const float c = cosf(h), sn = sinf(h);
  float e = s.ax * c + s.ay * sn, n = -s.ax * sn + s.ay * c;      // clockwise rotation of an (east, north) vector
  s.px += (e + wx) * dt;
  s.py += (n + wy) * dt;
  s.ax = e * c + n * sn;
  s.ay = -e * sn + n * c;
  s.t = t;
}

void DeadReckoner::yaw(uint32_t tMs, float rateDps) {
  if (!isfinite(rateDps)) return;
  if (haveYaw_ && (int32_t)(tMs - lastYawMs_) <= 0) return;
  hist_[head_] = {tMs, rateDps};
  head_ = (head_ + 1) % kHistory;
  if (n_ < kHistory) n_++;
  lastRate_ = rateDps;
  lastYawMs_ = tMs;
  haveYaw_ = true;
  if (haveFix_ && (int32_t)(tMs - s_.t) > 0) advance(s_, tMs, rateDps, wx_, wy_);
}

void DeadReckoner::fix(uint32_t rxMs, bool haveFix, double lat, double lon, float speedKmh, float courseDeg) {
  if (!haveFix || !isfinite(courseDeg) || !isfinite(speedKmh) || !isfinite(lat) || !isfinite(lon)) { haveFix_ = false; return; }
  haveFix_ = true;
  lat0_ = lat;
  lon0_ = lon;
  fixRx_ = rxMs;
  const uint32_t tFix = rxMs - (uint32_t)(latencyMs_ + 0.5f);     // the instant the fix describes
  const float v = speedKmh / 3.6f, th = courseDeg * kDeg2Rad;
  s_.t = tFix;
  s_.px = s_.py = 0;
  s_.ax = v * sinf(th) - wx_;                                      // air velocity = ground velocity - wind
  s_.ay = v * cosf(th) - wy_;
  // Carry it through the stored samples after tFix: each sample's rate holds for the time up to it.
  for (int i = 0; i < n_; i++) {
    const YawSample& h = hist_[(head_ - n_ + i + kHistory) % kHistory];
    if ((int32_t)(h.t - s_.t) > 0) advance(s_, h.t, h.rate, wx_, wy_);
  }
}

DeadReckoner::Estimate DeadReckoner::at(uint32_t nowMs) const {
  Estimate r;
  if (!haveFix_ || nowMs - fixRx_ > maxAgeMs_) return r;
  State s = s_;
  const bool live = haveYaw_ && nowMs - lastYawMs_ <= 500;
  if ((int32_t)(nowMs - s.t) > 0) advance(s, nowMs, live ? lastRate_ : 0.0f, wx_, wy_);
  const float e = s.ax + wx_, n = s.ay + wy_;
  float c = atan2f(e, n) / kDeg2Rad;
  if (c < 0) c += 360.0f;
  r.valid = true;
  r.gyroLive = live;
  r.courseDeg = c;
  r.speedKmh = hypotf(e, n) * 3.6f;
  r.eastM = s.px;
  r.northM = s.py;
  r.lat = lat0_ + s.py / kMPerDeg;
  r.lon = lon0_ + s.px / (kMPerDeg * cos(lat0_ * 0.017453292519943295));
  return r;
}
