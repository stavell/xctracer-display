#include "apps/vario/core/heading.h"

static float norm360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }

void HeadingSelector::feedGps(bool usable, float speedKmh, float courseDeg) {
  bool ok = usable && isfinite(speedKmh) && isfinite(courseDeg);
  float spd = ok ? speedKmh : -1.0f;
  if (gps_) { if (spd < off_) gps_ = false; }
  else if (spd >= on_) gps_ = true;
  course_ = ok ? norm360(courseDeg) : NAN;
}

void HeadingSelector::feedMag(bool valid, float headingDeg) {
  magValid_ = valid && isfinite(headingDeg);
  mag_ = magValid_ ? norm360(headingDeg) : NAN;
}

Heading HeadingSelector::current() const {
  Heading h;
  if (gps_ && isfinite(course_)) { h.deg = course_; h.src = HDG_GPS; }
  else if (magValid_) { h.deg = mag_; h.src = HDG_MAG; }
  return h;
}
