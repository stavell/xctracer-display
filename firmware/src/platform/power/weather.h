#pragma once
// The sleeping clock as a weather station: the data side (pure; the screen is platform/power/clock_view.h).
//   WeatherLog   a 24 h ring of barometric pressure, one sample per 10 min, hPa*10 as int16 (288 B), plus the last temperature
//                and humidity. A sample of 0 is a gap (no reading in that slot). The ring survives a reset and a System OFF
//                through platform/power/retained.h (a copy in retained RAM, restored at boot, the gap bridged when the time is known).
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

  void clear() { n_ = 0; head_ = 0; haveLast_ = false; tempC = rhPct = NAN; refEpoch_ = 0; pushed_ = 0; }
  int count() const { return n_; }
  // Age 0 = newest. hPa*10, 0 when there is no such sample.
  int get(int age) const { return age >= 0 && age < n_ ? v_[(head_ - 1 - age + 2 * kN) % kN] : 0; }
  int latest() const { return get(0); }

  // Adds one reading (hPa); ignores values outside 300..1100 hPa and NaN. epochNow: the wall-clock time in UTC seconds, 0 when
  // the clock is unknown (it anchors the ring in time, see bridge()).
  void push(float hPa, uint32_t epochNow = 0) {
    if (!(hPa >= 300.0f && hPa <= 1100.0f)) return;
    pushRaw((int16_t)lroundf(hPa * 10.0f));
    pushCount_++;
    if (bridge_ || (!epochNow && refEpoch_)) { if (pushed_ < 0xFFFF) pushed_++; }   // after the anchor: counted, bridged later
    else if (epochNow) { refEpoch_ = epochNow; pushed_ = 0; }
  }
  // Takes a sample when 10 min have passed since the last one (or none yet); a failed read is retried after a minute.
  void sample(uint32_t nowMs, IBaro& baro, uint32_t epochNow = 0) {
    if (haveLast_ && (uint32_t)(nowMs - lastMs_) < kEveryMs) return;
    float h = NAN;
    const bool ok = baro.readNow(h);
    haveLast_ = true;
    if (ok) { push(h, epochNow); lastMs_ = nowMs; }
    else lastMs_ = nowMs - kEveryMs + 60000u;
  }

  // ---- surviving a restart (platform/power/retained.h) ----
  uint32_t pushCount() const { return pushCount_; }  // pushes so far this boot (a cheap "changed" test)
  // Oldest first into out (room for kN); returns the count. With the time anchor: refEpoch() is the wall-clock time of the sample
  // `pushedSince()` places before the newest (0: unknown).
  int snapshot(int16_t* out) const {
    for (int i = 0; i < n_; i++) out[i] = v_[(head_ - n_ + i + 2 * kN) % kN];
    return n_;
  }
  uint32_t refEpoch() const { return refEpoch_; }
  int pushedSince() const { return pushed_; }
  // Replaces the ring by a retained one (oldest first). Samples taken since the boot stay after it. A bridge is due when ref != 0.
  void restore(const int16_t* oldestFirst, int n, uint32_t refEpoch, int pushed) {
    clear();
    if (n > kN) n = kN;
    for (int i = 0; i < n; i++) pushRaw(oldestFirst[i]);
    refEpoch_ = refEpoch;
    pushed_ = pushed;
    bridge_ = refEpoch != 0 && n > 0;
  }
  bool bridgePending() const { return bridge_; }
  // Once the wall clock is known: the slots that passed since the anchor become gaps (0), so the chart and the tendency stay
  // honest. Samples already taken since the boot stay at the end. Older than 24 h: the retained part is dropped. Returns the gaps inserted
  // (-1: the retained part was dropped). nowMs aligns the next sample with the slot grid.
  int bridge(uint32_t epochNow, uint32_t nowMs) {
    if (!bridge_ || !epochNow) return 0;
    bridge_ = false;
    const int k = pushed_ > n_ ? n_ : pushed_;                // the samples taken since the boot
    const uint32_t elapsed = epochNow >= refEpoch_ ? epochNow - refEpoch_ : 0;
    const uint32_t slots = elapsed / (kEveryMs / 1000u);
    int16_t tmp[kN];
    const int n = snapshot(tmp);
    const int nOld = n - k;
    clear();
    int gaps = 0;
    const bool drop = slots > (uint32_t)kN;                   // more than 24 h
    if (!drop) for (int i = 0; i < nOld; i++) pushRaw(tmp[i]);
    if (!drop && nOld > 0) {
      gaps = (int)(k == 0 ? (slots > 0 ? slots - 1 : 0) : (slots > (uint32_t)k ? slots - k : 0));
      for (int i = 0; i < gaps && i < kN; i++) pushRaw(0);
    }
    for (int i = nOld; i < n; i++) pushRaw(tmp[i]);
    refEpoch_ = epochNow;
    pushed_ = 0;
    if (!drop && k == 0) {                                     // the next sample is due when the next slot starts (at once after a gap)
      if (slots >= 1) haveLast_ = false;
      else { haveLast_ = true; lastMs_ = nowMs - elapsed * 1000u; }
    }
    return drop ? -1 : gaps;
  }

  // Change over the last 3 h in hPa*10; false until that much history exists (and both ends are real samples, not gaps).
  bool delta3h(int& d10) const {
    if (n_ <= kTrendAge || get(0) <= 0 || get(kTrendAge) <= 0) return false;
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
    for (int i = 0; i < n_; i++) { const int x = get(i); if (x <= 0) continue; if (x < lo) lo = x; if (x > hi) hi = x; }
    if (lo > hi) return false;                       // only gaps
    lo = lo / 10 * 10;                               // pressures are positive: truncation is the floor
    hi = (hi + 9) / 10 * 10;
    while (hi - lo < 40) { hi += 10; if (hi - lo < 40) lo -= 10; }
    lo10 = lo; hi10 = hi;
    return true;
  }

 private:
  void pushRaw(int16_t v) {
    v_[head_] = v;
    head_ = (head_ + 1) % kN;
    if (n_ < kN) n_++;
  }
  int16_t v_[kN] = {};
  int n_ = 0, head_ = 0;
  uint32_t lastMs_ = 0;
  bool haveLast_ = false;
  uint32_t refEpoch_ = 0, pushCount_ = 0;
  int pushed_ = 0;
  bool bridge_ = false;
};

// What the sleeping clock does at each redraw: the pressure sample if 10 min are due, and one single-shot temperature and
// humidity read (the sensors idle in between). Unknown stays NaN, never the last value of an hour ago.
inline void weatherRefresh(WeatherLog& wl, uint32_t nowMs, IThermo& thermo, IBaro& baro, uint32_t epochNow = 0) {
  wl.sample(nowMs, baro, epochNow);
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
