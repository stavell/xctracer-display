#include "apps/vario/core/windprofile.h"
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

void WindProfile::addTemp(float altM, float tempC, uint32_t clockMs) {
  if (!isfinite(altM) || !isfinite(tempC)) return;
  Band& b = b_[bandOf(altM)];
  bool stale = b.tCount == 0 || clockMs - b.tLast > EXPIRY_MS;
  if (stale) { b.t = tempC; b.tCount = 0; }
  else b.t += ALPHA * (tempC - b.t);
  if (b.tCount < 65535) b.tCount++;
  b.tLast = clockMs;
}

int WindProfile::query(ProfileBand* out, int maxN, uint32_t nowMs) const {
  int cnt = 0;
  for (int i = 0; i < BANDS && cnt < maxN; i++) {
    const Band& b = b_[i];
    bool wind = b.count > 0 && nowMs - b.last <= EXPIRY_MS;
    bool temp = b.tCount > 0 && nowMs - b.tLast <= EXPIRY_MS;
    if (!wind && !temp) continue;
    ProfileBand& o = out[cnt++];
    o.altM = i * BAND_M + BAND_M / 2.0f;
    o.speedKmh = wind ? (float)(hypot(b.e, b.n) * 3.6) : NAN;
    o.toDeg = wind ? (float)fmod(atan2(b.e, b.n) * R2D + 360.0, 360.0) : NAN;
    o.ageS = (wind ? nowMs - b.last : nowMs - b.tLast) / 1000;
    o.samples = wind ? b.count : 0;
    o.tempC = temp ? b.t : NAN;
    o.tempSamples = temp ? b.tCount : 0;
  }
  return cnt;
}

float lapseRate(const ProfileBand* b, int n) {
  int cnt = 0;
  float lo = 1e9f, hi = -1e9f, ma = 0, mt = 0;
  for (int i = 0; i < n; i++) {
    if (!isfinite(b[i].tempC) || !isfinite(b[i].altM)) continue;
    cnt++; ma += b[i].altM; mt += b[i].tempC;
    if (b[i].altM < lo) lo = b[i].altM;
    if (b[i].altM > hi) hi = b[i].altM;
  }
  if (cnt < 3 || hi - lo < 200) return NAN;
  ma /= cnt; mt /= cnt;                          // centred sums keep float precision at 6000 m
  float sxy = 0, sxx = 0;
  for (int i = 0; i < n; i++) {
    if (!isfinite(b[i].tempC) || !isfinite(b[i].altM)) continue;
    float dx = b[i].altM - ma;
    sxy += dx * (b[i].tempC - mt);
    sxx += dx * dx;
  }
  return sxx > 0 ? 100.0f * sxy / sxx : NAN;
}

bool inversionAt(const ProfileBand* b, int n, int i) {
  if (i < 0 || i >= n || !isfinite(b[i].tempC)) return false;
  for (int j = 0; j < n; j++) {
    if (fabsf(b[j].altM - (b[i].altM - WindProfile::BAND_M)) < 1.0f)
      return isfinite(b[j].tempC) && b[i].tempC - b[j].tempC > 0.2f;
  }
  return false;
}
