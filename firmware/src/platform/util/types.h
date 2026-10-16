#pragma once
// Plain data shared by every app and layer. No Arduino, no BLE, no display.
#include <math.h>
#include <stdint.h>

// NaN marks missing data everywhere; with -ffast-math / -ffinite-math-only every isfinite() check is
// compiled away. Refuse such builds.
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "built with finite-math-only (-Ofast / -ffast-math): NaN checks would be removed"
#endif

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

// Hard-iron calibration of the magnetometer: the offset that centres the measured field on a sphere.
struct MagCal {
  bool valid = false;
  float off[3] = {0, 0, 0};      // microtesla, subtracted from every reading
  float radius = 0;              // microtesla, the field strength found during calibration
};
