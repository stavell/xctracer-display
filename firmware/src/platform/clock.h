#pragma once
// Wall-clock time of day for the box's own screens. The box has no clock of its own: the tracer's GPS time is the
// only source. The vario stores it here whenever a sample with a fix arrives; the home screen extrapolates with
// the monotonic millisecond counter. Unknown (never set, or older than 12 h) reads as -1: never a made-up time.
#include <stdint.h>

class WallClock {
 public:
  void set(int utcSecondOfDay, uint32_t nowMs) {
    if (utcSecondOfDay < 0 || utcSecondOfDay >= 86400) return;
    sec_ = utcSecondOfDay;
    at_ = nowMs;
    known_ = true;
  }
  // Local minutes of the day, 0..1439, or -1 when unknown.
  int localMinutes(int tzMinutes, uint32_t nowMs) const {
    if (!known_) return -1;
    const uint32_t dt = nowMs - at_;                 // unsigned: correct across the millis() wrap
    if (dt > 12u * 3600u * 1000u) return -1;
    const long s = (long)sec_ + (long)(dt / 1000u);
    long m = s / 60 + tzMinutes;
    m %= 1440;
    if (m < 0) m += 1440;
    return (int)m;
  }

 private:
  int sec_ = 0;
  uint32_t at_ = 0;
  bool known_ = false;
};
