// PC emulator: the real core + view + app, with mock hardware.
//   emu shots  <outdir> [rot]                      render the standard scenarios
//   emu replay <lines.txt> <outdir> [options]      run lines through the whole app
//        --hz N          lines per virtual second (1 = real time for an IGC replay, 3 = tracer)
//        --inject        deliver lines as laptop commands (like igc_replay.py) instead of via the tracer
//        --mix FILE      also feed FILE's lines through the tracer at 3 Hz the whole time (a live tracer
//                        with no GPS fix next to a replay, the situation seen on the device)
//        --shot-every S  save a frame every S virtual seconds (default 30)
//        --rot R         display rotation (default 1)
//        --page N        start on page N (0 compass, 1 thermal, 2 wind profile, 3 home) 
//        --thermal       same as --page 1
//   emu soak [lines.txt] [--runs N] [--seed S]     long session through the real App (about a minute; NOT part of
//        the default build.sh run): the whole flight N times (default 3, ~3 h of flight each) in virtual time as
//        fast as the PC goes, with link drops, live no-fix lines, page switches, compass calibration
//        start / save / cancel and simulator toggles. Reports wall time, worst tick and memory (every global
//        new/delete is counted); exits non-zero when an invariant breaks. See soak.h.
//        Run:  .venv/bin/python host/make_lines.py igc flight.igc > lines.txt && firmware/emu/build.sh soak lines.txt
//        Without lines.txt a synthetic 3 h flight from FlightSim is used.
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
#include "soak.h"

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
  vm.heading = vm.s.course; vm.headingSrc = HDG_GPS;
  vm.localMinutes = 13 * 60 + 52; vm.localBattPct = 76;
  vm.page = ViewModel::PAGE_THERMAL;
  vm.trailCount = fm.airTrail(vm.trail, ViewModel::TRAIL_MAX);
  vm.markCount = fm.thermals(vm.marks, ViewModel::MARKS_MAX);
  return &vm;
}

static int cmdShots(const std::string& out, int rot) {
  struct Sc { const char* name; };
  const char* names[] = {"live", "nowind", "tail", "head", "big", "nofix", "status", "list", "thermal", "thermal_lost", "mag", "calibrate",
                         "profile", "profile_short", "profile_empty", "home", "home_unreachable", "home_provisional"};
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
    vm.heading = vm.s.course; vm.headingSrc = HDG_GPS;
    std::string s = n;
    if (s == "nowind") { vm.wind.valid = false; vm.s.vario = -1.1f; }
    if (s == "tail") vm.wind.toDeg = 120;
    if (s == "head") { vm.wind.toDeg = 300; vm.s.speedKmh = 28; }
    if (s == "big") { vm.s.alt = 2650; vm.s.vario = -4.2f; vm.s.speedKmh = 68; vm.glide = 12.8f; vm.avgVario = -3.0f; vm.wind.speedKmh = 38; }
    if (s == "nofix") { vm.s.fix = false; vm.s.speedKmh = NAN; vm.s.course = NAN; vm.glide = NAN; vm.wind.valid = false; vm.heading = NAN; vm.headingSrc = HDG_NONE; }
    if (s == "mag") { vm.s.fix = false; vm.s.speedKmh = NAN; vm.s.course = NAN; vm.glide = NAN; vm.wind.valid = false; vm.heading = 123; vm.headingSrc = HDG_MAG; }
    if (s == "calibrate") { vm.calibrating = true; vm.calSpan[0] = 61; vm.calSpan[1] = 33; vm.calSpan[2] = 12; vm.fieldUt = 47; }
    if (s == "status") vm.fresh = false;
    if (s.rfind("profile", 0) == 0) {
      vm.page = ViewModel::PAGE_PROFILE;
      vm.s.alt = 1530;
      vm.profileAlt = vm.s.alt;
      // band centre, km/h, toward degrees, age s
      static const float full[][4] = {{950, 8, 250, 300}, {1050, 10, 255, 1500}, {1150, 11, 260, 1400}, {1250, 12, 262, 900},
                                      {1350, 14, 268, 700}, {1450, 17, 280, 400}, {1550, 21, 290, 200}, {1650, 24, 295, 120},
                                      {1750, 25, 300, 60}, {1850, 27, 302, 30}, {2050, 30, 310, 20}, {2150, 33, 315, 10},
                                      {2250, 36, 320, 5}, {2350, 38, 322, 3}, {2450, 40, 325, 2}};
      static const float shortR[][4] = {{1350, 14, 268, 700}, {1450, 17, 280, 400}, {1550, 21, 290, 200}, {1650, 24, 295, 120}};
      const float (*src)[4] = s == "profile_short" ? shortR : full;
      int cnt = s == "profile_short" ? 4 : 15;
      if (s == "profile_empty") cnt = 0;
      if (s == "profile_short") vm.s.alt = 1500;
      vm.profileAlt = vm.s.alt;
      for (int i = 0; i < cnt; i++) {
        vm.profile[i].altM = src[i][0]; vm.profile[i].speedKmh = src[i][1]; vm.profile[i].toDeg = src[i][2];
        vm.profile[i].ageS = (uint32_t)src[i][3]; vm.profile[i].samples = 20;
      }
      vm.profileCount = cnt;
    }
    if (s.rfind("home", 0) == 0) {
      vm.page = ViewModel::PAGE_HOME;
      vm.homeValid = true; vm.homeProvisional = s == "home_provisional";
      vm.homeDist = 3400; vm.homeBearing = 200; vm.homeRelBearing = 80; vm.homeGlideReq = 6.2f; vm.homeArrival = 350; vm.homeReachable = true;
      if (s == "home_unreachable") { vm.homeDist = 12800; vm.homeGlideReq = 18.4f; vm.homeArrival = NAN; vm.homeReachable = false; vm.homeRelBearing = -150; }
      if (s == "home_provisional") { vm.homeDist = 640; vm.homeGlideReq = NAN; vm.homeArrival = -45; vm.homeRelBearing = NAN; vm.heading = NAN; vm.headingSrc = HDG_NONE; }
    }
    MockPanel p;
    p.setRotation(rot);
    if (s == "list") {
      ListModel lm;
      lm.count = 3; lm.selected = 1;       // the calibrate row
      strcpy(lm.items[0].name, "XC-Tracer"); lm.items[0].rssi = -52; strcpy(lm.items[0].id, "D6:75");
      strcpy(lm.items[1].name, "XC-Tracer"); lm.items[1].rssi = -83; strcpy(lm.items[1].id, "A1:0C");
      strcpy(lm.items[2].name, "Q-Series Soundbar"); lm.items[2].rssi = -78; strcpy(lm.items[2].id, "00:00");
      drawList(p.gfx(), lm, vm, 0);
    } else if (s == "calibrate") {
      drawCalibrate(p.gfx(), vm, 0);
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
  int startPage = 0;
  for (int i = 4; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--hz" && i + 1 < argc) hz = atof(argv[++i]);
    else if (a == "--inject") inject = true;
    else if (a == "--mix" && i + 1 < argc) mix = argv[++i];
    else if (a == "--shot-every" && i + 1 < argc) shotEvery = atoi(argv[++i]);
    else if (a == "--rot" && i + 1 < argc) rot = atoi(argv[++i]);
    else if (a == "--thermal") startPage = 1;
    else if (a == "--page" && i + 1 < argc) startPage = atoi(argv[++i]);
  }
  std::vector<std::string> L = readLines(file), M = mix.empty() ? std::vector<std::string>() : readLines(mix);
  if (L.empty()) { fprintf(stderr, "no lines in %s\n", file.c_str()); return 2; }

  Rig r;
  AppConfig cfg;
  cfg.rotation = rot;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.power, cfg);
  app.begin(0);
  if (startPage > 0 && startPage < ViewModel::PAGE_COUNT) { Command pc; pc.type = Command::PAGE; pc.value = startPage; r.laptop.cmds.push_back(pc); }

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
  if (argc >= 2 && !strcmp(argv[1], "soak")) return cmdSoak(argc, argv, readLines);
  alarm(120);
  if (argc >= 3 && !strcmp(argv[1], "shots")) return cmdShots(argv[2], argc > 3 ? atoi(argv[3]) : 1);
  if (argc >= 4 && !strcmp(argv[1], "replay")) return cmdReplay(argc, argv);
  fputs("usage: emu shots <outdir> [rot] | emu replay <lines.txt> <outdir> [--hz N] [--inject] [--mix FILE] [--shot-every S] [--rot R]\n", stderr);
  return 2;
}
