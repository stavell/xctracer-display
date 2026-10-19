#pragma once
// The sleeping clock as a weather station: the data side (pure; the screen is platform/power/clock_view.h).
//   WeatherLog   a 24 h ring of barometric pressure, one sample per 10 min, hPa*10 as int16 (288 B, RAM only: a reset or the
//                full power-off loses it), plus the last temperature and humidity.
//   tendency     the change over the last 3 h and its class (steady / slow / fast, rising or falling).
//   forecast     a rough barometer forecast from that tendency: sun, sun + cloud, cloud, rain, storm.
//   chartRange / chartY / chartX   the chart's scale and the row / column of a sample.
// Station pressure (what the BMP280 measures) is not sea-level pressure and the box does not know its height, so the
// forecast uses the tendency only, never the absolute level.
#include <math.h>
#include <stdint.h>
#include "platform/ports.h"

enum Forecast : uint8_t { FC_NONE, FC_SUN, FC_SUN_CLOUD, FC_CLOUD, FC_RAIN, FC_STORM };

class WeatherLog {
 public:
  static const int kN = 144;                         // 24 h of 10 min
  static const uint32_t kEveryMs = 10u * 60u * 1000u;
  static const int kTrendAge = 18;                   // samples back for the 3 h tendency

  float tempC = NAN, rhPct = NAN;                    // last single reads; NaN = unknown

  void clear() { n_ = 0; head_ = 0; haveLast_ = false; tempC = rhPct = NAN; }
  int count() const { return n_; }
  // Age 0 = newest. hPa*10, 0 when there is no such sample.
  int get(int age) const { return age >= 0 && age < n_ ? v_[(head_ - 1 - age + 2 * kN) % kN] : 0; }
  int latest() const { return get(0); }

  // Adds one reading (hPa); ignores values outside 300..1100 hPa and NaN.
  void push(float hPa) {
    if (!(hPa >= 300.0f && hPa <= 1100.0f)) return;
    v_[head_] = (int16_t)lroundf(hPa * 10.0f);
    head_ = (head_ + 1) % kN;
    if (n_ < kN) n_++;
  }
  // Takes a sample when 10 min have passed since the last one (or none yet); a failed read is retried after a minute.
  void sample(uint32_t nowMs, IBaro& baro) {
    if (haveLast_ && (uint32_t)(nowMs - lastMs_) < kEveryMs) return;
    float h = NAN;
    const bool ok = baro.readNow(h);
    haveLast_ = true;
    if (ok) { push(h); lastMs_ = nowMs; }
    else lastMs_ = nowMs - kEveryMs + 60000u;
  }

  // Change over the last 3 h in hPa*10; false until that much history exists.
  bool delta3h(int& d10) const {
    if (n_ <= kTrendAge) return false;
    d10 = get(0) - get(kTrendAge);
    return true;
  }
  // -2 falling fast, -1 falling, 0 steady, +1 rising, +2 rising fast; false when unknown.
  bool tendency(int& cls) const {
    int d;
    if (!delta3h(d)) return false;
    const int a = d < 0 ? -d : d;
    cls = a <= 10 ? 0 : a <= 35 ? 1 : 2;
    if (d < 0) cls = -cls;
    return true;
  }
  Forecast forecast() const {
    int d;
    if (!delta3h(d)) return FC_NONE;
    if (d >= 10) return FC_SUN;                      // rising: clearing, fine
    if (d > -10) return FC_SUN_CLOUD;                // steady: unsettled
    if (d > -35) return FC_CLOUD;                    // falling: clouding over
    if (d > -60) return FC_RAIN;                     // falling fast: rain
    return FC_STORM;                                 // 6 hPa in 3 h: storm
  }

  // The chart's scale (hPa*10, multiples of 10): the data's span, at least 4 hPa. false with no data.
  bool chartRange(int& lo10, int& hi10) const {
    if (n_ == 0) return false;
    int lo = 99999, hi = -99999;
    for (int i = 0; i < n_; i++) { const int x = get(i); if (x < lo) lo = x; if (x > hi) hi = x; }
    lo = lo / 10 * 10;                               // pressures are positive: truncation is the floor
    hi = (hi + 9) / 10 * 10;
    while (hi - lo < 40) { hi += 10; if (hi - lo < 40) lo -= 10; }
    lo10 = lo; hi10 = hi;
    return true;
  }

 private:
  int16_t v_[kN] = {};
  int n_ = 0, head_ = 0;
  uint32_t lastMs_ = 0;
  bool haveLast_ = false;
};

// What the sleeping clock does at each redraw: the pressure sample if 10 min are due, and one single-shot temperature and
// humidity read (the sensors idle in between). Unknown stays NaN, never the last value of an hour ago.
inline void weatherRefresh(WeatherLog& wl, uint32_t nowMs, IThermo& thermo, IBaro& baro) {
  wl.sample(nowMs, baro);
  float t = NAN, rh = NAN;
  if (thermo.readNow(t, rh)) { wl.tempC = t; wl.rhPct = rh; }
  else { wl.tempC = NAN; wl.rhPct = NAN; }
}

// Row (0 = top) of a pressure sample in a chart h rows high, 0..h-1.
inline int chartY(int v10, int lo10, int hi10, int h) {
  if (hi10 <= lo10) return h - 1;
  const long y = (long)(hi10 - v10) * (h - 1) / (hi10 - lo10);
  return y < 0 ? 0 : y > h - 1 ? h - 1 : (int)y;
}
// Column of a sample of the given age (0 = newest at the right edge) in a chart w wide.
inline int chartX(int age, int w) { return (w - 1) - age * (w - 1) / (WeatherLog::kN - 1); }
