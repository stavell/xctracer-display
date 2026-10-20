#pragma once
// The motion service: the box's orientation sensors as one platform service, so an app (the vario, the compass,
// the calibration, a game) asks for a heading or a tilt without owning any sensor logic. It takes the sensors of the
// ICompass port (board frame), applies the case mounting and the magnetometer calibration stored in BoxSettings,
// feeds the tilt-compensated compass (platform/sensors/compass.h) and the gyro fusion (ahrs.h), and decides which of
// the two is the heading. Pure code: time is passed in, the sensors are switched on only between start() and stop()
// (nobody asking = no current drawn, and no sensor read behind another app's back).
//
// Use: start() when the app opens, poll(now) on every tick (reads at most one sample), stop() when it closes. One
// owner at a time: each user (the vario App, the compass app, the calibration) holds its own service object, which is
// small (about 0.7 kB) and lives in the owner's arena slot; the sensor chips are shared only in time.
//
// Frames: device frame = x right of the screen, y top of the screen, z out of the screen. heading = the compass
// direction the TOP OF THE SCREEN points to, degrees 0..360, 0 = magnetic north.
#include <stdint.h>
#include "platform/ports.h"
#include "platform/sensors/ahrs.h"
#include "platform/sensors/compass.h"
#include "platform/settings/box_settings.h"

class MotionService {
 public:
  MotionService(ICompass& hw, BoxSettings& settings) : hw_(hw), settings_(settings) {}

  void loadSettings();                       // calibration and mounting from BoxSettings (start() does it too)
  void start();                              // sensors on; a no-op when running
  void stop();                               // sensors off, filters reset (the calibration stays loaded)
  bool running() const { return on_; }
  // Read the sensors once (the port delivers one sample at a time) and update the filters. True when a new sample
  // arrived: accel() / mag() / gyro() are then that sample (device frame, after the mounting).
  bool poll(uint32_t nowMs);

  // Settings changed under a running service: the mounting (ctl.py orient) or new calibration offsets (a calibration
  // finished). Both re-seed the filters.
  void remount();
  void recalibrated();

  // ---- outputs ----
  bool fused() const { return fused_; }                          // the heading comes from the gyro fusion
  bool valid() const { return on_ && (fused_ || compass_.valid()); }   // heading() means something
  float heading() const { return fused_ ? ahrs_.heading() : compass_.heading(); }
  bool quaternionValid() const { return fused_; }                // quaternion() is the fusion's attitude
  void quaternion(float out[4]) const { ahrs_.quaternion(out); } // w x y z, device -> world (east, north, up)
  // Tilt in degrees from the gravity direction (the fusion's, else the accelerometer's), 0 / 0 = lying flat, face up:
  // pitch = top edge raised positive, roll = right edge down positive. (The sim's own AccelTilt uses the box held upright as
  // zero, a different convention.) False while no sample defines a direction of gravity.
  bool tilt(float* rollDeg, float* pitchDeg) const;
  const Vec3& accel() const { return a_; }                       // m/s^2, +g along the axis pointing up
  const Vec3& mag() const { return m_; }                         // uT, raw (hard-iron not removed)
  const Vec3& gyro() const { return g_; }                        // rad/s, valid when gyroSample()
  bool gyroSample() const { return gyroSample_; }                // the last sample had a gyro reading
  float trust() const { return ahrs_.trust(); }                  // 1 = clean field, 0 = field rejected
  bool gyroFailed() const { return ahrs_.gyroFailed(); }
  bool calibrated() const { return compass_.calibration().valid; }
  float fieldUt() const { return compass_.fieldUt(); }           // field strength, microtesla
  // 0..1: the fusion's trust when fused, else how close the field is to the calibrated radius (0 at 25 % off).
  float fieldQuality() const;
  int mountIndex() const { return mount_; }
  const Compass& compass() const { return compass_; }
  const Ahrs& ahrs() const { return ahrs_; }

 private:
  ICompass& hw_;
  BoxSettings& settings_;
  Compass compass_;
  Ahrs ahrs_;
  int mount_ = 0;
  bool on_ = false, fused_ = false, haveGyro_ = false, gyroSample_ = false;
  uint32_t gyroMs_ = 0;
  Vec3 a_, m_, g_;
};
