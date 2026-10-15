#pragma once

// Extended Kalman filter for wind from ordinary heading changes (no full circle needed).
// State: wind east / north (m/s, the direction it blows toward) and airspeed (m/s).
// Pseudo-measurement per sample: |groundVelocity - wind| - airspeed = 0. With a constant airspeed this
// is only observable once the heading has changed, so the covariance stays large in straight flight and
// confident() stays false. Float only, no allocation.
class WindEkf {
 public:
  WindEkf() { reset(); }
  void reset();
  void init(float wx, float wy, float p);               // start from a known wind, P = diag(p, p, 9)
  void step(float dtS, bool fix, float speedKmh, float courseDeg);   // one data second
  bool confident() const { return sigmaW() < 0.8f; }
  float wx() const { return x_[0]; }
  float wy() const { return x_[1]; }
  float va() const { return x_[2]; }
  float sigmaW() const;

 private:
  float x_[3];
  float P_[3][3];
};
