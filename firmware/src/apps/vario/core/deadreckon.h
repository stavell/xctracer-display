#pragma once
// Dead reckoning between GPS fixes from the gyro turn rate. Pure maths; nothing in the app uses it yet (it is prepared and
// validated first: docs/thermal_review.md, "What the box's own sensors can add", item 1).
//
// Why: the tracer sends a fix about 3 times a second, and a line is 0.5 to 1.5 s old when it is on screen (the GPS course
// itself lags, then the BLE path). In a thermal turn of 12 to 15 deg/s that is 10 to 20 degrees and 10 to 15 m of position
// error. The gyro has no lag, so: take the fix as the state at (receive time - latency), carry it forward through the
// stored gyro samples to the latest one, and extend it to "now" with the last turn rate. Every new fix replaces the
// whole state, so gyro drift never accumulates beyond one fix interval.
//
// Model: the ground velocity is the AIR velocity plus the wind. The turn rotates the air velocity (the glider's own
// motion through the air); the wind stays. With no wind (the default) this is just "course = fix course + integral of the
// yaw rate, position = fix + speed along that course". With a wind estimate a circle drifts correctly: the ground track
// of a drifting circle does not turn at the constant yaw rate, but the air velocity does.
//
// The yaw rate is the earth-frame turn rate about the vertical, clockwise positive (platform/sensors/yawrate.h); only the
// turn rate is used, never an absolute heading, so the mounting does not matter.
#include <stdint.h>

class DeadReckoner {
 public:
  struct Estimate {
    bool valid = false;        // a fix is known and recent enough (maxAgeMs)
    bool gyroLive = false;     // the yaw rate used is fresh; false: the course is only held (the fix's, plus the turn so far)
    double lat = 0, lon = 0;
    float courseDeg = 0;       // 0..360
    float speedKmh = 0;
    float eastM = 0, northM = 0;   // from the fix position
  };

  // Time from the instant the fix describes to its arrival here (GPS course and speed lag, tracer, BLE). Measured by the
  // log evaluator (host/logeval.py); 0.8 s is a first guess.
  void setLatencyMs(float ms) { latencyMs_ = ms; }
  float latencyMs() const { return latencyMs_; }
  void setMaxAgeMs(uint32_t ms) { maxAgeMs_ = ms; }
  // Wind the air moves with (m/s east, north); valid=false = no wind.
  void setWind(bool valid, float eastMs, float northMs);

  // A yaw rate sample (deg/s, clockwise positive) valid for the time since the previous sample. Times must increase.
  void yaw(uint32_t tMs, float rateDps);
  // A fix received at rxMs. courseDeg NaN or no fix: the state becomes invalid.
  void fix(uint32_t rxMs, bool haveFix, double lat, double lon, float speedKmh, float courseDeg);
  Estimate at(uint32_t nowMs) const;
  void reset();

  static constexpr int kHistory = 128;               // yaw samples kept: 1.6 s at 80 Hz, 5 s at 25 Hz

 private:
  struct State { uint32_t t; float ax, ay; float px, py; };   // air velocity (m/s) and position (m east / north of the fix)
  static void advance(State& s, uint32_t t, float rateDps, float wx, float wy);
  float latencyMs_ = 800;
  uint32_t maxAgeMs_ = 3000;
  bool haveFix_ = false;
  double lat0_ = 0, lon0_ = 0;
  uint32_t fixRx_ = 0;
  State s_ = {};
  float wx_ = 0, wy_ = 0;
  float lastRate_ = 0;
  uint32_t lastYawMs_ = 0;
  bool haveYaw_ = false;
  struct YawSample { uint32_t t; float rate; };
  YawSample hist_[kHistory];
  int head_ = 0, n_ = 0;
};
