#pragma once
// The "wth" status line of the box (USB serial, printed with the other status lines while a terminal is attached): the weather
// sensors, the battery and the retained history, on one line. Pure formatting; platform/hw/wth_status.h reads the sensors.
//   wth p=1013.2 tb=21.5 ts=21.4 rh=48 batt=76% 3987mV log=12 ret=ok
// p = BMP280 pressure hPa, tb = its temperature C, ts / rh = SHT30 temperature C and humidity %, batt = percent and millivolts,
// log = samples in the weather log, ret = what the boot restored: ok, crc (a block that failed its checksum) or none.
// Anything unknown prints --.
#include <math.h>
#include <stddef.h>
#include <stdio.h>

struct WthData {
  float hPa = NAN, baroC = NAN, shtC = NAN, rh = NAN;
  int battPct = -1, battMv = 0;                       // percent < 0 / millivolts <= 0: unknown
  int logCount = 0;
  const char* ret = "none";
};

inline int formatWth(char* out, size_t cap, const WthData& d) {
  char p[12], tb[12], ts[12], rh[12], bp[8], bm[8];
  auto f = [](char* b, size_t n, float v, const char* fmt) { if (isfinite(v)) snprintf(b, n, fmt, (double)v); else snprintf(b, n, "--"); };
  f(p, sizeof p, d.hPa, "%.1f");
  f(tb, sizeof tb, d.baroC, "%.1f");
  f(ts, sizeof ts, d.shtC, "%.1f");
  f(rh, sizeof rh, d.rh, "%.0f");
  if (d.battPct >= 0) snprintf(bp, sizeof bp, "%d%%", d.battPct); else snprintf(bp, sizeof bp, "--");
  if (d.battMv > 0) snprintf(bm, sizeof bm, "%dmV", d.battMv); else snprintf(bm, sizeof bm, "--");
  return snprintf(out, cap, "wth p=%s tb=%s ts=%s rh=%s batt=%s %s log=%d ret=%s", p, tb, ts, rh, bp, bm, d.logCount, d.ret);
}
