#pragma once
#include "core/types.h"

// Synthetic flight for bench testing: glide, thermal (with a 12 km/h east wind), glide, 150 s loop.
class FlightSim {
 public:
  void reset(uint32_t nowMs);
  bool step(uint32_t nowMs, Sample& out);   // true when a new sample was produced (about 3 per second)
 private:
  uint32_t t0_ = 0, last_ = 0;
  double x_ = 0, y_ = 0, alt_ = 1500;
  float course_ = 90;
};
