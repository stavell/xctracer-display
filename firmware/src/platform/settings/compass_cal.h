#pragma once
// Magnetometer calibration as a reusable flow: start, feed it readings while the unit is turned through every
// direction, finish with keep or cancel. The result is stored in BoxSettings, where every app (the vario, the
// compass tool) picks it up when it starts. Pure maths over platform/sensors/compass.h.
//
// Who feeds it: whoever reads the sensors. The Settings app reads them itself; the vario feeds the readings it
// already takes when the laptop starts a calibration (`ctl.py cal start`), so the sensor is never read twice.
#include <stdint.h>
#include "platform/sensors/compass.h"
#include "platform/settings/box_settings.h"

class CompassCal {
 public:
  explicit CompassCal(BoxSettings& settings) : settings_(settings) {}

  void start();                                         // begins the fit; the stored calibration shows the field while turning
  bool active() const { return active_; }
  // Device-frame readings (after the mounting rotation): accelerometer m/s^2, magnetometer uT.
  void feed(const Vec3& accel, const Vec3& mag, uint32_t nowMs) { if (active_) fit_.update(accel, mag, nowMs); }
  // keep: store the result when the movement was enough to fit a sphere. Returns true when a calibration was stored.
  bool finish(bool keep);

  void span(float out[3]) const {                        // uT covered on each axis so far (zeros when not calibrating)
    if (active_) fit_.calibrationSpan(out);
    else out[0] = out[1] = out[2] = 0;
  }
  float fieldUt() const { return fit_.fieldUt(); }
  bool stored() const { MagCal c; return settings_.loadMagCal(c); }

 private:
  BoxSettings& settings_;
  Compass fit_;
  bool active_ = false;
};
