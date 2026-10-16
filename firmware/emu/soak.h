#pragma once
// `emu soak`: a long session through the real App (see the comment at the top of emu_main.cpp).
// Included by emu_main.cpp only: it replaces the global operator new / delete to count allocations.
#include <sys/resource.h>
#include <chrono>
#include <new>
#include <string>
#include <vector>
#include "apps/vario/app/app.h"
#include "apps/vario/core/flight.h"
#include "apps/vario/core/sim.h"
#include "apps/vario/core/thermal.h"
#include "fuzzgen.h"
#include "mocks.h"

// Allocation accounting: every global new/delete goes through here, so "memory does not grow" is measured
// exactly (live bytes, live blocks), and "the App never allocates after begin()" is asserted (allocations made
// while inside App::tick()). Each block carries a 16-byte header with its size.
static size_t gLiveBytes = 0, gLiveBlocks = 0, gTickAllocs = 0;
static bool gInTick = false;
void* operator new(size_t n) {
  void* p = malloc(n + 16);
  if (!p) throw std::bad_alloc();
  *(size_t*)p = n;
  gLiveBytes += n; gLiveBlocks++;
  if (gInTick) gTickAllocs++;
  return (char*)p + 16;
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* q) noexcept {
  if (!q) return;
  char* p = (char*)q - 16;
  gLiveBytes -= *(size_t*)p; gLiveBlocks--;
  free(p);
}
void operator delete[](void* q) noexcept { operator delete(q); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }
void operator delete[](void* q, size_t) noexcept { operator delete(q); }

static double rssMb() {
  struct rusage u;
  getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
  return u.ru_maxrss / 1048576.0;      // bytes
#else
  return u.ru_maxrss / 1024.0;         // KB
#endif
}

static std::vector<std::string> syntheticFlight() {     // 3 h from the simulator, one line per second
  std::vector<std::string> v;
  FlightSim sim;
  sim.reset(0);
  Sample s;
  int lastSec = -1;
  for (uint32_t t = 340; v.size() < 10800; t += 340)      // the simulator needs ~3 Hz steps to fly its thermals properly
    if (sim.step(t, s) && (int)(s.hh * 3600 + s.mm * 60 + s.ss) != lastSec && (lastSec = s.hh * 3600 + s.mm * 60 + s.ss, true)) v.push_back(xctrcValid(s.hh * 3600 + s.mm * 60 + s.ss, s.lat, s.lon, s.alt, s.speedKmh, s.course, s.vario));
  return v;
}

static int cmdSoak(int argc, char** argv, std::vector<std::string> (*readLines)(const std::string&)) {
  std::string file;
  int runs = 3;
  uint64_t seed = 2026;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--runs" && i + 1 < argc) runs = atoi(argv[++i]);
    else if (a == "--seed" && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 10);
    else file = a;
  }
  std::vector<std::string> L = file.empty() ? syntheticFlight() : readLines(file);
  if (L.empty()) { fprintf(stderr, "no lines\n"); return 2; }
  const std::string noFix = "$XCTRC,2026,10,15,9,52,20,0,0.000000,0.000000,128.66,0.00,0.0,0.20,,,,997.88,85*7c";
  int failures = 0;
  auto expect = [&](bool ok, const char* what) { if (!ok) { failures++; printf("  FAIL %s\n", what); } };

  static Rig r;
  Driven app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  Rng rng(seed);
  printf("soak: %zu lines (%s), %d runs, seed %llu\n", L.size(), file.empty() ? "synthetic FlightSim flight" : file.c_str(), runs, (unsigned long long)seed);

  uint32_t now = 1000;
  auto t0 = std::chrono::steady_clock::now();
  double worstTick = 0;
  uint64_t ticks = 0;
  size_t liveAfterRun1 = 0, blocksAfterRun1 = 0;
  double rssAfterRun1 = 0;
  uint32_t dropUntil = 0, nextDrop = now + 300000, nextPage = now + 120000, nextCal = now + 1500000, calEnd = 0;
  uint32_t nextSim = now + 2400000, simEnd = 0, nextPress = now + 420000;
  bool calSave = false;
  int drops = 0, pages = 0, cals = 0, sims = 0, presses = 0;
  uint32_t maxMarks = 0, maxTrail = 0, maxHist = 0;
  gTickAllocs = 0;

  for (int run = 0; run < runs; run++) {
    bool inject = run % 2 == 0;                 // alternate: laptop-injected replay, and straight through the tracer
    uint32_t tStart = now;
    printf("  run %d (%s): ", run + 1, inject ? "injected, no-fix live lines alongside" : "via tracer, no-fix lines interleaved");
    fflush(stdout);
    int refreshes0 = r.panel.refreshes;
    for (size_t i = 0; i < L.size(); i++) {
      bool dropped = now < dropUntil;
      if (!dropped) {
        if (inject) r.laptop.inject(L[i]); else r.tracer.lines.push_back(L[i]);
        if (!inject && rng.chance(30)) r.tracer.lines.push_back(noFix);
      }
      for (int k = 0; k < 100; k++, now += 10) {          // one virtual second per flight line
        if (now >= nextDrop) {                             // link drop: no lines at all for 10-60 s
          dropUntil = now + (uint32_t)rng.range(10000, 60000);
          nextDrop = dropUntil + (uint32_t)rng.range(300000, 900000);
          drops++; dropped = true; r.tracer.lines.clear();
        }
        if (!dropped && inject && k % 33 == 5) r.tracer.lines.push_back(noFix);   // live tracer without a fix, 3 Hz
        if (now >= nextPage) { Command c; c.type = Command::PAGE; c.value = rng.range(0, 3); r.laptop.cmds.push_back(c); nextPage = now + (uint32_t)rng.range(120000, 300000); pages++; }
        if (now >= nextPress) { r.input.press(now, 120); nextPress = now + (uint32_t)rng.range(300000, 600000); presses++; }
        if (now >= nextCal) {
          Command c; c.type = Command::CAL; c.value = 1; r.laptop.cmds.push_back(c);
          calEnd = now + (uint32_t)rng.range(20000, 60000); nextCal = now + 1500000 + (uint32_t)rng.range(0, 600000); cals++;
        }
        if (calEnd && now >= calEnd) { Command c; c.type = Command::CAL; c.value = calSave ? 2 : 0; r.laptop.cmds.push_back(c); calSave = !calSave; calEnd = 0; }
        if (app.calibrating()) { r.compass.psi = fmod(r.compass.psi + 3, 360); r.compass.pitch = 35 * sin(now / 900.0); r.compass.roll = 40 * cos(now / 700.0); }
        if (now >= nextSim) { r.laptop.sim(true); simEnd = now + 60000; nextSim = now + 2400000 + (uint32_t)rng.range(0, 600000); sims++; }
        if (simEnd && now >= simEnd) { r.laptop.sim(false); simEnd = 0; }
        auto a = std::chrono::steady_clock::now();
        gInTick = true;
        app.tick(now);
        gInTick = false;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        if (ms > worstTick) worstTick = ms;
        ticks++;
      }
      // once per virtual second: the bounds
      const ViewModel& vm = app.buildViewModel(now);
      static ThermalRel marks[64];
      static AirPoint trail[1024];
      uint32_t nm = (uint32_t)app.flight().thermals(marks, 64), nt = (uint32_t)app.flight().airTrail(trail, 1024);
      if (nm > maxMarks) maxMarks = nm;
      if (nt > maxTrail) maxTrail = nt;
      if ((uint32_t)app.flight().histCount() > maxHist) maxHist = (uint32_t)app.flight().histCount();
      if (vm.markCount > ViewModel::MARKS_MAX || vm.trailCount > ViewModel::TRAIL_MAX) expect(false, "view model counts above maximum");
      if (nm > (uint32_t)ThermalMemory::MAX) expect(false, "thermal marks above maximum");
      if (nt > (uint32_t)FlightModel::HIST) expect(false, "trail above maximum");
    }
    // drain: end any calibration / simulator, let the queues empty
    r.laptop.sim(false);
    { Command c; c.type = Command::CAL; c.value = 0; r.laptop.cmds.push_back(c); }
    for (int k = 0; k < 300; k++, now += 10) app.tick(now);
    r.tracer.lines.clear();
    printf("%.0f s virtual, %d panel refreshes, live heap %zu B in %zu blocks\n", (now - tStart) / 1000.0, r.panel.refreshes - refreshes0, gLiveBytes, gLiveBlocks);
    if (run == 0) { liveAfterRun1 = gLiveBytes; blocksAfterRun1 = gLiveBlocks; rssAfterRun1 = rssMb(); }
  }
  double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("soak: %llu ticks, %.1f virtual hours in %.1f s wall, worst tick %.2f ms\n", (unsigned long long)ticks, (now - 1000) / 3600000.0, wall, worstTick);
  printf("  events: %d link drops, %d page commands, %d presses, %d calibrations, %d simulator runs; lines ok %u bad %u\n",
         drops, pages, presses, cals, sims, app.linesOk(), app.linesBad());
  printf("  maxima seen: thermal marks %u (limit %d), trail %u, history %u (limit %d)\n", maxMarks, ThermalMemory::MAX, maxTrail, maxHist, FlightModel::HIST);
  printf("  memory: App::tick() made %zu heap allocations; live heap after run 1 %zu B / %zu blocks, at end %zu B / %zu blocks;\n"
         "          peak RSS (ASan build, informative only) %.0f MB after run 1, %.0f MB at end\n",
         gTickAllocs, liveAfterRun1, blocksAfterRun1, gLiveBytes, gLiveBlocks, rssAfterRun1, rssMb());
  expect(worstTick < 50.0, "a tick took 50 ms or more");
  expect(gTickAllocs == 0, "App::tick() allocated");
  if (runs > 1) {
    expect(gLiveBytes <= liveAfterRun1 + 4096, "live heap grew between run 1 and the end");
    expect(gLiveBlocks <= blocksAfterRun1 + 16, "live heap blocks grew between run 1 and the end");
  }
  // (the synthetic FlightSim flight never reaches its thermal centre, so only a real flight must produce marks)
  if (!file.empty()) expect(maxMarks > 0, "no thermal was ever remembered (the soak is not exercising anything)");
  expect(maxTrail > 0, "no trail");
  expect(app.linesOk() > L.size() * (size_t)runs / 2, "too few lines accepted");
  printf(failures ? "soak FAILED (%d)\n" : "soak OK\n", failures);
  return failures ? 1 : 0;
}
