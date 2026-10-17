#pragma once

// Extended Kalman filter for wind from ordinary heading changes (no full circle needed).
// State: wind east / north (m/s, the direction it blows toward) and airspeed (m/s).
// Pseudo-measurement per sample: |groundVelocity - wind| - airspeed = 0. With a constant airspeed this
// is only observable once the heading has changed, so the covariance stays large in straight flight and
// confident() stays false. Float only, no allocation.
// Learning is gated on a turn: a measurement is applied only while the course has moved by at least
// TURN_MIN_DEG within the last TURN_WIN_S seconds. In a near-straight glide the pseudo-measurement carries no
// wind information, but the airspeed is not constant there (pilot on and off the bar, turbulence), and the
// filter explained that scatter with a huge wind and a tiny airspeed, then reported it as confident
// (SVE flight from 4000 s: 42 km/h, truth 12; NOTES.md, "Wind EKF locked high").
class WindEkf {
 public:
  WindEkf() { reset(); }
  void reset();
  void init(float wx, float wy, float p);               // start from a known wind, P = diag(p, p, 9)
  // One data second. Returns true when a measurement update was applied (not skipped or rejected).
  bool step(float dtS, bool fix, float speedKmh, float courseDeg);
  bool confident() const { return sigmaW() < 0.8f; }
  float wx() const { return x_[0]; }
  float wy() const { return x_[1]; }
  float va() const { return x_[2]; }
  float sigmaW() const;
  static constexpr float TURN_MIN_DEG = 30;     // course spread that makes a second informative
  static constexpr int TURN_WIN_S = 20;
  static constexpr int REJECTS_VA = 5;          // outlier balance (turning seconds, an accepted one counts -1) that reopens the airspeed variance
  static constexpr int REJECTS_ALL = 10;        // ... and the whole covariance

 private:
  float turnSpread(float courseDeg) const;
  float x_[3];
  float crs_[TURN_WIN_S];                        // recent courses of usable seconds, newest at crsHead_ - 1
  int crsN_ = 0, crsHead_ = 0;
  int rejects_ = 0;
  float P_[3][3];
};
