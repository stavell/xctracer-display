#include "core/xctrc.h"
#include <stdlib.h>
#include <string.h>

static float sane(float v, float lo, float hi) { return (isfinite(v) && v >= lo && v <= hi) ? v : NAN; }

// Whole-field integer in [lo, hi]; false for empty, junk or out of range (no atoi: it overflows silently).
static bool intField(const char* f, long lo, long hi, long& out) {
  char* end;
  long v = strtol(f, &end, 10);
  if (end == f || *end != 0 || v < lo || v > hi) return false;
  out = v;
  return true;
}

static float norm360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }

bool xctrcParse(const char* line, Sample& out) {
  char s[160];
  size_t len = strlen(line);
  if (len == 0 || len >= sizeof s) return false;
  memcpy(s, line, len + 1);

  char* star = strrchr(s, '*');
  if (s[0] != '$' || !star || strlen(star) < 3) return false;
  uint8_t cs = 0;
  for (char* p = s + 1; p < star; p++) cs ^= (uint8_t)*p;
  if (cs != (uint8_t)strtoul(star + 1, nullptr, 16)) return false;
  *star = 0;

  char* f[24];
  int n = 0;
  char* p = s;
  while (n < 24) {
    f[n++] = p;
    char* c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  if (n < 19 || strcmp(f[0], "$XCTRC") != 0) return false;

  auto num = [&](int i, float lo, float hi) -> float { return f[i][0] ? sane((float)atof(f[i]), lo, hi) : NAN; };
  long hh, mm, ss;
  if (!intField(f[4], 0, 23, hh) || !intField(f[5], 0, 59, mm) || !intField(f[6], 0, 60, ss)) return false;

  Sample r;
  r.hh = (uint8_t)hh; r.mm = (uint8_t)mm; r.ss = (uint8_t)ss;
  // A fix needs a real position: finite and on the globe. The tracer sends 0,0 without one; anything
  // else impossible ("nan", "inf", 1e300, lat 95) is treated the same, never as a fix.
  double lat = atof(f[8]), lon = atof(f[9]);
  r.fix = isfinite(lat) && isfinite(lon) && fabs(lat) <= 90.0 && fabs(lon) <= 180.0 && !(lat == 0.0 && lon == 0.0);
  r.lat = r.fix ? lat : 0;
  r.lon = r.fix ? lon : 0;
  r.alt = num(10, -1000, 30000);
  r.speedKmh = r.fix ? num(11, 0, 400) : NAN;
  r.course = r.fix ? num(12, -720, 720) : NAN;
  if (isfinite(r.course)) r.course = norm360(r.course);
  r.vario = num(13, -50, 50);
  r.pressure = num(17, 100, 1200);
  long batt;
  r.battPct = intField(f[18], 0, 100, batt) ? (int)batt : -1;
  out = r;
  return true;
}
