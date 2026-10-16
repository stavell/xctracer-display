// Draw-time profiler: a fixed table of named sections (last, max, sum, count in microseconds).
// Pure C++, no hardware: the clock is injected with profSetClock(). With no clock every call is a
// single null test, so the hooks stay compiled in all builds and only the diagnostics build sets one.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum ProfId : uint8_t {
  P_LIVE,      // all of drawLive
  P_HEADER,
  P_CELLS,     // the four value cells together
  P_VARIO,     // thin vario strip
  P_DIAL,      // compass dial
  P_WCORNER,   // wind arrow in the compass corner
  P_THERMAL,   // whole thermal map
  P_TRAIL,     //   of which the trail
  P_MARKS,     //   of which thermal markers and labels
  P_WPROFILE,  // wind profile page
  P_HOME,      // home page
  P_WLINE,     // wind text line at the bottom
  P_CLEAR,     // App: panel clear
  P_PRESENT,   // App: panel present (SPI write)
  P_RING,      // compass: grey ring
  P_BEZEL,     // compass: circles and the direction pointer
  P_TICKS,     // compass: tick marks
  P_LABELS,    // compass: N/NE/E.. letters
  P_HDGNUM,    // compass: heading number and source
  P_CLIMB,     // centring page: the turns and the core
  P_COUNT
};

struct ProfStat {
  const char* name;
  uint32_t last, max, sum, count;
  uint32_t t0;
};

extern uint32_t (*gProfClock)();
extern ProfStat gProf[P_COUNT];

void profSetClock(uint32_t (*usNow)());
void profReset();       // zero everything
void profResetMax();    // zero only the max column
// "prof name=last/max ..." (sections with count 0 are skipped); returns the length written.
size_t profFormat(char* out, size_t n);

inline void profBegin(ProfId id) {
  if (gProfClock) gProf[id].t0 = gProfClock();
}
inline void profEnd(ProfId id) {
  if (!gProfClock) return;
  ProfStat& s = gProf[id];
  uint32_t dt = gProfClock() - s.t0;     // unsigned: survives the 32-bit wrap
  s.last = dt;
  if (dt > s.max) s.max = dt;
  s.sum += dt;
  s.count++;
}

#define PROF_BEGIN(id) profBegin(id)
#define PROF_END(id) profEnd(id)
