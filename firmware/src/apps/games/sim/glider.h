#pragma once
// The paraglider: polar, turn dynamics, speed bar, stall. Pure float maths in SI units, no allocation.
//
//   Polar: still-air sink against airspeed, a table with linear interpolation (km/h -> m/s down). Wing loaded
//   so that trim is 37 km/h, minimum sink 1.10 m/s at 33 km/h, full speed bar 52 km/h at 2.20 m/s.
//   Turn: coordinated, omega = g tan(bank) / V. The load factor n = 1 / cos(bank) adds induced drag, which grows with
//   n squared: sink(V, n) = sink(V) + (n^2 - 1) * 7.5 / V (V in m/s), about +30 % in a 35 degree turn at trim speed;
//   the stall speed rises by sqrt(n).
//   Roll: the real bank follows the commanded bank with a first-order lag (the pendulum, 0.4 s).
//   Speed: the target airspeed is trim + bar * (max - trim) - brake * (trim - slow); the airspeed follows it with
//   a lag of 1.2 s. Below the stall speed (26 km/h * sqrt(n)) the wing stalls: heavy sink, the warning flag, and
//   it recovers when the brake is released and the speed is back above the stall speed + 3 km/h.
#include "apps/games/sim/sim_math.h"

namespace sim {

struct Polar {
  static constexpr float kStallKmh = 26.0f;
  static constexpr float kMinSinkKmh = 33.0f;
  static constexpr float kTrimKmh = 37.0f;
  static constexpr float kBarKmh = 52.0f;       // full speed bar
  static constexpr float kSlowKmh = 28.0f;      // full brake (still flying; in a turn the stall speed rises above it)
  static constexpr float kInduced = 7.5f;       // induced-drag constant: extra sink in a turn = (n^2 - 1) * kInduced / V (V in m/s)
  // Still-air sink (m/s, positive down) at 1 g for an airspeed in km/h; clamped to the table's ends.
  static float sinkKmh(float kmh);
  // Sink at load factor n.
  static float sink(float kmh, float n);
  // Best glide speed in still air (km/h) from the table, and the glide ratio there.
  static float bestGlideKmh(float* ratio);
};

struct Controls {
  float bank = 0;       // -1 (full left) .. 1 (full right)
  float bar = 0;        // speed bar 0..1
  float brake = 0;      // 0..1
};

struct Glider {
  static constexpr float kMaxBank = 35.0f * kDeg2Rad;    // at full stick
  static constexpr float kRollTau = 0.4f;                // pendulum / roll lag, s
  static constexpr float kSpeedTau = 1.2f;
  static constexpr float kStallSink = 5.5f;              // m/s while stalled

  float x = 0, y = 0, z = 0;       // m east, m north, m above sea level
  float heading = 0;               // rad, 0 = north, clockwise
  float bank = 0;                  // rad, right wing down positive (actual)
  float airspeed = Polar::kTrimKmh * kKmh2Ms;   // m/s
  float sinkRate = 1.2f;           // m/s relative to the air (positive down), what the polar says
  float vz = 0;                    // m/s over the ground, positive up (what a vario shows, before its own lag)
  float turnRate = 0;              // rad/s, positive = clockwise (right)
  float load = 1;                  // load factor in g
  float gx = 0, gy = 0;            // velocity over the ground, m/s east / north
  bool stalled = false;
  float stallBlend = 0;            // 0..1, how deep into the stall
  float barSmooth = 0, brakeSmooth = 0;

  void reset(float x0, float y0, float z0, float heading0);
  float groundSpeed() const { return sqrtf(gx * gx + gy * gy); }
  float airspeedKmh() const { return airspeed * 3.6f; }
  // Advance dt seconds in an air mass moving (wx, wy) horizontally and wz vertically (m/s).
  void step(const Controls& c, float wx, float wy, float wz, float dt);
};

}  // namespace sim
