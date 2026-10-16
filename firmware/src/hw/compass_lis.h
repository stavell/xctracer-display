#pragma once
#include "app/ports.h"

// LIS3MDL magnetometer + LSM6DS33 accelerometer and gyro on the Feather Sense. Powered down unless enabled.
// Readings are mapped to the BOARD frame by the toBoard* functions in the .cpp (per-chip axes, fixed by
// the PCB); the board -> case rotation is applied by the app (core/mount.h).
class CompassLis : public ICompass {
 public:
  bool begin();                                     // after Wire is available; false if a sensor is missing
  void enable(bool on) override;
  bool read(Vec3& accel, Vec3& mag, Vec3& gyro, bool& haveGyro) override;   // true on each new magnetometer sample (80 Hz)
  const Vec3& lastAccel() const { return a_; }      // last mapped readings, for diagnostics
  const Vec3& lastMag() const { return m_; }
  const Vec3& lastGyro() const { return g_; }
  bool present() const { return haveImu_ && haveMag_; }

 private:
  bool haveImu_ = false, haveMag_ = false, on_ = false;
  uint32_t gyroFrom_ = 0;
  Vec3 a_, m_, g_;
};
