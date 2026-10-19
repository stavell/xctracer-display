#pragma once
// Wall-clock time for the box's own screens. The box has no clock of its own: the time comes from the tracer's GPS
// (time of day, whenever a sample with a fix arrives) or from the PC over USB (seconds since 1970, host/settime.py), and
// is extrapolated with the monotonic millisecond counter. A GPS fix always overrides.
// Validity: millis() runs from the nRF's 32.768 kHz crystal (+-20 ppm worst case, about 1.7 s/day, under a minute after
// 30 days). After kValidMs the time reads unknown (-1, "--:--"): never a made-up time.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

class WallClock {
 public:
  static const uint32_t kValidMs = 30u * 24u * 3600u * 1000u;   // 30 days; below 2^32 ms (49.7 days), so dt stays unambiguous

  // GPS time of day. A known date is kept: the day nearest to the running clock (so midnight is crossed right).
  void set(int utcSecondOfDay, uint32_t nowMs) {
    if (utcSecondOfDay < 0 || utcSecondOfDay >= 86400) return;
    if (dayKnown_ && valid(nowMs)) {
      const int64_t run = (int64_t)sec_ + (int64_t)((nowMs - at_) / 1000u);   // running seconds since day_ 00:00 UTC
      long day = day_ + (long)(run / 86400);
      const long d = utcSecondOfDay - (long)(run % 86400);
      if (d > 43200) day--; else if (d < -43200) day++;
      day_ = day;
    } else {
      dayKnown_ = false;
    }
    sec_ = utcSecondOfDay;
    at_ = nowMs;
    known_ = true;
  }
  // The PC's time: seconds since 1970-01-01 UTC. Out of 2001..2100 is refused.
  bool setEpoch(int64_t utcEpochSec, uint32_t nowMs) {
    if (utcEpochSec < 1000000000LL || utcEpochSec > 4102444800LL) return false;
    day_ = (long)(utcEpochSec / 86400);
    sec_ = (int)(utcEpochSec % 86400);
    at_ = nowMs;
    known_ = true;
    dayKnown_ = true;
    return true;
  }
  bool valid(uint32_t nowMs) const { return known_ && nowMs - at_ <= kValidMs; }
  // Milliseconds until the local minute changes (1..60000), or -1 when the time is unknown. The sleeping clock
  // wakes up exactly then.
  long msToNextMinute(uint32_t nowMs) const {
    if (!valid(nowMs)) return -1;
    const uint32_t dt = nowMs - at_;
    const uint32_t into = (uint32_t)(sec_ % 60) * 1000u + dt % 60000u;
    return (long)(60000u - into % 60000u);
  }
  // Local minutes of the day, 0..1439, or -1 when unknown.
  int localMinutes(int tzMinutes, uint32_t nowMs) const {
    if (!valid(nowMs)) return -1;
    const uint32_t dt = nowMs - at_;                 // unsigned: correct across the millis() wrap
    const long s = (long)sec_ + (long)(dt / 1000u);
    long m = s / 60 + tzMinutes;
    m %= 1440;
    if (m < 0) m += 1440;
    return (int)m;
  }
  // Local date as days since 1970-01-01; false when unknown (a GPS-only time has no date).
  bool localDays(int tzMinutes, uint32_t nowMs, long* days) const {
    if (!valid(nowMs) || !dayKnown_) return false;
    const int64_t t = (int64_t)day_ * 86400 + sec_ + (int64_t)((nowMs - at_) / 1000u) + (int64_t)tzMinutes * 60;
    int64_t d = t / 86400;
    if (t % 86400 < 0) d--;
    *days = (long)d;
    return true;
  }
  // "Mon 19 Oct" from days since 1970-01-01.
  static void formatDate(long days, char* dst, size_t cap) {
    static const char* wd[7] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};   // 1970-01-01 was a Thursday
    static const char* mn[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
    long w = days % 7;
    if (w < 0) w += 7;
    snprintf(dst, cap, "%s %d %s", wd[w], d, mn[m - 1]);
  }

 private:
  int sec_ = 0;
  long day_ = 0;
  uint32_t at_ = 0;
  bool known_ = false, dayKnown_ = false;
};
