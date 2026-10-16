#pragma once
// Tests for the climb column (apps/vario/core/thermal3d.*): synthetic thermals with a known lean and core, noise,
// no circling, a climb that ended, trend, strength bands, and the column behind the FlightModel.
// Included by tests.cpp after the CHECK macros.
#include <chrono>
#include <string.h>
#include "apps/vario/core/thermal3d.h"

namespace t3 {

static ThermalColumn gCol;                      // ~19 kB: static, like on the device

// A pilot circling in a thermal whose core leans: core(z) = (cx0 + sx z, cy0 + sy z) in the air-mass frame.
struct Synth {
  double sx = 0.0, sy = 0.0;                    // lean, metres per metre of height
  double cx0 = 0, cy0 = 0;
  double offE = 0, offN = 0;                    // the circle's centre is this far from the core
  float v0 = 2.5f, sigma = 30.0f;               // peak climb m/s, Gaussian radius of the lift m
  float vSlope = 0;                             // m/s of extra peak climb per 100 m of height
  float windE = 0, windN = 0;                   // m/s
  float posNoise = 0, varNoise = 0;
  double turnDeg = 15.0;                        // deg/s, clockwise
  double airMs = 9.0;
  double hdg = 40, z = 1500;
  double t = 0, px = 0, py = 0;                 // the pilot in the air frame (a parcel of air)
  uint32_t seed = 7;
  double lat0 = 43.2, lon0 = 26.9;

  double rnd() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFF) / 65536.0 - 0.5; }
  double gauss() { return (rnd() + rnd() + rnd() + rnd()) * 1.7320508; }   // sum of 4 uniforms: sigma 1/sqrt(3)*..., near enough
  double coreX(double zz) const { return cx0 + sx * (zz - 1500); }
  double coreY(double zz) const { return cy0 + sy * (zz - 1500); }

  // Place the pilot on a circle around the (offset) core at the current heading.
  void place() {
    const double R = airMs / (turnDeg * 0.017453292519943295);
    const double h = hdg * 0.017453292519943295;
    px = coreX(z) + offE - R * sin(h + 1.5707963267948966);
    py = coreY(z) + offN - R * cos(h + 1.5707963267948966);
  }
  void step(ThermalColumn& col, bool push = true) {
    place();
    const double r = hypot(px - coreX(z), py - coreY(z));
    float v = (v0 + vSlope * (float)(z - 1500) / 100.0f) * expf(-(float)(r * r) / (2 * sigma * sigma));
    const double gx = px + windE * t + posNoise * gauss(), gy = py + windN * t + posNoise * gauss();
    const double lat = lat0 + gy / 111320.0, lon = lon0 + gx / (111320.0 * cos(lat0 * 0.017453292519943295));
    if (push) col.push((uint32_t)(1000 + t * 1000), true, lat, lon, (float)z, v + varNoise * (float)gauss());
    z += v;
    hdg += turnDeg;
    t += 1;
  }
  void circles(ThermalColumn& col, double n) { for (int i = 0; i < (int)(n * 360.0 / turnDeg); i++) step(col); }
  // straight flight with the wind in the air frame: heading fixed, no lift
  void glide(ThermalColumn& col, int seconds, double sinkMs = -1.0) {
    const double h = hdg * 0.017453292519943295;
    for (int i = 0; i < seconds; i++) {
      px += airMs * sin(h);
      py += airMs * cos(h);
      const double gx = px + windE * t, gy = py + windN * t;
      col.push((uint32_t)(1000 + t * 1000), true, lat0 + gy / 111320.0,
               lon0 + gx / (111320.0 * cos(lat0 * 0.017453292519943295)), (float)z, (float)sinkMs);
      z += sinkMs;
      t += 1;
    }
  }
  // The true core relative to the newest point (the frame analyze() reports in): both are air-frame coordinates.
  void trueCore(double zz, double& x, double& y) const { x = coreX(zz) - px; y = coreY(zz) - py; }
};

static double bearingDiff(double a, double b) {
  double d = fmod(fabs(a - b), 360.0);
  return d > 180 ? 360 - d : d;
}

static void run() {
  puts("climb column (thermal3d)");
  {
    // a perfect tilted cylinder, 5 m/s of wind, the pilot centred on the core
    Synth s;
    s.sx = 0.08 * sin(60 * 0.0174533); s.sy = 0.08 * cos(60 * 0.0174533);     // 8 m per 100 m toward 60 deg
    s.windE = 5 * sin(250 * 0.0174533); s.windN = 5 * cos(250 * 0.0174533);
    s.sigma = 1e6f;                                          // uniform lift inside the column
    gCol.reset();
    s.circles(gCol, 8.2);
    gCol.analyze(s.windE, s.windN);
    CHECK(gCol.valid); CHECK(gCol.circling);
    CHECK(gCol.nCircles >= 7 && gCol.nCircles <= 9);
    CHECK(gCol.leanValid);
    NEAR(gCol.leanPer100, 8.0, 0.5);
    CHECK(bearingDiff(gCol.leanDeg, 60) < 4);
    CHECK(gCol.leanRms < 3.0);
    double worst = 0, wclimb = 0;
    for (int i = 0; i < gCol.nCircles; i++) {
      double tx, ty;
      s.trueCore(gCol.circles[i].z, tx, ty);
      worst = fmax(worst, hypot(gCol.circles[i].x - tx, gCol.circles[i].y - ty));
      wclimb = fmax(wclimb, fabs(gCol.circles[i].climb - 2.5));
    }
    CHECK(worst < 4.0);
    CHECK(wclimb < 0.15);
    // the core line at the newest altitude is where the core really is
    double tx, ty;
    s.trueCore(s.z, tx, ty);
    NEAR(gCol.coreX0 + gCol.coreSx * (s.z - gCol.coreZ0), tx, 4.0);
    NEAR(gCol.coreY0 + gCol.coreSy * (s.z - gCol.coreZ0), ty, 4.0);
    CHECK(gCol.trend == TREND_STEADY);
    CHECK(gCol.count() > 150 && gCol.count() <= 600);
    // the track's points are in the air frame: a closed ring, not smeared by the wind (radius 34 m)
    {
      const CoreCircle& lc = gCol.circles[gCol.nCircles - 1];
      double rmin = 1e9, rmax = 0;
      for (int i = lc.a; i < lc.b; i++) {
        const double r = hypot(gCol.px(i) - lc.cx, gCol.py(i) - lc.cy);
        rmin = fmin(rmin, r); rmax = fmax(rmax, r);
      }
      NEAR(rmin, 34.4, 3.0); NEAR(rmax, 34.4, 3.0);
    }
  }
  {
    // the circle is off the core by 12 m (west): the best lift is on the east side, the core point is pulled east
    Synth s;
    s.sx = 0.06; s.sy = 0.03;
    s.offE = -12; s.windE = 3; s.windN = 2;
    s.sigma = 28;
    gCol.reset();
    s.circles(gCol, 7.2);
    gCol.analyze(s.windE, s.windN);
    CHECK(gCol.valid && gCol.leanValid);
    const double leanTrue = 100 * hypot(s.sx, s.sy), dirTrue = atan2(s.sx, s.sy) / 0.0174533;
    NEAR(gCol.leanPer100, leanTrue, 1.5);
    CHECK(bearingDiff(gCol.leanDeg, dirTrue) < 10);
    int ok = 0;
    for (int i = 0; i < gCol.nCircles; i++) {
      // the core is east of the circle's centre: the best sector and the core point are on that side
      if (bearingDiff(gCol.circles[i].bestDeg, 90) < 40) ok++;
      CHECK(gCol.circles[i].x > gCol.circles[i].cx - 1.0f);
    }
    CHECK(ok >= gCol.nCircles - 1);
    CHECK(gCol.circles[2].bestClimb > gCol.circles[2].climb + 0.1f);
  }
  {
    // noise: 0.4 m/s on the vario, 0.7 m on every position (1 m/s on the velocity, ten times a good GPS)
    Synth s;
    s.sx = 0.05; s.sy = -0.04;
    s.windE = -4; s.windN = 1;
    s.posNoise = 0.7; s.varNoise = 0.4f;
    s.offN = 6;
    gCol.reset();
    s.circles(gCol, 8.2);
    gCol.analyze(s.windE, s.windN);
    CHECK(gCol.valid && gCol.leanValid);
    const double leanTrue = 100 * hypot(s.sx, s.sy), dirTrue = atan2(s.sx, s.sy) / 0.0174533 + 360;
    NEAR(gCol.leanPer100, leanTrue, 3.0);
    CHECK(bearingDiff(gCol.leanDeg, dirTrue) < 25);
    CHECK(gCol.nCircles >= 6 && gCol.nCircles <= 10);
    NEAR(gCol.circles[gCol.nCircles - 1].climb, 1.3, 0.4);      // the pilot is off-centre in a Gaussian core
  }
  {
    // the lean is only as good as the wind: an error of dw in the wind adds 100 * dw / climb metres per 100 m
    Synth s;
    s.sigma = 1e6f;
    s.windE = 4;
    gCol.reset();
    s.circles(gCol, 8.2);
    gCol.analyze(4, 0);
    NEAR(gCol.leanPer100, 0.0, 1.0);
    gCol.analyze(4.5f, 0);                                   // 0.5 m/s too much wind, climbing at 2.5 m/s
    NEAR(gCol.leanPer100, 20.0, 3.0);
    CHECK(bearingDiff(gCol.leanDeg, 270) < 8);              // the air-frame track is pushed upwind of where it was
  }
  {
    // a wind two thirds of the glider's speed (6 of 9 m/s): the ground course still turns 360 degrees per circle
    Synth s;
    s.sigma = 1e6f;
    s.windE = 6;
    s.airMs = 9;
    gCol.reset();
    s.circles(gCol, 8.2);
    gCol.analyze(6, 0);
    CHECK(gCol.valid && gCol.nCircles >= 6 && gCol.nCircles <= 9);
    NEAR(gCol.leanPer100, 0.0, 3.0);
  }
  {
    // the cue: the circle is 12 m west of a Gaussian core: shift east; a centred pilot gets a short shift
    Synth s;
    s.offE = -12; s.sigma = 28;
    gCol.reset();
    s.circles(gCol, 3.2);
    gCol.analyze(0, 0);
    CHECK(gCol.cue.valid);
    CHECK(bearingDiff(gCol.cue.shiftDeg, 90) < 25);
    CHECK(gCol.cue.shiftM > 8 && gCol.cue.shiftM < 45);
    CHECK(gCol.cue.turns >= 2);
    CHECK(gCol.cue.a0 > gCol.cue.a1 && gCol.cue.a1 >= 0);
    NEAR(gCol.cue.meanClimb, 1.3, 0.5);
    CHECK(gCol.cue.bestClimb > gCol.cue.meanClimb);
    CHECK(bearingDiff(atan2(gCol.cue.bestX - gCol.cue.cx, gCol.cue.bestY - gCol.cue.cy) / 0.0174533 + 360, 90) < 45);   // best lift on the east side
    Synth c;
    c.sigma = 28;
    gCol.reset();
    c.circles(gCol, 3.2);
    gCol.analyze(0, 0);
    CHECK(gCol.cue.valid && gCol.cue.shiftM < 12);                      // CENTRED
    // not before a full turn, not when the climb is over
    gCol.reset();
    Synth h;
    h.circles(gCol, 0.6);
    gCol.analyze(0, 0);
    CHECK(!gCol.cue.valid);
    gCol.reset();
    Synth e;
    e.circles(gCol, 3.2);
    e.glide(gCol, 30);
    gCol.analyze(0, 0);
    CHECK(gCol.valid && !gCol.cue.valid);
    // wind: the cue does not depend on it (air frame): same shift with 6 m/s of wind as without
    Synth w;
    w.offE = -12; w.sigma = 28; w.windE = 6; w.windN = 2;
    gCol.reset();
    w.circles(gCol, 3.2);
    gCol.analyze(6, 2);
    CHECK(gCol.cue.valid && bearingDiff(gCol.cue.shiftDeg, 90) < 25);
  }
  {
    // no circling: a straight glide, then a long wiggle of 30 degrees of S-turns
    Synth s;
    gCol.reset();
    s.glide(gCol, 200);
    gCol.analyze(0, 0);
    CHECK(!gCol.valid); CHECK(gCol.nCircles == 0); CHECK(gCol.count() == 0); CHECK(!gCol.leanValid);
    CHECK(gCol.trend == TREND_NONE);
    gCol.setHead(true, s.lat0, s.lon0, 1500, 1.0f);
    CHECK(!gCol.headValid());
    ThermalColumn& c2 = gCol;
    for (int i = 0; i < 100; i++) {                         // S-turns: heading swings +-25 degrees, never a circle
      s.hdg = 40 + 25 * sin(i * 0.3);
      s.glide(c2, 1, 0.5);
    }
    gCol.analyze(0, 0);
    CHECK(!gCol.valid);
    // half a circle only
    gCol.reset();
    s.turnDeg = 15;
    s.circles(gCol, 0.5);
    gCol.analyze(0, 0);
    CHECK(!gCol.valid);
    // too few points, and an empty column
    gCol.reset();
    gCol.analyze(0, 0);
    CHECK(!gCol.valid);
  }
  {
    // a climb that ended: shown for two minutes, with its age; later it is gone
    Synth s;
    s.sx = 0.05;
    gCol.reset();
    s.circles(gCol, 6.2);
    s.glide(gCol, 60);
    gCol.analyze(0, 0);
    CHECK(gCol.valid); CHECK(!gCol.circling);
    CHECK(gCol.ageS >= 55 && gCol.ageS <= 62);
    CHECK(gCol.nCircles >= 5);
    s.glide(gCol, 70);
    gCol.analyze(0, 0);
    CHECK(!gCol.valid);                                     // 130 s since the last circle
  }
  {
    // trend: constant, dying and building lift
    Synth s;
    s.sigma = 1e6f;                                         // lift the same everywhere on the circle
    gCol.reset();
    s.v0 = 3;
    s.circles(gCol, 4.2);
    s.v0 = 1.8f;
    s.circles(gCol, 2.2);
    gCol.analyze(0, 0);
    CHECK(gCol.valid && gCol.trend == TREND_WEAKENING && gCol.trendRatio < 0.75f && gCol.trendRatio > 0.4f);
    s.v0 = 0.2f;
    s.circles(gCol, 2.2);
    gCol.analyze(0, 0);
    CHECK(gCol.trend == TREND_TOP);
    Synth b;
    b.sigma = 1e6f;
    gCol.reset();
    b.v0 = 1.2f;
    b.circles(gCol, 4.2);
    b.v0 = 2.6f;
    b.circles(gCol, 2.2);
    gCol.analyze(0, 0);
    CHECK(gCol.trend == TREND_BUILDING);
  }
  {
    // strength by altitude: the lift gets stronger with height, 1 m/s per 100 m
    Synth s;
    s.sigma = 1e6f;
    s.v0 = 1.5f;
    s.vSlope = 0.5f;
    gCol.reset();
    s.circles(gCol, 8.2);
    gCol.analyze(0, 0);
    CHECK(gCol.nBands >= 5);
    CHECK(gCol.bandM == 25 || gCol.bandM == 50);
    bool rising = true;
    for (int i = 1; i < gCol.nBands; i++) if (gCol.bands[i].climb < gCol.bands[i - 1].climb - 0.05f) rising = false;
    CHECK(rising);
    CHECK(gCol.bands[gCol.nBands - 1].climb > gCol.bands[0].climb + 0.5f);
    NEAR(gCol.zMax, s.z, 4.0);
  }
  {
    // the ring holds ten minutes; older points drop out without breaking anything
    Synth s;
    s.sx = 0.04;
    gCol.reset();
    s.circles(gCol, 40);                                    // 960 s
    gCol.analyze(0, 0);
    CHECK(gCol.pushed() == ThermalColumn::MAXP);
    CHECK(gCol.valid && gCol.nCircles <= ThermalColumn::MAXC && gCol.nCircles >= 20);
    NEAR(gCol.leanPer100, 4.0, 0.7);                        // fitted through the newest ten circles
    // cost of one analyze() with the ring full (once per data second, only on the centring page)
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; i++) { s.step(gCol); gCol.analyze(0, 0); }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 200;
    printf("  analyze() with %d points: %.0f us per call on the PC (sanitizers on)\n", gCol.pushed(), us);
  }
  {
    // head: the newest sample, up to a second ahead of the newest point
    Synth s;
    gCol.reset();
    s.circles(gCol, 4.2);
    gCol.analyze(0, 0);
    gCol.setHead(true, s.lat0 + (s.py + 3.0) / 111320.0, s.lon0 + (s.px + 4.0) / (111320.0 * cos(s.lat0 * 0.0174533)), (float)s.z + 1.0f, 2.0f);
    CHECK(gCol.headValid());
    NEAR(gCol.headX(), 4.0, 0.1); NEAR(gCol.headY(), 3.0, 0.1); NEAR(gCol.headZ(), s.z + 1, 0.01);
    gCol.setHead(true, s.lat0 + 0.01, s.lon0, 1500, 0);       // 1 km away: not the same climb
    CHECK(!gCol.headValid());
    gCol.setHead(false, 0, 0, 0, 0);
    CHECK(!gCol.headValid());
  }
  {
    // bad input: no fix, NaN altitude and vario, a repeated time stamp
    gCol.reset();
    gCol.push(1000, false, 43, 26, 100, 0);
    gCol.push(2000, true, 43, 26, NAN, 0);
    CHECK(gCol.pushed() == 0);
    gCol.push(3000, true, 43, 26, 100, NAN);
    gCol.push(3000, true, 43, 26.001, 100, 1);
    CHECK(gCol.pushed() == 1);
    gCol.analyze(NAN, NAN);                                 // a NaN wind must not crash it
    CHECK(!gCol.valid);
  }
  {
    // behind the FlightModel: samples in, a column out (the tracer's own time stamps)
    static FlightModel fm;
    fm.reset();
    Synth s;
    s.sx = 0.07; s.windE = 4; s.windN = -2;
    int sec = 0;
    ThermalColumn dummy;                                     // only to reuse Synth's position maths
    (void)dummy;
    for (int i = 0; i < 190; i++, sec++) {
      s.place();
      const double r = hypot(s.px - s.coreX(s.z), s.py - s.coreY(s.z));
      const float v = 2.5f * expf(-(float)(r * r) / (2 * 30 * 30));
      Sample m;
      m.fix = true;
      const double gx = s.px + s.windE * s.t, gy = s.py + s.windN * s.t;
      m.lat = s.lat0 + gy / 111320.0;
      m.lon = s.lon0 + gx / (111320.0 * cos(s.lat0 * 0.0174533));
      m.alt = (float)s.z;
      m.vario = v;
      const double vx = 9 * sin(s.hdg * 0.0174533) + s.windE, vy = 9 * cos(s.hdg * 0.0174533) + s.windN;
      m.speedKmh = (float)(hypot(vx, vy) * 3.6);
      m.course = (float)fmod(atan2(vx, vy) / 0.0174533 + 360, 360);
      m.hh = 10; m.mm = (uint8_t)(sec / 60); m.ss = (uint8_t)(sec % 60);
      fm.feed(m);
      s.z += v; s.hdg += 15; s.t += 1;
    }
    const ThermalColumn& c = fm.column();
    printf("  via FlightModel: %d circles, lean %.1f m/100 m toward %.0f (true 7 toward 90), rms %.1f m\n", c.nCircles, c.leanPer100, c.leanDeg, c.leanRms);
    CHECK(c.valid && c.circling && c.nCircles >= 6);
    CHECK(c.leanValid);
    CHECK(c.leanPer100 > 3.0 && c.leanPer100 < 14.0);       // the model's own wind estimate is a few tenths of m/s off: see the sensitivity test
    CHECK(bearingDiff(c.leanDeg, 90) < 25);
    CHECK(c.headValid());
    fm.reset();
    CHECK(!fm.column().valid);
  }
}


// ---- the two pages
static int blackIn(MockPanel& p, int x0, int y0, int x1, int y1) {
  Bitmap1 bm = p.bitmap();
  int n = 0;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (!bm.getPixel(x, y)) n++;
  return n;
}

static void fillVm(ViewModel& vm, FlightModel& fm, int circlingSeconds, int glideSeconds) {
  Synth s;
  s.sx = 0.07; s.sy = 0.02; s.windE = 3; s.windN = 1; s.sigma = 40;
  fm.reset();
  int sec = 0;
  auto feed = [&](float v) {
    Sample m;
    m.fix = true;
    const double gx = s.px + s.windE * s.t, gy = s.py + s.windN * s.t;
    m.lat = s.lat0 + gy / 111320.0;
    m.lon = s.lon0 + gx / (111320.0 * cos(s.lat0 * 0.0174533));
    m.alt = (float)s.z; m.vario = v;
    const double vx = 9 * sin(s.hdg * 0.0174533) + s.windE, vy = 9 * cos(s.hdg * 0.0174533) + s.windN;
    m.speedKmh = (float)(hypot(vx, vy) * 3.6);
    m.course = (float)fmod(atan2(vx, vy) / 0.0174533 + 360, 360);
    m.hh = 10; m.mm = (uint8_t)(sec / 60); m.ss = (uint8_t)(sec % 60);
    fm.feed(m);
    sec++;
  };
  for (int i = 0; i < circlingSeconds; i++) {
    s.place();
    const double r = hypot(s.px - s.coreX(s.z), s.py - s.coreY(s.z));
    const float v = 2.5f * expf(-(float)(r * r) / (2 * 40 * 40));
    feed(v);
    s.z += v; s.hdg += 15; s.t += 1;
  }
  for (int i = 0; i < glideSeconds; i++) {
    s.px += 9 * sin(s.hdg * 0.0174533); s.py += 9 * cos(s.hdg * 0.0174533);
    feed(-1.0f);
    s.z -= 1; s.t += 1;
  }
  vm = ViewModel();
  vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED;
  vm.s = fm.last(); vm.avgVario = fm.avgVario(); vm.wind = fm.wind();
  vm.heading = vm.s.course; vm.headingSrc = HDG_GPS;
  vm.localMinutes = 600; vm.localBattPct = 80;
  vm.col = &fm.column();
}

static void runView() {
  puts("centring page");
  static ViewModel vm;
  static FlightModel fm;
  static MockPanel p;
  p.setRotation(1);
  struct Case { const char* name; int circ, glide; bool cue; };
  const Case cases[] = {{"circling", 9 * 24, 0, true}, {"left the lift", 9 * 24, 40, false}, {"half a circle", 12, 0, false},
                        {"nothing", 0, 90, false}, {"long climb, ring full", 40 * 24, 0, true}, {"very old climb", 9 * 24, 150, false}};
  for (const Case& cs : cases) {
    fillVm(vm, fm, cs.circ, cs.glide);
    vm.page = ViewModel::PAGE_CENTER;
    CHECK(vm.col->cue.valid == cs.cue);
    p.clear();
    drawLive(p.gfx(), vm, 0);
    Bitmap1 bm = p.bitmap();
    const int ink = blackIn(p, 22, 24, 240, 400);
    printf("  %-22s cue %d, black pixels in the page frame: %d\n", cs.name, (int)vm.col->cue.valid, ink);
    CHECK(ink > 400);
    // through the fast path (a Bitmap1) as well, and with every optional field missing
    p.clear();
    drawLive(p.gfx(), vm, 0, &bm);
    vm.s.alt = NAN; vm.s.vario = NAN; vm.wind.valid = false; vm.heading = NAN; vm.avgVario = NAN;
    p.clear();
    drawLive(p.gfx(), vm, 0, &bm);
  }
  // no column (a model built for another page): no crash
  fillVm(vm, fm, 0, 0);
  vm.col = nullptr;
  vm.page = ViewModel::PAGE_CENTER;
  p.clear();
  drawLive(p.gfx(), vm, 0);
  CHECK(blackIn(p, 22, 24, 240, 400) > 300);
  // the page cost on the PC: one turn and three turns on screen
  fillVm(vm, fm, 9 * 24, 0);
  vm.page = ViewModel::PAGE_CENTER;
  Bitmap1 bm = p.bitmap();
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 50; i++) { p.clear(); drawLive(p.gfx(), vm, 0, &bm); }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 50;
  printf("  PC draw time (sanitizers on, includes the panel clear): %.2f ms, %d track points in the column\n", ms, vm.col->count());
}

}  // namespace t3
