#include "platform/sensors/motion.h"
#include <math.h>
#include "platform/sensors/mount.h"

void MotionService::loadSettings() {
  MagCal cal;
  if (settings_.loadMagCal(cal)) compass_.setCalibration(cal);
  mount_ = settings_.mountIndex();
  if (!mountValid(mount_)) mount_ = 0;
}

void MotionService::start() {
  if (on_) return;
  loadSettings();
  on_ = true;
  hw_.enable(true);
}

void MotionService::stop() {
  if (on_) { hw_.enable(false); on_ = false; }
  compass_.reset();                  // keeps the calibration
  ahrs_.reset();
  haveGyro_ = false;
  fused_ = false;
  gyroSample_ = false;
}

void MotionService::remount() {
  loadSettings();
  compass_.reset();
  ahrs_.reset();
}

void MotionService::recalibrated() {
  MagCal cal;
  if (settings_.loadMagCal(cal)) compass_.setCalibration(cal);
  ahrs_.reset();
}

bool MotionService::poll(uint32_t now) {
  bool got = false;
  gyroSample_ = false;
  Vec3 a, m, g;
  bool hg = false;
  if (on_ && hw_.read(a, m, g, hg)) {
    got = true;
    a_ = mountApply(mount_, a);                   // board frame -> device frame, all three sensors alike
    m_ = mountApply(mount_, m);
    compass_.update(a_, m_, now);                 // field strength and the no-gyro fallback
    if (hg) {
      g_ = mountApply(mount_, g);
      ahrs_.update(g_, a_, compass_.corrected(m_), now);
      gyroMs_ = now;
      haveGyro_ = true;
      gyroSample_ = true;
    }
  }
  if (haveGyro_ && now - gyroMs_ > 500) haveGyro_ = false;     // the gyro went quiet: the compass alone again
  fused_ = haveGyro_ && ahrs_.valid();
  return got;
}

bool MotionService::tilt(float* rollDeg, float* pitchDeg) const {
  Vec3 u = a_;
  if (fused_) u = ahrs_.up();
  if (!isfinite(u.x) || !isfinite(u.y) || !isfinite(u.z)) return false;
  const float n = sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
  if (!(n > 1e-3f)) return false;
  *rollDeg = atan2f(-u.x, u.z) * 57.29578f;
  *pitchDeg = atan2f(u.y, sqrtf(u.x * u.x + u.z * u.z)) * 57.29578f;
  return true;
}

float MotionService::fieldQuality() const {
  if (fused_) return ahrs_.trust();
  const MagCal& c = compass_.calibration();
  if (!c.valid || !(c.radius > 1.0f) || !compass_.valid()) return 0.0f;
  const float dev = fabsf(compass_.fieldUt() - c.radius) / c.radius;
  const float q = 1.0f - dev / 0.25f;
  return q < 0 ? 0.0f : (q > 1 ? 1.0f : q);
}
