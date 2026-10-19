#pragma once
#include <math.h>
#include <stdio.h>

// A compass bearing in tenths of a degree, 0..3599: round first, then wrap, so 359.95..360 reads 0 and
// 360.0 never appears. Returns -1 for NaN/inf (callers print "--").
inline int bearingTenths(float deg) {
  if (!isfinite(deg)) return -1;
  long r = lroundf(fmodf(deg, 360.0f) * 10.0f);   // fmodf first keeps lroundf in range for huge inputs
  r %= 3600;
  return (int)(r < 0 ? r + 3600 : r);
}

// "0.0".."359.9", or "--" when not finite.
inline const char* bearingText(char* b, size_t n, float deg) {
  int v = bearingTenths(deg);
  if (v < 0) snprintf(b, n, "--");
  else snprintf(b, n, "%d.%d", v / 10, v % 10);
  return b;
}
