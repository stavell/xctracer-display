#include "core/heading_smoother.h"
#include <math.h>

static float wrap360(float d) {
  d = fmodf(d, 360.0f);
  if (d < 0) d += 360.0f;
  if (d >= 360.0f) d = 0;       // a tiny negative can round up to 360
  return d;
}

static float shortestDiff(float to, float from) {    // (-180, 180]
  float d = wrap360(to - from);
  return d > 180.0f ? d - 360.0f : d;
}

float HeadingSmoother::update(float targetDeg, uint32_t nowMs) {
  if (!isfinite(targetDeg)) { reset(); return out_; }
  float t = wrap360(targetDeg);
  uint32_t dt = nowMs - lastMs_;                      // unsigned: correct across the millis() wrap
  bool snap = !isfinite(out_) || dt > GAP_MS;
  lastMs_ = nowMs;
  if (snap) { target_ = t; driftSign_ = 0; out_ = t; return out_; }
  if (dead_ > 0 && isfinite(target_)) {
    float e = shortestDiff(t, target_);
    int sign = e > DRIFT_DEG ? 1 : (e < -DRIFT_DEG ? -1 : 0);
    if (sign != driftSign_) { driftSign_ = sign; driftSince_ = nowMs; }
    if (fabsf(e) > dead_ || (sign != 0 && nowMs - driftSince_ > holdMs_)) { target_ = t; driftSign_ = 0; }
  } else {
    target_ = t;
  }
  t = target_;
  float d = shortestDiff(t, out_);
  float settle = fmaxf(SETTLE_DEG, 2.0f * dead_);    // with a deadband, a sub-degree retarget is not worth a glide
  if (fabsf(d) < settle) { out_ = t; return out_; }
  float alpha = tau_ > 0 ? 1.0f - expf(-(float)dt / tau_) : 1.0f;
  out_ = wrap360(out_ + d * alpha);
  if (fabsf(shortestDiff(t, out_)) < settle) out_ = t;
  return out_;
}

bool HeadingSmoother::settled() const {
  if (!isfinite(out_)) return true;
  return out_ == target_;
}

void HeadingSmoother::reset() {
  out_ = NAN;
  target_ = NAN;
}

void HeadingSmoother::setDeadband(float deg, uint32_t holdMs) {
  dead_ = deg;
  holdMs_ = holdMs;
  driftSign_ = 0;
}
