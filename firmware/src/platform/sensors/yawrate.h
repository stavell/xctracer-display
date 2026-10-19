#pragma once
// The turn rate about the vertical, from a gyro in the device frame and the AHRS's gravity direction. Pure maths.
//
// The mounting does not matter: only the part of the rotation vector along "up" is kept, so it is the same number whether the
// box lies flat, hangs on a riser or sits tilted in a cockpit. Positive = clockwise seen from above, the way a compass
// heading and a GPS course increase. `up` is Ahrs::up() (unit, device frame), `bias` Ahrs::bias() (rad/s).
#include <math.h>
#include "platform/util/types.h"

inline float earthYawRateCwDps(const Vec3& gyro, const Vec3& bias, const Vec3& up) {
  const float r = (gyro.x - bias.x) * up.x + (gyro.y - bias.y) * up.y + (gyro.z - bias.z) * up.z;   // rad/s, counter-clockwise positive
  return -r * 57.29577951f;
}
