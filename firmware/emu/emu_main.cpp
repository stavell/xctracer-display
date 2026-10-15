// PC emulator: the real core + view + app, with mock hardware.
//   emu shots  <outdir> [rot]                      render the standard scenarios
//   emu replay <lines.txt> <outdir> [options]      run lines through the whole app
//        --hz N          lines per virtual second (1 = real time for an IGC replay, 3 = tracer)
//        --inject        deliver lines as laptop commands (like igc_replay.py) instead of via the tracer
//        --mix FILE      also feed FILE's lines through the tracer at 3 Hz the whole time (a live tracer
//                        with no GPS fix next to a replay, the situation seen on the device)
//        --shot-every S  save a frame every S virtual seconds (default 30)
//        --rot R         display rotation (default 1)
//        --thermal       switch to the thermal map page (one short press) at the start
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>
#include "app/app.h"
#include "core/flight.h"
#include "core/thermal.h"
#include "mocks.h"
#include "view/screens.h"

static void watchdog(int) {
  fputs("WATCHDOG: the emulator is stuck (infinite loop?)\n", stderr);
  abort();
}

static std::vector<std::string> readLines(const std::string& path) {
  std::vector<std::string> v;
  std::ifstream f(path);
  std::string l;
  while (std::getline(f, l)) {
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
    if (!l.empty()) v.push_back(l);
  }
  return v;
}

// A view model filled from a FlightModel that has flown a drifting thermal climb (like the unit tests).
// kind 0: still circling in the lift. kind 1: left it and glided 35 s away (the thermal is behind you).
static ViewModel* thermalVm(int kind) {
  static ViewModel vm;
  static FlightModel fm;
  fm.reset();
  const double D2R = 0.017453292519943295;
  double x = 0, y = 0, hdg = 40;
  const double wx = 12 / 3.6 * sin(270 * D2R), wy = 12 / 3.6 * cos(270 * D2R);
  int total = kind == 0 ? 110 : 170;
  for (int i = 0; i < total; i++) {
    bool circling = i >= 20 && i < 110;
    double vsp = circling ? 2.6 - 0.9 * ((i / 6) % 2) : -1.1;
    if (circling) hdg += 18;
    double ax = (circling ? 30 : 38) / 3.6 * sin(hdg * D2R), ay = (circling ? 30 : 38) / 3.6 * cos(hdg * D2R);
    double gx = ax + wx, gy = ay + wy;
    x += gx; y += gy;
    Sample s;
    s.fix = true; s.lat = 43 + y / 111320.0; s.lon = 26 + x / (111320.0 * cos(43 * D2R));
    s.speedKmh = (float)(hypot(gx, gy) * 3.6); s.course = (float)fmod(atan2(gx, gy) / D2R + 360, 360);
    s.vario = (float)vsp; s.alt = (float)(1400 + i); s.battPct = 85; s.hh = 12; s.mm = i / 60; s.ss = i % 60;
    fm.feed(s);
  }
  vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED; vm.hasSaved = true; strcpy(vm.device, "XC-Tracer");
  vm.s = fm.last(); vm.avgVario = fm.avgVario(); vm.glide = fm.glide(); vm.wind = fm.wind();
  vm.localMinutes = 13 * 60 + 52; vm.localBattPct = 76;
  vm.page = ViewModel::PAGE_THERMAL;
  vm.trailCount = fm.airTrail(vm.trail, ViewModel::TRAIL_MAX);
  vm.markCount = fm.thermals(vm.marks, ViewModel::MARKS_MAX);
  return &vm;
}

static int cmdShots(const std::string& out, int rot) {
  struct Sc { const char* name; };
  const char* names[] = {"live", "nowind", "tail", "head", "big", "nofix", "status", "list", "thermal", "thermal_lost"};
  for (const char* n : names) {
    if (!strncmp(n, "thermal", 7)) {
      ViewModel* tv = thermalVm(strcmp(n, "thermal_lost") == 0 ? 1 : 0);
      MockPanel tp;
      tp.setRotation(rot);
      drawLive(tp.gfx(), *tv, 0);
      tp.dump((out + "/" + n + ".pgm").c_str());
      continue;
    }
    ViewModel vm;
    vm.fresh = true;
    vm.link = ViewModel::LINK_CONNECTED;
    vm.hasSaved = true;
    strcpy(vm.device, "XC-Tracer");
    vm.s.fix = true; vm.s.alt = 1413; vm.s.speedKmh = 41; vm.s.course = 120; vm.s.vario = 2.4f; vm.s.battPct = 85;
    vm.avgVario = 1.8f; vm.glide = 9.4f;
    vm.wind.valid = true; vm.wind.speedKmh = 14; vm.wind.toDeg = 262; vm.wind.ageS = 12;
    vm.localMinutes = 13 * 60 + 52;
    vm.localBattPct = 76;
    std::string s = n;
    if (s == "nowind") { vm.wind.valid = false; vm.s.vario = -1.1f; }
    if (s == "tail") vm.wind.toDeg = 120;
    if (s == "head") { vm.wind.toDeg = 300; vm.s.speedKmh = 28; }
    if (s == "big") { vm.s.alt = 2650; vm.s.vario = -4.2f; vm.s.speedKmh = 68; vm.glide = 12.8f; vm.avgVario = -3.0f; vm.wind.speedKmh = 38; }
    if (s == "nofix") { vm.s.fix = false; vm.s.speedKmh = NAN; vm.s.course = NAN; vm.glide = NAN; vm.wind.valid = false; }
    if (s == "status") vm.fresh = false;
    MockPanel p;
    p.setRotation(rot);
    if (s == "list") {
      ListModel lm;
      lm.count = 3; lm.selected = 1;
      strcpy(lm.items[0].name, "XC-Tracer"); lm.items[0].rssi = -52; strcpy(lm.items[0].id, "D6:75");
      strcpy(lm.items[1].name, "XC-Tracer"); lm.items[1].rssi = -83; strcpy(lm.items[1].id, "A1:0C");
      strcpy(lm.items[2].name, "Q-Series Soundbar"); lm.items[2].rssi = -78;
      drawList(p.gfx(), lm, vm, 0);
    } else {
      drawLive(p.gfx(), vm, 0);
    }
    p.dump((out + "/" + s + ".pgm").c_str());
  }
  printf("rendered %zu scenarios into %s\n", sizeof names / sizeof names[0], out.c_str());
  return 0;
}

static int cmdReplay(int argc, char** argv) {
  std::string file = argv[2], out = argv[3];
  double hz = 1;
  bool inject = false;
  std::string mix;
  int shotEvery = 30, rot = 1;
  bool thermal = false;
  for (int i = 4; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--hz" && i + 1 < argc) hz = atof(argv[++i]);
    else if (a == "--inject") inject = true;
    else if (a == "--mix" && i + 1 < argc) mix = argv[++i];
    else if (a == "--shot-every" && i + 1 < argc) shotEvery = atoi(argv[++i]);
    else if (a == "--rot" && i + 1 < argc) rot = atoi(argv[++i]);
    else if (a == "--thermal") thermal = true;
  }
  std::vector<std::string> L = readLines(file), M = mix.empty() ? std::vector<std::string>() : readLines(mix);
  if (L.empty()) { fprintf(stderr, "no lines in %s\n", file.c_str()); return 2; }

  Rig r;
  AppConfig cfg;
  cfg.rotation = rot;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.power, cfg);
  app.begin(0);
  if (thermal) r.input.press(300, 120);

  uint32_t endMs = (uint32_t)(L.size() * 1000.0 / hz) + 5000;
  size_t next = 0, mixNext = 0;
  int frames = 0;
  double worstMs = 0;
  uint32_t nonFinite[5] = {0, 0, 0, 0, 0};   // alt, vario, speed (with fix), course (with fix), glide
  uint32_t freshTicks = 0, windTicks = 0, circlingTicks = 0;
  uint32_t nextShot = shotEvery * 1000;
  for (uint32_t now = 0; now < endMs; now += 10) {
    while (next < L.size() && now >= (uint32_t)(next * 1000.0 / hz)) {
      if (inject) r.laptop.inject(L[next]);
      else r.tracer.lines.push_back(L[next]);
      next++;
    }
    while (!M.empty() && now >= mixNext * 333) { r.tracer.lines.push_back(M[mixNext % M.size()]); mixNext++; }

    auto t0 = std::chrono::steady_clock::now();
    app.tick(now);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > worstMs) worstMs = ms;

    if (now % 500 == 0) {
      ViewModel vm = app.buildViewModel(now);
      if (vm.fresh) {
        freshTicks++;
        if (!isfinite(vm.s.alt)) nonFinite[0]++;
        if (!isfinite(vm.s.vario)) nonFinite[1]++;
        if (vm.s.fix && !isfinite(vm.s.speedKmh)) nonFinite[2]++;
        if (vm.s.fix && !isfinite(vm.s.course)) nonFinite[3]++;
        if (vm.wind.valid) windTicks++;
        if (app.flight().circling()) circlingTicks++;
      }
    }
    if (now >= nextShot) {
      nextShot += shotEvery * 1000;
      char name[256];
      snprintf(name, sizeof name, "%s/frame_%04d.pgm", out.c_str(), frames++);
      r.panel.dump(name);
    }
  }
  char name[256];
  snprintf(name, sizeof name, "%s/last.pgm", out.c_str());
  r.panel.dump(name);
  printf("replayed %zu lines (%s) over %.0f virtual s\n", L.size(), inject ? "injected" : "via tracer", endMs / 1000.0);
  printf("  parsed ok %u, bad %u, panel refreshes %d, frames saved %d\n", app.linesOk(), app.linesBad(), r.panel.refreshes, frames);
  printf("  fresh samples %u: non-finite alt %u, vario %u, speed-with-fix %u, course-with-fix %u\n",
         freshTicks, nonFinite[0], nonFinite[1], nonFinite[2], nonFinite[3]);
  printf("  wind valid in %u, circling in %u of those\n", windTicks, circlingTicks);
  printf("  worst app tick %.2f ms\n", worstMs);
  return 0;
}

int main(int argc, char** argv) {
  signal(SIGALRM, watchdog);
  alarm(120);
  if (argc >= 3 && !strcmp(argv[1], "shots")) return cmdShots(argv[2], argc > 3 ? atoi(argv[3]) : 1);
  if (argc >= 4 && !strcmp(argv[1], "replay")) return cmdReplay(argc, argv);
  fputs("usage: emu shots <outdir> [rot] | emu replay <lines.txt> <outdir> [--hz N] [--inject] [--mix FILE] [--shot-every S] [--rot R]\n", stderr);
  return 2;
}
