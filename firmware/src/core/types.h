#pragma once
// Plain data shared by every layer. No Arduino, no BLE, no display.
#include <math.h>
#include <stdint.h>

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
