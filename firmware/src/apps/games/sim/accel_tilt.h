#pragma once
// The tilt source of the box without front buttons: roll and pitch of the box from gravity alone (the accelerometer of the
// board, through the case mounting), lightly smoothed. No gyro, no magnetometer: the sim only needs where "down" is.
// Pure maths over the ICompass port; the sensors run only between enable(true) and enable(false) (SimApp::begin / end).
//   roll  = atan2(-ax, ay)         right side down = positive (steering wheel seen from the pilot)
//   pitch = atan2(az, hypot(ax, ay))  top away from the pilot = positive
// (a, device frame: x right, y top, z out of the screen, +g along the axis pointing up).
#include <math.h>
#include "apps/games/sim/tilt.h"
#include "platform/ports.h"
#include "platform/sensors/mount.h"
#include "platform/settings/box_settings.h"

namespace sim {

class AccelTilt : public ITilt {
 public:
  static constexpr float kAlpha = 0.25f;                 // per new sample (about 20 ms apart): tau ~ 70 ms
  void attach(ICompass* c, const BoxSettings* s) { compass_ = c; settings_ = s; }

  void enable(bool on) override {
    have_ = false;
    if (!compass_) return;
    mount_ = settings_ ? settings_->mountIndex() : 0;
    compass_->enable(on);
  }
  bool read(float* rollDeg, float* pitchDeg) override {
    if (!compass_) return false;
    Vec3 a, m, g;
    bool hg = false;
    if (compass_->read(a, m, g, hg)) {
      a = mountApply(mount_, a);
      float r, p;
      if (angles(a, &r, &p)) {
        if (!have_) { roll_ = r; pitch_ = p; have_ = true; }
        else { roll_ += kAlpha * (r - roll_); pitch_ += kAlpha * (p - pitch_); }
      }
    }
    if (!have_) return false;
    *rollDeg = roll_;
    *pitchDeg = pitch_;
    return true;
  }
  // False in free fall or with garbage (no direction of gravity to read).
  static bool angles(const Vec3& a, float* rollDeg, float* pitchDeg) {
    if (!isfinite(a.x) || !isfinite(a.y) || !isfinite(a.z)) return false;
    const float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (!(n > 4.0f && n < 20.0f)) return false;
    *rollDeg = atan2f(-a.x, a.y) * 57.29578f;
    *pitchDeg = atan2f(a.z, sqrtf(a.x * a.x + a.y * a.y)) * 57.29578f;
    return true;
  }

 private:
  ICompass* compass_ = nullptr;
  const BoxSettings* settings_ = nullptr;
  int mount_ = 0;
  bool have_ = false;
  float roll_ = 0, pitch_ = 0;
};

}  // namespace sim
