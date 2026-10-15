#pragma once
// Plain data shared by every layer. No Arduino, no BLE, no display.
#include <math.h>
#include <stdint.h>

// NaN marks missing data everywhere; with -ffast-math / -ffinite-math-only every isfinite() check is
// compiled away. Refuse such builds.
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "built with finite-math-only (-Ofast / -ffast-math): NaN checks would be removed"
#endif

// One decoded $XCTRC line from the vario.
struct Sample {
  bool fix = false;                 // GPS fix (the tracer sends lat = lon = 0 without one)
  double lat = 0, lon = 0;
  float alt = NAN, speedKmh = NAN, course = NAN, vario = NAN, pressure = NAN;
  int battPct = -1;
  uint8_t hh = 0, mm = 0, ss = 0;   // UTC time of day from the vario
};

// One entry per data second, for averages, circling detection and the wind fit.
struct TrackPoint {
  bool fix;
  double lat, lon;
  float alt, vario, speedKmh, course;
};

struct Wind {
  bool valid = false;
  float speedKmh = 0;
  float toDeg = 0;                  // direction the wind blows TOWARD, 0 = north
  uint32_t ageS = 0;                // flight seconds since the last accepted fit
};

// A past position in the air-mass frame: metres east / north of the current position after removing
// the wind drift since then, so a thermal stays put while you circle in it.
struct AirPoint {
  float x, y, vario;
  float turn = 0;           // |heading change| in degrees per second at this point (0 if unknown)
};

// A remembered thermal, relative to the current position (metres east / north) after wind drift.
struct ThermalRel {
  float x, y;
  float avgClimb;        // m/s
  float gain;            // metres gained in it
  uint32_t ageS;         // seconds since the climb ended
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

// Hard-iron calibration of the magnetometer: the offset that centres the measured field on a sphere.
struct MagCal {
  bool valid = false;
  float off[3] = {0, 0, 0};      // microtesla, subtracted from every reading
  float radius = 0;              // microtesla, the field strength found during calibration
};
