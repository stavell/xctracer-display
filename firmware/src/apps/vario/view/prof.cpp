#include "apps/vario/view/prof.h"
#include <stdio.h>

uint32_t (*gProfClock)() = nullptr;

#define PN(n) {n, 0, 0, 0, 0, 0}
ProfStat gProf[P_COUNT] = {
  PN("live"), PN("header"), PN("cells"), PN("vario"), PN("dial"), PN("wcorner"), PN("thermal"),
  PN("trail"), PN("marks"), PN("wprofile"), PN("home"), PN("wline"), PN("clear"), PN("present"),
  PN("ring"), PN("bezel"), PN("ticks"), PN("labels"), PN("hdgnum"), PN("climb"),
};

void profSetClock(uint32_t (*usNow)()) { gProfClock = usNow; }

void profReset() {
  for (ProfStat& s : gProf) s.last = s.max = s.sum = s.count = s.t0 = 0;
}

void profResetMax() {
  for (ProfStat& s : gProf) s.max = 0;
}

size_t profFormat(char* out, size_t n) {
  if (n == 0) return 0;
  size_t k = (size_t)snprintf(out, n, "prof");
  for (const ProfStat& s : gProf) {
    if (s.count == 0 || k >= n) continue;
    k += (size_t)snprintf(out + k, n - k, " %s=%lu/%lu", s.name, (unsigned long)s.last, (unsigned long)s.max);
  }
  return k < n ? k : n - 1;
}
