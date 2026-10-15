#pragma once
#include "app/ports.h"

// LIS3MDL magnetometer + LSM6DS33 accelerometer on the Feather Sense. Powered down unless enabled.
// Readings are mapped to the device frame (x right of the screen, y top of the screen, z out of the
// screen) by the two toDevice* functions in the .cpp: that mapping depends on how the board sits in
// the case.
class CompassLis : public ICompass {
 public:
  bool begin();                                     // after Wire is available; false if a sensor is missing
  void enable(bool on) override;
  bool read(Vec3& accel, Vec3& mag) override;       // at most 10 Hz
  const Vec3& lastAccel() const { return a_; }      // last mapped readings, for diagnostics
  const Vec3& lastMag() const { return m_; }
  bool present() const { return haveImu_ && haveMag_; }

 private:
  bool haveImu_ = false, haveMag_ = false, on_ = false;
  uint32_t last_ = 0;
  Vec3 a_, m_;
};
