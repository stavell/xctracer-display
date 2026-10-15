#include "core/windprofile.h"
#include <math.h>

static const float ALPHA = 0.25f;
static const double R2D = 57.29577951308232;

void WindProfile::reset() {
  for (int i = 0; i < BANDS; i++) b_[i] = Band();
}

int WindProfile::bandOf(float altM) {
  if (!isfinite(altM) || altM < 0) return 0;
  if (altM >= BANDS * BAND_M) return BANDS - 1;
  return (int)(altM / BAND_M);
}

void WindProfile::add(float altM, float e, float n, uint32_t clockMs) {
  if (!isfinite(altM) || !isfinite(e) || !isfinite(n)) return;
  Band& b = b_[bandOf(altM)];
  bool stale = b.count == 0 || clockMs - b.last > EXPIRY_MS;   // an expired band starts afresh
  if (stale) { b.e = e; b.n = n; b.count = 0; }
  else { b.e += ALPHA * (e - b.e); b.n += ALPHA * (n - b.n); }
  if (b.count < 65535) b.count++;
  b.last = clockMs;
}

int WindProfile::query(ProfileBand* out, int maxN, uint32_t nowMs) const {
  int cnt = 0;
  for (int i = 0; i < BANDS && cnt < maxN; i++) {
    const Band& b = b_[i];
    if (b.count == 0) continue;
    uint32_t age = nowMs - b.last;
    if (age > EXPIRY_MS) continue;
    ProfileBand& o = out[cnt++];
    o.altM = i * BAND_M + BAND_M / 2.0f;
    o.speedKmh = (float)(hypot(b.e, b.n) * 3.6);
    o.toDeg = (float)fmod(atan2(b.e, b.n) * R2D + 360.0, 360.0);
    o.ageS = age / 1000;
    o.samples = b.count;
  }
  return cnt;
}
