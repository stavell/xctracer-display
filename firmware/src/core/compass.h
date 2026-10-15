#pragma once
#include "core/types.h"

// Tilt-compensated magnetic compass from an accelerometer and a magnetometer, plus the calibration
// routine. Pure maths: readings come in the device frame (x right of the screen, y top of the screen,
// z out of the screen), so the hardware layer owns the sensor-to-device axis mapping.
// The heading is the compass direction the TOP OF THE SCREEN points to (0 = magnetic north).
class Compass {
 public:
  void reset();
  void setCalibration(const MagCal& c) { cal_ = c; }
  const MagCal& calibration() const { return cal_; }

  // accel: m/s^2 as the sensor reports it (+g along the axis pointing up when at rest); mag: microtesla.
  void update(const Vec3& accel, const Vec3& mag);
  bool valid() const { return valid_; }
  float heading() const { return heading_; }    // degrees, 0..360
  float fieldUt() const { return field_; }

  // calibration: turn the unit through all directions, then finish(true) to keep the result. The
  // offset is a least-squares sphere fit, which tolerates incomplete coverage far better than min/max.
  void startCalibration();
  bool calibrating() const { return calibrating_; }
  void calibrationSpan(float out[3]) const;       // microtesla covered so far on each axis
  bool finishCalibration(bool keep);              // false if kept but too little movement to be useful

 private:
  MagCal cal_;
  bool calibrating_ = false;
  float mn_[3], mx_[3];                 // extremes seen, only to show progress
  double ata_[4][4], atb_[4];           // normal equations of the sphere fit
  int nCal_ = 0;
  bool seeded_ = false, valid_ = false;
  Vec3 a_, m_;
  float heading_ = 0, field_ = 0;
};
