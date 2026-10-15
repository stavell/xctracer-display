#include "core/xctrc.h"
#include <stdlib.h>
#include <string.h>

static float sane(float v, float lo, float hi) { return (isfinite(v) && v >= lo && v <= hi) ? v : NAN; }

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
  int hh = atoi(f[4]), mm = atoi(f[5]), ss = atoi(f[6]);
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return false;

  Sample r;
  r.hh = (uint8_t)hh; r.mm = (uint8_t)mm; r.ss = (uint8_t)ss;
  r.lat = atof(f[8]);
  r.lon = atof(f[9]);
  r.fix = !(r.lat == 0.0 && r.lon == 0.0);
  r.alt = num(10, -1000, 30000);
  r.speedKmh = r.fix ? num(11, 0, 400) : NAN;
  r.course = r.fix ? num(12, -720, 720) : NAN;
  r.vario = num(13, -50, 50);
  r.pressure = num(17, 100, 1200);
  r.battPct = f[18][0] ? atoi(f[18]) : -1;
  out = r;
  return true;
}
