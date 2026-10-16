#include "apps/vario/core/map_motion.h"
#include <math.h>

static const double D2R = 0.017453292519943295;

static int32_t sdiff(uint32_t a, uint32_t b) { return (int32_t)(a - b); }   // wrap-safe a - b

static float lerpAngle(float a, float b, float f) {      // along the shortest arc, degrees
  float d = fmodf(b - a, 360.0f);
  if (d > 180.0f) d -= 360.0f;
  if (d < -180.0f) d += 360.0f;
  float r = fmodf(a + d * f, 360.0f);
  return r < 0 ? r + 360.0f : r;
}

void MapMotion::reset() {
  n_ = 0;
  originValid_ = false;
  interval_ = 333;
  tdValid_ = false;
  haveNow_ = false;
  na_ = 0;
  rate_ = 1.0f;
  haveRef_ = false;
}

void MapMotion::clearRing() {
  n_ = 0;
  tdValid_ = false;
}

uint32_t MapMotion::delayMs() const { return interval_ + MARGIN_MS; }

void MapMotion::feed(uint32_t arrMs, const Sample& s, bool newSecond, uint32_t flightClockMs) {
  if (!s.fix || !isfinite(s.lat) || !isfinite(s.lon)) {      // no position: nothing to interpolate (the trail is empty too)
    clearRing();
    if (newSecond) originValid_ = false;
    return;
  }
  if (n_ > 0) {
    uint32_t last = ring_[n_ - 1].t;
    uint32_t gap = arrMs - last;
    if (sdiff(arrMs, last) <= 0) { arrMs = last + 1; gap = 1; }        // two lines in one tick: keep time strictly increasing
    double dx = (s.lon - ring_[n_ - 1].lon) * kx_, dy = (s.lat - ring_[n_ - 1].lat) * 111320.0;
    if (gap > GAP_MS || hypot(dx, dy) > TELEPORT_M) clearRing();
    else interval_ = (uint32_t)(0.75f * interval_ + 0.25f * (float)(gap < 60 ? 60 : gap));
  }
  if (!haveRef_) { ref_ = {arrMs, s.lat, s.lon, s.course}; kxRef_ = 111320.0 * cos(s.lat * D2R); haveRef_ = true; }
  if (n_ == N) { for (int i = 1; i < N; i++) ring_[i - 1] = ring_[i]; n_--; }
  ring_[n_++] = {arrMs, s.lat, s.lon, s.course};
  if (newSecond) {
    origin_ = ring_[n_ - 1];
    originValid_ = true;
    kx_ = 111320.0 * cos(s.lat * D2R);
    if (na_ == 8) { for (int i = 1; i < 8; i++) anchors_[i - 1] = anchors_[i]; na_--; }
    anchors_[na_++] = {arrMs, flightClockMs};
    if (na_ >= 2 && sdiff(arrMs, anchors_[0].arr) > 1500) {
      float r = (float)(flightClockMs - anchors_[0].clock) / (float)(arrMs - anchors_[0].arr);
      rate_ = fminf(fmaxf(r, 0.25f), 16.0f);
    }
  }
}

// Ground position at arrival time t (clamped to the ring), metres east/north of the origin sample.
void MapMotion::ground(uint32_t t, double& lat, double& lon, float* course) const {
  int k = 0;
  if (sdiff(t, ring_[0].t) <= 0) k = -1;
  else if (sdiff(t, ring_[n_ - 1].t) >= 0) k = n_ - 1;
  else while (sdiff(ring_[k + 1].t, t) <= 0) k++;      // ring_[k].t <= t < ring_[k+1].t after the loop
  float crs;
  if (k < 0) { lat = ring_[0].lat; lon = ring_[0].lon; crs = ring_[0].course; }
  else if (k == n_ - 1) { lat = ring_[k].lat; lon = ring_[k].lon; crs = ring_[k].course; }
  else {
    float f = (float)sdiff(t, ring_[k].t) / (float)sdiff(ring_[k + 1].t, ring_[k].t);
    lat = ring_[k].lat + (ring_[k + 1].lat - ring_[k].lat) * f;
    lon = ring_[k].lon + (ring_[k + 1].lon - ring_[k].lon) * f;
    crs = (isfinite(ring_[k].course) && isfinite(ring_[k + 1].course)) ? lerpAngle(ring_[k].course, ring_[k + 1].course, f) : ring_[k].course;
  }
  if (course) *course = crs;
}

MapView MapMotion::view(uint32_t nowMs, float windE, float windN, int maxDrop) {
  MapView v;
  if (!originValid_ || n_ == 0) return v;
  uint32_t dt = haveNow_ ? nowMs - lastNow_ : 0;
  if (dt > 5000) dt = 5000;
  lastNow_ = nowMs;
  haveNow_ = true;
  const uint32_t newest = ring_[n_ - 1].t;
  if (!tdValid_) {
    td_ = newest;                                   // snap: start at the newest sample
    tdValid_ = true;
  } else {
    // Follow nowMs - delay at wall speed; when behind (after a pause) catch up at up to twice that, when
    // ahead (the delay grew) wait. Never past the newest sample: with no new data the view holds.
    uint32_t target = nowMs - delayMs();
    int32_t lag = sdiff(target, td_);
    if (lag > 3000 || lag < -3000) td_ = sdiff(target, newest) > 0 ? newest : target;
    else {
      int32_t step = (int32_t)dt;
      if (lag > 0) step += lag < (int32_t)dt ? lag : (int32_t)dt;
      else step = (int32_t)dt + lag > 0 ? (int32_t)dt + lag : 0;
      td_ += (uint32_t)step;
    }
    if (sdiff(td_, newest) > 0) td_ = newest;
    if (sdiff(td_, ring_[0].t) < 0) td_ = ring_[0].t;
  }

  // data seconds elapsed since the origin sample, at the measured data/wall ratio
  auto air = [&](uint32_t t, double& x, double& y, float* crs) {
    double lat, lon;
    ground(t, lat, lon, crs);
    x = (lon - origin_.lon) * kx_;
    y = (lat - origin_.lat) * 111320.0;
    double sec = (double)sdiff(t, origin_.t) / 1000.0 * rate_;
    x -= windE * sec;
    y -= windN * sec;
  };
  double x, y;
  float crs = NAN;
  air(td_, x, y, &crs);
  v.valid = true;
  v.cx = (float)x;
  v.cy = (float)y;
  {
    double lat, lon;
    ground(td_, lat, lon, nullptr);
    double sec = (double)sdiff(td_, ref_.t) / 1000.0 * rate_;
    v.ax = (float)((lon - ref_.lon) * kxRef_ - windE * sec);
    v.ay = (float)((lat - ref_.lat) * 111320.0 - windN * sec);
  }

  uint32_t win = (uint32_t)(HDG_WINDOW_MS / rate_);
  double x0, y0;
  air(td_ - win, x0, y0, nullptr);
  float dx = (float)(x - x0), dy = (float)(y - y0);
  if (hypotf(dx, dy) > HDG_MIN_M) v.hdgRad = atan2f(dx, dy);
  else if (isfinite(crs)) v.hdgRad = crs * (float)D2R;

  // trail points (one per data second back from the origin) that are newer than the display time
  const uint32_t spacing = (uint32_t)(1000.0f / rate_ + 0.5f);
  uint32_t tp = origin_.t;
  while (v.drop < maxDrop && sdiff(tp, td_) > 0) { v.drop++; tp -= spacing; }
  return v;
}
