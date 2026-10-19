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
//   emu wind <lines.txt>                           wind EKF against the circle fit over a real flight: share of seconds
//        with a valid wind, per circling episode the median of both, overall median difference.
//        Run:  .venv/bin/python host/make_lines.py igc flight.igc > lines.txt && firmware/emu/build.sh wind lines.txt
//   emu cue <lines.txt>                            hit rate of the centring cue against the next turn (see cmdCue)
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
#include <math.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>
#include "apps/vario/app/app.h"
#include "apps/vario/core/flight.h"
#include "apps/vario/core/thermal.h"
#include "apps/vario/core/wind.h"
#include "apps/vario/core/xctrc.h"
#include "mocks.h"
#include "apps/vario/view/screens.h"
#include "soak.h"
#include "platform/gfx/gray.h"
#include "platform/hw/fast_sharp.h"
#include "count.h"
#include "tetris_shots.h"
#include "sim_shots.h"
#include "box_shots.h"
#include "map_page_shots.h"
#include "intro_shots.h"

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
  int total = kind == 0 ? 110 : kind == 1 ? 170 : 150;
  const int c0 = kind == 2 ? 0 : 20, c1 = kind == 2 ? 150 : 110;   // kind 2: 2.5 min in the lift, the early circles are memory
  for (int i = 0; i < total; i++) {
    bool circling = i >= c0 && i < c1;
    double vsp = circling ? 2.6 - 0.9 * ((i / 6) % 2) : -1.1;
    if (circling) hdg += 18;
    double ax = (circling ? 30 : 38) / 3.6 * sin(hdg * D2R), ay = (circling ? 30 : 38) / 3.6 * cos(hdg * D2R);
    double gx = ax + wx, gy = ay + wy;
    x += gx; y += gy;
    if (kind == 2) { x -= 0.25; y += 0.2; }   // the unit slides across the air mass: older circles sit apart from new ones
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


// A view model filled from a FlightModel that has climbed in a leaning thermal: 9 circles, the core leaning 9 m per
// 100 m toward 70 degrees, 12 km/h of wind, the lift peaking mid-climb. kind 0: still circling (the centring page),
// 1: only 14 s of the first turn, 2: no circling at all, 3: left the lift 40 s ago, 4: circling 24 m off the core, 5: 14 s of that.
static ViewModel* climbVm(int kind) {
  static ViewModel vm;
  static FlightModel fm;
  fm.reset();
  const double D2R = 0.017453292519943295;
  const double wx = 12 / 3.6 * sin(270 * D2R), wy = 12 / 3.6 * cos(270 * D2R);
  const double sx = 0.09 * sin(70 * D2R), sy = 0.09 * cos(70 * D2R);
  double hdg = 40, z = 1400, t = 0;
  const int circlingSeconds = kind == 2 ? 0 : (kind == 1 || kind == 5) ? 14 : 9 * 24, glide = kind == 3 ? 40 : (kind == 2 ? 120 : 0);
  double lastX = 0, lastY = 0;
  for (int i = 0; i < 20 + circlingSeconds + glide; i++, t += 1) {
    const bool circling = i >= 20 && i < 20 + circlingSeconds;
    double px, py;
    float v;
    if (circling) {
      const double R = 9.0 / (15 * D2R);
      const double off = kind >= 4 ? 24.0 : 0.0;                    // 4, 5: the circle sits 24 m east of the core
      px = sx * (z - 1400) - R * sin(hdg * D2R + 1.5707963) + off;
      py = sy * (z - 1400) - R * cos(hdg * D2R + 1.5707963);
      const double r = hypot(px - sx * (z - 1400), py - sy * (z - 1400));
      const float peak = 3.4f - 0.015f * fabsf((float)(z - 1560));
      v = peak * expf(-(float)(r * r) / (2 * 55 * 55)) + 0.15f * (float)sin(i * 0.7);
      hdg += 15;
    } else {
      px = lastX + 9 * sin(hdg * D2R);
      py = lastY + 9 * cos(hdg * D2R);
      v = -1.1f;
    }
    lastX = px; lastY = py;
    Sample s;
    const double gx = px + wx * t, gy = py + wy * t;
    s.fix = true; s.lat = 43 + gy / 111320.0; s.lon = 26 + gx / (111320.0 * cos(43 * D2R));
    s.alt = (float)z; s.vario = v; s.battPct = 85; s.hh = 12; s.mm = i / 60; s.ss = i % 60;
    { const double vx = 9 * sin(hdg * D2R) + wx, vy = 9 * cos(hdg * D2R) + wy;      // ground velocity of a circle in the wind
      s.speedKmh = (float)(hypot(vx, vy) * 3.6); s.course = (float)fmod(atan2(vx, vy) / D2R + 360, 360); }
    fm.feed(s);
    z += v;
  }
  vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED; vm.hasSaved = true; strcpy(vm.device, "XC-Tracer");
  vm.s = fm.last(); vm.avgVario = fm.avgVario(); vm.glide = fm.glide(); vm.wind = fm.wind();
  vm.wind.valid = true; vm.wind.speedKmh = 12; vm.wind.toDeg = 270;       // the model's own estimate needs more circles than the page cares about
  vm.heading = vm.s.course; vm.headingSrc = HDG_GPS;
  vm.localMinutes = 13 * 60 + 52; vm.localBattPct = 76;
  vm.page = ViewModel::PAGE_CENTER;
  vm.col = &fm.column();
  return &vm;
}

// gray.png: a 17-step grey ramp, two touching half-grey rectangles (no seam), and a dithered gradient disc
// drawn with blitGray over a grey backdrop (nibble 0 is transparent).
static void renderGray(const std::string& out, int rot) {
  static FastSharp disp(nullptr, 0, 400, 240, 0);
  disp.setRotation((uint8_t)rot);
  disp.clearDisplay();
  Bitmap1 bm{disp.getBuffer(), 400, 240, 50, (uint8_t)rot};
  for (int i = 0; i <= 16; i++) fillRect(bm, 10 + i * 12, 10, 12, 40, Gray(i));
  bm.rectIncl(9, 9, 9 + 17 * 12 + 1, 9, DRAW_CLEAR);
  bm.rectIncl(9, 51, 9 + 17 * 12 + 1, 51, DRAW_CLEAR);
  fillRect(bm, 11, 62, 90, 24, Gray(8));
  fillRect(bm, 101, 62, 90, 24, Gray(8));
  fillRect(bm, 11, 90, 180, 6, Gray(3));
  static uint8_t disc[64 * 32];
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      float d = sqrtf((x - 31.5f) * (x - 31.5f) + (y - 31.5f) * (y - 31.5f)) / 31.5f;
      int v = d > 1 ? 0 : 1 + (int)(14 * (1 - d));
      uint8_t& b = disc[y * 32 + x / 2];
      b = (x & 1) ? (uint8_t)((b & 0xF0) | v) : (uint8_t)((b & 0x0F) | (v << 4));
    }
  fillRect(bm, 10, 110, 190, 100, Gray(4));
  Gray4 g{disc, 64, 64, 32};
  blitGray(bm, 20, 125, g, 0, 0, 64, 64, 0);
  blitGray(bm, 120, 125, g, 0, 0, 64, 64, -1);     // opaque: the corners come out dithered as well
  disp.dumpPgm((out + "/gray.pgm").c_str());
}

static bool gCountMode = false;

static int cmdShots(const std::string& out, int rot) {
  if (!gCountMode) { ts::shots(out.c_str()); bs::shots(out.c_str()); ms::shots(out.c_str()); simshots::fixtureShots(out.c_str()); is::shots(out.c_str()); }
  struct Sc { const char* name; };
  const char* names[] = {"live", "nowind", "tail", "head", "big", "nofix", "status", "list", "thermal", "thermal_lost", "thermal_memory", "mag",
                         "center", "center_first", "center_none", "center_left", "center_off", "center_half", "profile", "profile_short", "profile_empty", "profile_temp", "profile_inversion", "profile_temp_only", "home", "home_unreachable", "home_provisional", "standalone", "standalone_nodata"};
  for (const char* n : names) {
    if (gCountMode && strcmp(n, "live") && strcmp(n, "nofix") && strcmp(n, "mag") && strcmp(n, "standalone") &&
        strcmp(n, "standalone_nodata") && strncmp(n, "thermal", 7) && strncmp(n, "center", 6)) continue;
    if (!strncmp(n, "center", 6)) {
      ViewModel* cv = climbVm(!strcmp(n, "center_first") ? 1 : !strcmp(n, "center_none") ? 2 : !strcmp(n, "center_left") ? 3 : !strcmp(n, "center_off") ? 4 : !strcmp(n, "center_half") ? 5 : 0);
      if (gCountMode) {
        static CountingSharp cc;
        cc.setRotation((uint8_t)rot);
        countReport(n, cc, [&] { Bitmap1 bm = cc.bm(); drawLive(cc, *cv, 0, &bm); });
        continue;
      }
      MockPanel cp;
      cp.setRotation(rot);
      Bitmap1 cbm = cp.bitmap();
      drawLive(cp.gfx(), *cv, 0, &cbm);
      cp.dump((out + "/" + n + ".pgm").c_str());
      continue;
    }
    if (!strncmp(n, "thermal", 7)) {
      if (gCountMode) {
        ViewModel* tv = thermalVm(0);
        static CountingSharp ct;
        ct.setRotation((uint8_t)rot);
        countReport(n, ct, [&] { Bitmap1 bm = ct.bm(); drawLive(ct, *tv, 0, &bm); });
        continue;
      }
      ViewModel* tv = thermalVm(strcmp(n, "thermal_lost") == 0 ? 1 : strcmp(n, "thermal_memory") == 0 ? 2 : 0);
      MockPanel tp;
      tp.setRotation(rot);
      Bitmap1 tbm = tp.bitmap();
      drawLive(tp.gfx(), *tv, 0, &tbm);
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
    if (s == "status") vm.fresh = false;
    if (s.rfind("standalone", 0) == 0) {
      vm.fresh = false; vm.link = ViewModel::LINK_SEARCHING;
      vm.heading = 123; vm.headingSrc = HDG_MAG; vm.ambientC = 21.4f; vm.humidityPct = 48;
      if (s == "standalone_nodata") { vm.heading = NAN; vm.headingSrc = HDG_NONE; vm.ambientC = NAN; vm.humidityPct = NAN; }
    }
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
      const bool temps = s == "profile_temp" || s == "profile_inversion" || s == "profile_temp_only";
      if (s == "profile_short") vm.s.alt = 1500;
      vm.profileAlt = vm.s.alt;
      for (int i = 0; i < cnt; i++) {
        vm.profile[i].altM = src[i][0]; vm.profile[i].speedKmh = src[i][1]; vm.profile[i].toDeg = src[i][2];
        vm.profile[i].ageS = (uint32_t)src[i][3]; vm.profile[i].samples = 20;
        vm.profile[i].tempC = NAN; vm.profile[i].tempSamples = 0;
        if (temps) {            // 18 C at 950 m, -0.65 C per 100 m; the inversion adds 2 C from 1550 m up
          float t = 18.0f - 0.65f * (src[i][0] - 950) / 100.0f;
          if (s == "profile_inversion" && src[i][0] > 1500 && src[i][0] < 1700) t += 2.0f * (src[i][0] - 1500) / 200.0f;
          else if (s == "profile_inversion" && src[i][0] >= 1700) t += 2.0f;
          vm.profile[i].tempC = t; vm.profile[i].tempSamples = 12;
        }
        if (s == "profile_temp_only") { vm.profile[i].speedKmh = NAN; vm.profile[i].toDeg = NAN; vm.profile[i].samples = 0; }
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
      lm.count = 3; lm.selected = 1;       // the second tracer
      strcpy(lm.items[0].name, "XC-Tracer"); lm.items[0].rssi = -52; strcpy(lm.items[0].id, "D6:75");
      strcpy(lm.items[1].name, "XC-Tracer"); lm.items[1].rssi = -83; strcpy(lm.items[1].id, "A1:0C");
      strcpy(lm.items[2].name, "Q-Series Soundbar"); lm.items[2].rssi = -78; strcpy(lm.items[2].id, "00:00");
      Bitmap1 bm = p.bitmap();
      drawList(p.gfx(), lm, vm, 0, &bm);
    } else if (gCountMode) {
      static CountingSharp cd;
      cd.setRotation((uint8_t)rot);
      countReport(n, cd, [&] { Bitmap1 bm = cd.bm(); drawLive(cd, vm, 0, &bm); });
      continue;
    } else {
      Bitmap1 bm = p.bitmap();
      drawLive(p.gfx(), vm, 0, &bm);
    }
    p.dump((out + "/" + s + ".pgm").c_str());
  }
  renderGray(out, rot);
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
  std::string press;
  for (int i = 4; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--hz" && i + 1 < argc) hz = atof(argv[++i]);
    else if (a == "--inject") inject = true;
    else if (a == "--mix" && i + 1 < argc) mix = argv[++i];
    else if (a == "--shot-every" && i + 1 < argc) shotEvery = atoi(argv[++i]);
    else if (a == "--rot" && i + 1 < argc) rot = atoi(argv[++i]);
    else if (a == "--thermal") startPage = 1;
    else if (a == "--page" && i + 1 < argc) startPage = atoi(argv[++i]);
    else if (a == "--press" && i + 1 < argc) press = argv[++i];
  }
  std::vector<std::string> L = readLines(file), M = mix.empty() ? std::vector<std::string>() : readLines(mix);
  if (L.empty()) { fprintf(stderr, "no lines in %s\n", file.c_str()); return 2; }

  Rig r;
  AppConfig cfg;
  cfg.rotation = rot;
  Driven app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power, cfg);
  app.begin(0);
  if (startPage > 0 && startPage < ViewModel::PAGE_COUNT) { Command pc; pc.type = Command::PAGE; pc.value = startPage; r.laptop.cmds.push_back(pc); }

  // --press at:dur,at:dur (ms): scripted presses of the single switch
  for (size_t i = 0; i < press.size();) {
    uint32_t at = (uint32_t)strtoul(press.c_str() + i, nullptr, 10);
    size_t c = press.find(':', i);
    if (c == std::string::npos) break;
    uint32_t dur = (uint32_t)strtoul(press.c_str() + c + 1, nullptr, 10);
    r.input.press(at, dur);
    size_t n = press.find(',', c);
    if (n == std::string::npos) break;
    i = n + 1;
  }

  uint32_t endMs = (uint32_t)(L.size() * 1000.0 / hz) + 5000;
  // Behaviour digest: when each present() happened and what the frame buffer held at that moment.
  uint64_t digest = 1469598103934665603ull;
  int lastPresents = 0;
  auto fold = [&](const uint8_t* p, size_t n) { for (size_t k = 0; k < n; k++) { digest ^= p[k]; digest *= 1099511628211ull; } };
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
    if (r.panel.presents != lastPresents) {
      lastPresents = r.panel.presents;
      fold((const uint8_t*)&now, sizeof now);
      fold(r.panel.raw(), 50 * 240);
    }

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
  printf("  presents %d, behaviour digest %016llx (present times + frame contents)\n", r.panel.presents, (unsigned long long)digest);
  printf("  fresh samples %u: non-finite alt %u, vario %u, speed-with-fix %u, course-with-fix %u\n",
         freshTicks, nonFinite[0], nonFinite[1], nonFinite[2], nonFinite[3]);
  printf("  wind valid in %u, circling in %u of those\n", windTicks, circlingTicks);
  printf("  worst app tick %.2f ms\n", worstMs);
  return 0;
}

// emu wind: EKF against the circle fit over a real flight
static double med(std::vector<double> v) {
  if (v.empty()) return NAN;
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
static double circMean(const std::vector<double>& deg) {
  double s = 0, c = 0;
  for (double d : deg) { s += sin(d * M_PI / 180); c += cos(d * M_PI / 180); }
  return deg.empty() ? NAN : fmod(atan2(s, c) * 180 / M_PI + 360, 360);
}
static double angDiffDeg(double a, double b) { return fabs(fmod(a - b + 540, 360) - 180); }

struct Episode { std::vector<double> es, ed, cs, cd; int secs = 0; uint32_t start = 0; };


// emu cue <lines.txt> [gain...]: how good is the centring cue (ThermalColumn::cue) on a real flight? Every second
// of circling it records the cue (where the core is, how far to shift the circle). One turn later, when the pilot
// has flown the next turn (without having been shown the cue), it compares with what happened:
//  * lift: the lift-weighted centroid of the next turn lies on the side of its own centre the cue pointed to
//          (angle between the two within 90 degrees; angle error and distance error reported)
//  * pilot: the pilot's own re-centring (next turn's centre minus this turn's) agrees with the cue
//  * pay: next turn's mean climb minus this one's, when the pilot moved toward the cue and when against it
// baselines: the last turn's raw lift centroid alone (no windows, no gain, no lean).
static int cmdCue(int argc, char** argv) {
  std::vector<std::string> L = readLines(argv[2]);
  if (L.empty()) return 2;
  for (int i = 3; i + 1 < argc; i += 2) {                         // --gain G --lean 0|1 --w a,b,c
    if (!strcmp(argv[i], "--gain")) ThermalColumn::cueGain = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--lean")) ThermalColumn::cueLean = atoi(argv[i + 1]) != 0;
    else if (!strcmp(argv[i], "--turns")) ThermalColumn::cueMinTurns = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--max")) ThermalColumn::cueMaxM = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--swin")) ThermalColumn::secWinDeg = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--sspread")) ThermalColumn::secMaxSpreadM = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--sshift")) ThermalColumn::secMinShiftM = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--sagree")) ThermalColumn::secMaxAgreeDeg = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--smin")) ThermalColumn::secMinDeg = (float)atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--w")) sscanf(argv[i + 1], "%f,%f,%f", &ThermalColumn::cueW[0], &ThermalColumn::cueW[1], &ThermalColumn::cueW[2]);
  }
  struct Rec {
    double t, gx, gy;                  // seconds, ground position of the newest point (m from the first fix)
    float cx, cy, x, y, rawX, rawY, sx, sy, shiftM, spread, climb, turnS;
    float we, wn;                      // wind (m/s) of the analysis
    SectorCue sc;                      // the sector cue of this second (before any gate of the tool)
    bool valid;
    float bsx, bsy;                    // baseline shift: raw lift centroid - centre
  };
  static FlightModel fm;
  fm.reset();
  std::vector<Rec> R;
  double lat0 = 0, lon0 = 0, kx = 111320;
  bool have = false;
  double tSec = 0;
  for (const std::string& l : L) {
    Sample s;
    if (!xctrcParse(l.c_str(), s)) continue;
    if (!fm.feed(s)) continue;
    tSec += 1;
    if (!s.fix) continue;
    if (!have) { have = true; lat0 = s.lat; lon0 = s.lon; kx = 111320.0 * cos(lat0 * 0.017453292519943295); }
    const ThermalColumn& c = fm.column();
    Rec r{};
    r.t = tSec;
    const TrackPoint& h = fm.hist(fm.histCount() - 1);
    r.gx = (h.lon - lon0) * kx; r.gy = (h.lat - lat0) * 111320.0;
    Wind w = fm.wind();
    if (w.valid) { r.we = w.speedKmh / 3.6f * sinf(w.toDeg * 0.017453292f); r.wn = w.speedKmh / 3.6f * cosf(w.toDeg * 0.017453292f); }
    r.sc = c.sec;
    r.valid = c.cue.valid;
    if (r.valid) {
      const CoreCue& q = c.cue;
      r.cx = q.cx; r.cy = q.cy; r.x = q.x; r.y = q.y; r.rawX = q.rawX; r.rawY = q.rawY; r.sx = q.shiftX; r.sy = q.shiftY;
      r.shiftM = q.shiftM; r.spread = q.spread; r.climb = q.meanClimb; r.turnS = q.turnS;
      r.bsx = q.rawX - q.cx; r.bsy = q.rawY - q.cy;
    }
    R.push_back(r);
  }
  int nValid = 0;
  for (const Rec& r : R) if (r.valid) nValid++;
  printf("%zu seconds, cue available in %d (%.0f%%)\n", R.size(), nValid, 100.0 * nValid / (R.empty() ? 1 : R.size()));
  // statistics over (record, record one turn later) pairs
  struct Acc { int n = 0, hit = 0; std::vector<double> ang, err; };
  Acc lift, liftBase, pilot, pilotBase;
  std::vector<double> payToward, payAgainst, shiftSizes, spreads;
  int pairs = 0;
  for (size_t i = 0; i < R.size(); i++) {
    const Rec& a = R[i];
    if (!a.valid) continue;
    size_t j = i + (size_t)lroundf(a.turnS);
    if (j >= R.size() || !R[j].valid || R[j].t - a.t > a.turnS + 3.0) continue;
    const Rec& b = R[j];
    pairs++;
    // predicted core in the frame of b: move by the newest point's ground shift and the wind drift in between
    const double dx = a.gx - b.gx + b.we * (b.t - a.t), dy = a.gy - b.gy + b.wn * (b.t - a.t);
    const double px = a.x + dx, py = a.y + dy;                          // predicted core, frame of b
    const double acx = a.cx + dx, acy = a.cy + dy;                      // this turn's centre, frame of b
    shiftSizes.push_back(a.shiftM);
    spreads.push_back(a.spread);
    // lift: where the next turn's own lift centroid lies relative to its own centre
    const double lx = b.rawX - b.cx, ly = b.rawY - b.cy;
    const double lm = hypot(lx, ly);
    if (a.shiftM >= 10 && lm >= 3) {
      auto ang = [](double x1, double y1, double x2, double y2) { return fabs(atan2(x1 * y2 - y1 * x2, x1 * x2 + y1 * y2)) * 57.29578; };
      const double A = ang(a.sx, a.sy, lx, ly);
      lift.n++; if (A < 90) lift.hit++; lift.ang.push_back(A);
      // distance error: predicted core vs where the next turn's lift is (centre + gain * offset, same gain)
      lift.err.push_back(hypot(px - (b.cx + ThermalColumn::cueGain * lx), py - (b.cy + ThermalColumn::cueGain * ly)));
    }
    if (hypot(a.bsx, a.bsy) >= 10 && lm >= 3) {
      auto ang = [](double x1, double y1, double x2, double y2) { return fabs(atan2(x1 * y2 - y1 * x2, x1 * x2 + y1 * y2)) * 57.29578; };
      const double A = ang(a.bsx, a.bsy, lx, ly);
      liftBase.n++; if (A < 90) liftBase.hit++; liftBase.ang.push_back(A);
      liftBase.err.push_back(hypot(acx + a.bsx * ThermalColumn::cueGain - (b.cx + ThermalColumn::cueGain * lx), acy + a.bsy * ThermalColumn::cueGain - (b.cy + ThermalColumn::cueGain * ly)));
    }
    // pilot: his own re-centring
    const double mx = b.cx - acx, my = b.cy - acy, mm = hypot(mx, my);
    if (a.shiftM >= 10 && mm >= 8) {
      const double A = fabs(atan2(a.sx * my - a.sy * mx, a.sx * mx + a.sy * my)) * 57.29578;
      pilot.n++; if (A < 90) pilot.hit++; pilot.ang.push_back(A);
      pilot.err.push_back(hypot(px - b.cx, py - b.cy));                  // predicted core vs the pilot's new centre
      const double cosA = cos(A / 57.29578);
      if (cosA > 0.5) payToward.push_back(b.climb - a.climb);
      if (cosA < -0.5) payAgainst.push_back(b.climb - a.climb);
    }
    if (hypot(a.bsx, a.bsy) >= 10 && mm >= 8) {
      const double A = fabs(atan2(a.bsx * my - a.bsy * mx, a.bsx * mx + a.bsy * my)) * 57.29578;
      pilotBase.n++; if (A < 90) pilotBase.hit++; pilotBase.ang.push_back(A);
    }
  }
  auto med = [](std::vector<double> v) -> double { if (v.empty()) return NAN; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
  auto mean = [](const std::vector<double>& v) -> double { double s = 0; for (double x : v) s += x; return v.empty() ? NAN : s / v.size(); };
  printf("pairs (a cue and the turn after it): %d; median cue %.0f m, median spread %.0f m\n", pairs, med(shiftSizes), med(spreads));
  printf("cue vs the next turn's lift:   n %d, hit rate (within 90 deg) %.0f%%, median angle error %.0f deg, median distance error %.0f m\n",
         lift.n, 100.0 * lift.hit / (lift.n ? lift.n : 1), med(lift.ang), med(lift.err));
  printf("  baseline (last turn only):   n %d, hit rate %.0f%%, median angle error %.0f deg, median distance error %.0f m\n",
         liftBase.n, 100.0 * liftBase.hit / (liftBase.n ? liftBase.n : 1), med(liftBase.ang), med(liftBase.err));
  printf("cue vs the pilot's re-centring: n %d, agree (within 90 deg) %.0f%%, median angle %.0f deg, median distance cue-core to new centre %.0f m\n",
         pilot.n, 100.0 * pilot.hit / (pilot.n ? pilot.n : 1), med(pilot.ang), med(pilot.err));
  printf("  baseline (last turn only):   n %d, agree %.0f%%\n", pilotBase.n, 100.0 * pilotBase.hit / (pilotBase.n ? pilotBase.n : 1));
  printf("next turn's climb minus this one's: pilot moved toward the cue n %zu mean %+.2f m/s; against it n %zu mean %+.2f m/s\n",
         payToward.size(), mean(payToward), payAgainst.size(), mean(payAgainst));

  // ---- the sector cue (SectorCue): coverage of the circling seconds and hit rate against the next full turn.
  // Circling seconds = the seconds of a circling run that reached 270 degrees of turn, found afterwards.
  std::vector<char> circ(R.size(), 0);
  for (size_t i = 0; i < R.size();) {
    if (R[i].sc.runDeg <= 0) { i++; continue; }
    size_t e = i;
    float mx = 0;
    while (e < R.size() && R[e].sc.runDeg > 0 && (e == i || R[e].sc.runDeg >= R[e - 1].sc.runDeg - 1.0f || R[e].t - R[e - 1].t > 5)) { mx = fmaxf(mx, R[e].sc.runDeg); e++; }
    if (mx >= 270) for (size_t k = i; k < e; k++) circ[k] = 1;
    i = e;
  }
  int nCirc = 0, nOld = 0;
  for (size_t i = 0; i < R.size(); i++) if (circ[i]) { nCirc++; if (R[i].valid) nOld++; }
  printf("\nSECTOR CUE  circling seconds %d; old cue (two full turns, metres) in %d of them = %.0f%%\n", nCirc, nOld, 100.0 * nOld / (nCirc ? nCirc : 1));
  auto angD = [](double x1, double y1, double x2, double y2) { return fabs(atan2(x1 * y2 - y1 * x2, x1 * x2 + y1 * y2)) * 57.29578; };
  struct Gate { float minShift, maxSpread, maxAgree; int minConf; };
  struct Res { int shown = 0, n = 0, hit = 0, hitQ = 0, nW = 0; };
  // evaluate(gate, partialOnly: 0 all, 1 only partial windows, 2 only full)
  auto evaluate = [&](const Gate& g, int which) {
    Res res;
    for (size_t i = 0; i < R.size(); i++) {
      const Rec& a = R[i];
      if (!circ[i] || !a.sc.cand) continue;
      const bool part = a.sc.conf == 1 && a.sc.spread < 0;
      if (which == 1 && !part) continue;
      if (which == 2 && part) continue;
      if (a.sc.shiftM < g.minShift) continue;
      if (a.sc.spread >= 0 && a.sc.spread > g.maxSpread) continue;
      if (a.sc.agreeDeg >= 0 && a.sc.agreeDeg > g.maxAgree) continue;
      if (a.sc.conf < g.minConf) continue;
      res.shown++;
      size_t j = i + (size_t)lroundf(a.sc.periodS);
      if (j >= R.size() || R[j].t - a.t > a.sc.periodS + 3.0 || !R[j].sc.fullValid) continue;
      const Rec& b = R[j];
      if (hypot(b.sc.fullX, b.sc.fullY) < 3.0) continue;
      res.n++;
      if (angD(a.sc.shiftM * 0 + sin(a.sc.deg * 0.0174533), cos(a.sc.deg * 0.0174533), b.sc.fullX, b.sc.fullY) < 90) res.hit++;
      const double qd = a.sc.sector * 45.0 * 0.0174533;
      if (angD(sin(qd), cos(qd), b.sc.fullX, b.sc.fullY) < 90) res.hitQ++;
    }
    return res;
  };
  auto line = [&](const char* name, const Res& r) {
    printf("  %-34s cue in %5.1f%% of circling secs, n %4d, hit %5.1f%% (sector %5.1f%%)\n", name, 100.0 * r.shown / (nCirc ? nCirc : 1), r.n,
           100.0 * r.hit / (r.n ? r.n : 1), 100.0 * r.hitQ / (r.n ? r.n : 1));
  };
  // baseline: the last full turn's raw lift offset (>= 10 m), direction unquantised, against the next turn's
  {
    Res b0;
    for (size_t i = 0; i < R.size(); i++) {
      const Rec& a = R[i];
      if (!circ[i] || !a.sc.fullValid || hypot(a.sc.fullX, a.sc.fullY) < 10) continue;
      b0.shown++;
      const double tp = a.turnS > 0 ? a.turnS : 26;
      size_t j = i + (size_t)lroundf((float)tp);
      if (j >= R.size() || !R[j].sc.fullValid || hypot(R[j].sc.fullX, R[j].sc.fullY) < 3.0) continue;
      b0.n++;
      if (angD(a.sc.fullX, a.sc.fullY, R[j].sc.fullX, R[j].sc.fullY) < 90) b0.hit++;
    }
    line("baseline: last turn raw (>=10 m)", b0);
  }
  Gate open{0, 1e9f, 181, 0};
  line("ungated, all windows", evaluate(open, 0));
  line("ungated, partial windows only", evaluate(open, 1));
  line("ungated, full turns only", evaluate(open, 2));
  for (int cf = 1; cf <= 3; cf++) { char nm[40]; snprintf(nm, sizeof nm, "confidence >= %d", cf); Gate g = open; g.minConf = cf; line(nm, evaluate(g, 0)); }
  printf(" gate: centred (pushed core nearer than X m to the centre)\n");
  for (float m : {3.f, 5.f, 8.f, 10.f, 15.f, 20.f, 25.f, 30.f, 40.f, 50.f, 70.f}) { char nm[40]; snprintf(nm, sizeof nm, "min shift %.0f m", m); Gate g = open; g.minShift = m; line(nm, evaluate(g, 0)); }
  printf(" gate: broken thermal (older turns' spread above X m; only where an older full turn exists)\n");
  for (float m : {100.f, 80.f, 60.f, 50.f, 40.f, 30.f, 20.f}) { char nm[40]; snprintf(nm, sizeof nm, "max spread %.0f m", m); Gate g = open; g.maxSpread = m; line(nm, evaluate(g, 0)); }
  printf(" gate: previous turn's direction differs by more than X deg\n");
  for (float m : {120.f, 90.f, 60.f, 45.f}) { char nm[40]; snprintf(nm, sizeof nm, "max disagreement %.0f deg", m); Gate g = open; g.maxAgree = m; line(nm, evaluate(g, 0)); }
  printf(" gate: mean climb of the window at least X m/s\n");
  for (float m : {0.5f, 1.0f, 1.5f, 2.0f}) {
    Res r; // climb gate needs the window climb: evaluate inline
    for (size_t i = 0; i < R.size(); i++) {
      const Rec& a = R[i];
      if (!circ[i] || !a.sc.cand || a.sc.climb < m) continue;
      r.shown++;
      size_t j = i + (size_t)lroundf(a.sc.periodS);
      if (j >= R.size() || R[j].t - a.t > a.sc.periodS + 3.0 || !R[j].sc.fullValid || hypot(R[j].sc.fullX, R[j].sc.fullY) < 3.0) continue;
      r.n++;
      if (angD(sin(a.sc.deg * 0.0174533), cos(a.sc.deg * 0.0174533), R[j].sc.fullX, R[j].sc.fullY) < 90) r.hit++;
      const double qd = a.sc.sector * 45.0 * 0.0174533;
      if (angD(sin(qd), cos(qd), R[j].sc.fullX, R[j].sc.fullY) < 90) r.hitQ++;
    }
    char nm[40]; snprintf(nm, sizeof nm, "mean climb >= %.1f", m); line(nm, r);
  }
  printf(" gate: min shift x max spread\n");
  for (float m : {5.f, 10.f}) for (float sp : {60.f, 40.f, 30.f}) {
    char nm[40]; snprintf(nm, sizeof nm, "shift>=%.0f spread<=%.0f", m, sp); Gate g = open; g.minShift = m; g.maxSpread = sp; line(nm, evaluate(g, 0));
  }
  {
    printf(" the shipped rule (ThermalColumn::secMinShiftM / secMaxSpreadM / secMaxAgreeDeg): shift >= %.0f m, spread <= %.0f m, disagreement <= %.0f deg\n",
           ThermalColumn::secMinShiftM, ThermalColumn::secMaxSpreadM, ThermalColumn::secMaxAgreeDeg);
    Gate g{ThermalColumn::secMinShiftM, ThermalColumn::secMaxSpreadM, ThermalColumn::secMaxAgreeDeg, 0};
    line("  SHIPPED, all windows", evaluate(g, 0));
    line("  SHIPPED, partial turn only", evaluate(g, 1));
    line("  SHIPPED, full turn only", evaluate(g, 2));
    {                                   // the first two turns of a climb: where the old cue was absent by construction
      int nEarly = 0, shown = 0, n = 0, hit = 0, oldOnly = 0;
      for (size_t i = 0; i < R.size(); i++) {
        const Rec& a = R[i];
        if (!circ[i] || a.sc.runDeg >= 720) continue;
        nEarly++;
        if (R[i].valid) oldOnly++;
        if (!a.sc.cand || a.sc.shiftM < g.minShift || (a.sc.spread >= 0 && a.sc.spread > g.maxSpread) || (a.sc.agreeDeg >= 0 && a.sc.agreeDeg > g.maxAgree)) continue;
        shown++;
        size_t j = i + (size_t)lroundf(a.sc.periodS);
        if (j >= R.size() || R[j].t - a.t > a.sc.periodS + 3.0 || !R[j].sc.fullValid || hypot(R[j].sc.fullX, R[j].sc.fullY) < 3.0) continue;
        n++;
        const double qd = a.sc.sector * 45.0 * 0.0174533;
        if (angD(sin(qd), cos(qd), R[j].sc.fullX, R[j].sc.fullY) < 90) hit++;
      }
      printf("  first two turns of a climb: %d circling secs, new cue in %d (%.0f%%), n %d, hit %.1f%% (sector); old cue in %d\n",
             nEarly, shown, 100.0 * shown / (nEarly ? nEarly : 1), n, 100.0 * hit / (n ? n : 1), oldOnly);
    }
    {                                   // the old cue's own sample: shift >= 10 m, as in the hit-rate lines above
      int k = 0;
      for (size_t i = 0; i < R.size(); i++) if (circ[i] && R[i].valid && R[i].shiftM >= 10) k++;
      printf("  old cue with its >= 10 m rule: %.1f%% of circling secs\n", 100.0 * k / (nCirc ? nCirc : 1));
    }
    int nAll = (int)R.size();
    Res r = evaluate(g, 0);
    printf("  = %.1f%% of all flight seconds (the review's coverage basis; the old two-turn cue: %.1f%%)\n", 100.0 * r.shown / (nAll ? nAll : 1), 100.0 * nValid / (nAll ? nAll : 1));
  }
  return 0;
}

static int cmdWind(const char* file) {
  std::vector<std::string> L = readLines(file);
  if (L.empty()) { fprintf(stderr, "no lines in %s\n", file); return 2; }
  static FlightModel fm;
  fm.reset();
  unsigned secs = 0, flySecs = 0, circleValid = 0, circleFresh = 0, ekfConf = 0, combined = 0, circlingSecs = 0;
  unsigned flyCircle = 0, flyFresh = 0, flyEkf = 0, flyCombined = 0;
  std::vector<Episode> eps;
  uint32_t lastCirc = 0, t = 0;
  std::vector<double> dSpeed, dDir, dVec;
  for (const std::string& l : L) {
    Sample s;
    if (!xctrcParse(l.c_str(), s)) continue;
    if (!fm.feed(s)) continue;
    t++;
    secs++;
    Wind cw = fm.circleWind(), w = fm.wind();
    bool cf = cw.valid && cw.ageS <= 60, ec = fm.ekfConfident();
    if (cw.valid) circleValid++;
    if (cf) circleFresh++;
    if (ec) ekfConf++;
    if (w.valid) combined++;
    if (fm.flying()) { flySecs++; flyCircle += cw.valid; flyFresh += cf; flyEkf += ec; flyCombined += w.valid; }
    double ew = hypot(fm.ekf().wx(), fm.ekf().wy()) * 3.6, ed = fmod(atan2(fm.ekf().wx(), fm.ekf().wy()) * 180 / M_PI + 360, 360);
    if (ec && cf) {
      dSpeed.push_back(fabs(ew - cw.speedKmh));
      dDir.push_back(angDiffDeg(ed, cw.toDeg));
      double ex = fm.ekf().wx(), ey = fm.ekf().wy(), cwr = cw.toDeg * M_PI / 180;
      dVec.push_back(hypot(ex - cw.speedKmh / 3.6 * sin(cwr), ey - cw.speedKmh / 3.6 * cos(cwr)) * 3.6);
    }
    if (fm.circling()) {
      circlingSecs++;
      if (eps.empty() || t - lastCirc > 60) { eps.push_back(Episode()); eps.back().start = t; }
      lastCirc = t;
      Episode& e = eps.back();
      e.secs++;
      if (ec) { e.es.push_back(ew); e.ed.push_back(ed); }
      if (cw.valid) { e.cs.push_back(cw.speedKmh); e.cd.push_back(cw.toDeg); }
    }
  }
  auto pct = [&](unsigned n, unsigned d) { return d ? 100.0 * n / d : 0.0; };
  printf("data seconds %u, flying %u, circling %u\n", secs, flySecs, circlingSecs);
  printf("valid wind, all seconds : circle fit %.1f %% (age <= 60 s: %.1f %%), EKF confident %.1f %%, combined wind() %.1f %%\n",
         pct(circleValid, secs), pct(circleFresh, secs), pct(ekfConf, secs), pct(combined, secs));
  printf("valid wind, flying only : circle fit %.1f %% (age <= 60 s: %.1f %%), EKF confident %.1f %%, combined wind() %.1f %%\n",
         pct(flyCircle, flySecs), pct(flyFresh, flySecs), pct(flyEkf, flySecs), pct(flyCombined, flySecs));
  printf("circling episodes (gap > 60 s): %zu   (medians over the episode; direction = circular mean)\n", eps.size());
  printf("  %3s %6s %5s | %-22s | %-22s\n", "#", "start", "secs", "EKF km/h  to-deg (n)", "circle fit km/h  to-deg (n)");
  for (size_t i = 0; i < eps.size(); i++) {
    const Episode& e = eps[i];
    char a[40], b[40];
    if (e.es.empty()) snprintf(a, sizeof a, "-"); else snprintf(a, sizeof a, "%5.1f  %5.0f (%zu)", med(e.es), circMean(e.ed), e.es.size());
    if (e.cs.empty()) snprintf(b, sizeof b, "-"); else snprintf(b, sizeof b, "%5.1f  %5.0f (%zu)", med(e.cs), circMean(e.cd), e.cs.size());
    printf("  %3zu %5us %5d | %-22s | %-22s\n", i + 1, e.start, e.secs, a, b);
  }
  printf("both available (EKF confident, circle fit age <= 60 s): %zu s\n", dVec.size());
  printf("  median |speed difference| %.2f km/h, median |direction difference| %.1f deg, median vector difference %.2f km/h\n",
         med(dSpeed), med(dDir), med(dVec));
  return 0;
}

// emu windtrace <lines.txt>: one CSV row per data second for host/wind_ref.py (the reference comparison):
// time of day, raw circle fit of the newest 30 s (ok, east, north), smoothed circle wind (valid, east, north, age),
// EKF (east, north, airspeed, sigma, confident) and the shown wind() (valid, east, north, age). Wind in m/s.
static int cmdWindTrace(const char* file) {
  std::vector<std::string> L = readLines(file);
  static FlightModel fm;
  fm.reset();
  puts("t,fit_ok,fit_e,fit_n,cw_ok,cw_e,cw_n,cw_age,ekf_e,ekf_n,ekf_va,ekf_sig,ekf_conf,w_ok,w_e,w_n,w_age");
  for (const std::string& l : L) {
    Sample s;
    if (!xctrcParse(l.c_str(), s) || !fm.feed(s)) continue;
    WindFit f;
    int n = fm.histCount();
    if (n >= FlightModel::WIND_S && s.fix) {
      static TrackPoint buf[FlightModel::WIND_S];
      for (int i = 0; i < FlightModel::WIND_S; i++) buf[i] = fm.hist(n - FlightModel::WIND_S + i);
      f = fitWind(buf, FlightModel::WIND_S);
    }
    Wind cw = fm.circleWind(), w = fm.wind();
    auto xy = [](const Wind& v, double& e, double& nn) {
      double r = v.toDeg * M_PI / 180; e = v.speedKmh / 3.6 * sin(r); nn = v.speedKmh / 3.6 * cos(r);
    };
    double ce = 0, cn = 0, we = 0, wn = 0;
    if (cw.valid) xy(cw, ce, cn);
    if (w.valid) xy(w, we, wn);
    printf("%d,%d,%.3f,%.3f,%d,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%d,%d,%.3f,%.3f,%u\n", s.hh * 3600 + s.mm * 60 + s.ss, f.ok, f.east, f.north,
           cw.valid, ce, cn, cw.ageS, fm.ekf().wx(), fm.ekf().wy(), fm.ekf().va(), fm.ekf().sigmaW(), fm.ekfConfident(),
           w.valid, we, wn, w.ageS);
  }
  return 0;
}

int main(int argc, char** argv) {
  signal(SIGALRM, watchdog);
  if (argc >= 2 && !strcmp(argv[1], "soak")) return cmdSoak(argc, argv, readLines);
  if (argc >= 3 && !strcmp(argv[1], "wind")) return cmdWind(argv[2]);
  if (argc >= 3 && !strcmp(argv[1], "windtrace")) return cmdWindTrace(argv[2]);
  if (argc >= 3 && !strcmp(argv[1], "cue")) return cmdCue(argc, argv);
  alarm(120);
  if (argc >= 3 && !strcmp(argv[1], "count")) {   // emu count <outdir> [rot]: per-section work of the live page
    gCountMode = true;
    return cmdShots(argv[2], argc > 3 ? atoi(argv[3]) : 1);
  }
  if (argc >= 3 && !strcmp(argv[1], "introshots")) { is::shots(argv[2], argc > 3 ? argv[3] : nullptr); return 0; }   // emu introshots <outdir> [pack]
  if (argc >= 3 && !strcmp(argv[1], "shots")) return cmdShots(argv[2], argc > 3 ? atoi(argv[3]) : 1);
  if (argc >= 4 && !strcmp(argv[1], "replay")) return cmdReplay(argc, argv);
  fputs("usage: emu shots <outdir> [rot] | emu replay <lines.txt> <outdir> [--hz N] [--inject] [--mix FILE] [--shot-every S] [--rot R]\n", stderr);
  return 2;
}
