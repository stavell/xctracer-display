#pragma once
#include "apps/vario/core/types.h"

enum HeadingSource { HDG_NONE, HDG_GPS, HDG_MAG };

struct Heading {
  float deg = NAN;                  // 0..360, NaN when there is no heading
  HeadingSource src = HDG_NONE;
};

// One heading with two feeds. The GPS track is preferred once you are moving (a track is only meaningful
// above a few km/h); otherwise the magnetic compass is used. Hysteresis keeps it from flapping around
// the threshold. Pure logic: it knows nothing about sensors or screens.
class HeadingSelector {
 public:
  void configure(float onKmh, float offKmh) { on_ = onKmh; off_ = offKmh; }

  // usable = the data is fresh and has a GPS fix
  void feedGps(bool usable, float speedKmh, float courseDeg);
  void feedMag(bool valid, float headingDeg);

  bool gpsActive() const { return gps_; }
  bool wantsMagnetometer() const { return !gps_; }   // power the sensors only when they matter
  Heading current() const;

 private:
  float on_ = 4.0f, off_ = 2.5f;
  bool gps_ = false;
  float course_ = NAN;
  bool magValid_ = false;
  float mag_ = NAN;
};
