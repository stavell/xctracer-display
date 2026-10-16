#pragma once
#include <stdint.h>
#include "core/types.h"

// Attitude and heading from a gyro, an accelerometer and a (hard-iron corrected) magnetometer, all in
// the device frame (x right of the screen, y top of the screen, z out of the screen), on a quaternion.
//
// Why Mahony and not Madgwick: both are complementary filters with the same cost class, but Mahony's
// correction is a plain PI controller on a rotation-vector error. That lets us (a) apply the magnetic
// correction ONLY about the vertical axis, computed from the horizontal field direction, so a disturbed
// or noisy magnetometer can never tilt the attitude (Madgwick's gradient step mixes the two), (b) get the
// gyro bias for free from the integral term, and (c) set the gains as time constants. Pure float maths.
//
// The gyro does the fast part. Gravity (accel) pulls roll and pitch back with a time constant of about
// 1 s, the horizontal magnetic field pulls the heading back with about 10 s (the gyro bias is learned, so
// the field only has to remove slow drift; faster would pass its noise on to the dial). A still box also learns
// the gyro bias directly. See docs/MATH.md section 8.
class Ahrs {
 public:
  static constexpr float KP_ACC = 1.0f;           // 1/s: gravity -> roll/pitch (tau 1 s)
  static constexpr float KI_ACC = 0.05f;          // 1/s^2: gyro bias from the gravity error
  static constexpr float KP_MAG = 0.1f;           // 1/s: field -> heading (tau 10 s); the gyro carries the short term
  static constexpr float KI_MAG = 0.0025f;        // 1/s^2: KP^2/4, critically damped bias learning
  static constexpr float TRUST_FULL = 0.05f;      // field magnitude / dip deviation (relative) up to which the field has full weight
  static constexpr float BOOST_MS = 2000.0f;      // high gains after a (re)start, and after each of the first bias estimates, fading out over this time
  static constexpr float BOOST_GAIN = 8.0f;       // gravity KP multiplier at the start
  static constexpr float BOOST_GAIN_MAG = 40.0f;  // heading KP multiplier at the start (4 /s): washes out the seed's noise within the boost time
  static constexpr float G = 9.81f;
  static constexpr float G_TOL = 0.12f;           // accel magnitude within +-12% of g counts for gravity
  static constexpr float FIELD_TOL = 0.20f;       // field magnitude / dip within +-20% of the running reference
  static constexpr float MIN_HORIZ_UT = 8.0f;     // weaker horizontal field gives no usable heading
  static constexpr float BIAS_MAX = 0.21f;        // rad/s (12 deg/s), the LSM6DS33 zero-rate offset is within +-10
  static constexpr uint32_t GAP_MS = 500;         // a longer pause between samples re-seeds
  static constexpr float MAX_DT = 0.1f;           // s
  static constexpr uint32_t WINDOW_MS = 500;      // still-detection window
  static constexpr float STILL_GYRO_STD = 0.0105f;   // rad/s (0.6 deg/s) rms about the window mean
  static constexpr float STILL_ACC_DEG = 0.4f;    // gravity direction change between windows
  static constexpr float STILL_MAG_DEG = 0.8f;    // field direction change between windows
  static constexpr uint32_t ACC_LOST_MS = 10000;  // heading invalid without gravity updates for this long
  static constexpr uint32_t MAG_LOST_MS = 30000;  // ... or without field updates for this long
  static constexpr int STUCK_SAMPLES = 80;        // identical gyro readings in a row = a dead gyro

  void reset();
  // gyro rad/s, accel m/s^2 (+g along the axis pointing up at rest), mag uT (hard-iron corrected). nowMs
  // is the time of the sample; dt comes from it. Samples with non-finite values are ignored.
  void update(const Vec3& gyro, const Vec3& accel, const Vec3& mag, uint32_t nowMs);

  bool valid() const { return valid_; }
  float heading() const { return heading_; }      // degrees 0..360, azimuth of the top of the screen
  bool gyroFailed() const { return stuck_; }      // the gyro output froze: the caller should fall back
  Vec3 bias() const { return bias_; }             // rad/s
  float biasDps() const;                          // magnitude, deg/s (diagnostics)
  Vec3 up() const;                                // predicted gravity direction (unit, device frame)
  bool still() const { return stillWin_ >= 1; }   // box has been still for about a second
  struct Debug { float trust, hxy, magRef, dipRef, dip, gstdDps; int magOk, stillWin, biasWins, winBad; };
  Debug debug() const { return dbg_; }            // last values, for the diagnostics status line

 private:
  bool seed(const Vec3& a, const Vec3& m, uint32_t nowMs);
  void rotationMatrix(float r[3][3]) const;
  void windowEnd(uint32_t nowMs);

  bool seeded_ = false, valid_ = false, stuck_ = false;
  float q_[4] = {1, 0, 0, 0};                     // w, x, y, z: device -> world (east, north, up)
  Vec3 bias_;
  float heading_ = 0;
  uint32_t lastMs_ = 0, seedMs_ = 0, boostMs_ = 0, accMs_ = 0, magMs_ = 0, magRejectMs_ = 0;
  float magRef_ = 0, dipRef_ = 0;                 // running field magnitude and vertical share
  Vec3 lastGyro_;
  int sameGyro_ = 0;
  // still detection
  uint32_t winStart_ = 0;
  int winN_ = 0;
  bool winBad_ = false;
  double sumG_[3], sumG2_ = 0, sumA_[3], sumM_[3];
  Vec3 prevA_, prevM_;
  bool havePrev_ = false;
  int stillWin_ = 0, biasWins_ = 0;
  Debug dbg_ = {};
};
