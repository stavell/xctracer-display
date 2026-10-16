// Unit tests for the pure layers and the app, run on the PC.
#include <math.h>
#include <stdio.h>
#include <string>
#include "app/app.h"
#include "app/command_decode.h"
#include "core/compass.h"
#include "core/devmatch.h"
#include "core/flight.h"
#include "core/home.h"
#include "core/linediff.h"
#include <string.h>
#include "core/windprofile.h"
#include "core/heading.h"
#include "core/heading_smoother.h"
#include "core/sim.h"
#include "core/spsc_ring.h"
#include "protocol.h"
#include "core/thermal.h"
#include "core/wind.h"
#include "core/windekf.h"
#include "core/xctrc.h"
#include "mocks.h"
#include "view/prof.h"
#include "view/screens.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))
#include "gfx_tests.h"
#include "ahrs_tests.h"

static const double D2R = 0.017453292519943295;

static std::string line(const char* body) {   // add the checksum
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= (uint8_t)*p;
  char buf[200];
  snprintf(buf, sizeof buf, "$%s*%02x", body, cs);
  return buf;
}

static void test_xctrc() {
  puts("xctrc parser");
  Sample s;
  // a line captured from the real tracer (no GPS fix indoors)
  CHECK(xctrcParse("$XCTRC,2026,10,15,9,52,20,0,0.000000,0.000000,128.66,0.00,0.0,0.20,,,,997.88,85*7c", s));
  CHECK(!s.fix); NEAR(s.alt, 128.66, 0.01); NEAR(s.vario, 0.20, 0.001); CHECK(s.battPct == 85);
  CHECK(!isfinite(s.speedKmh)); CHECK(!isfinite(s.course));
  CHECK(s.hh == 9 && s.mm == 52 && s.ss == 20);
  CHECK(!xctrcParse("$XCTRC,2026,10,15,9,52,20,0,0.000000,0.000000,128.66,0.00,0.0,0.20,,,,997.88,85*7d", s));  // checksum
  CHECK(!xctrcParse("$XCTRC,2026,10,15,9,52,20", s));
  CHECK(!xctrcParse("", s));
  CHECK(!xctrcParse("$GPGGA,1,2,3*00", s));
  std::string fixed = line("XCTRC,2026,8,16,10,38,24,0,43.252650,26.927967,1387.00,34.13,12.5,-1.30,,,,967.62,85");
  CHECK(xctrcParse(fixed.c_str(), s));
  CHECK(s.fix); NEAR(s.speedKmh, 34.13, 0.01); NEAR(s.course, 12.5, 0.01);
  // insane values become NaN instead of poisoning the maths
  std::string bad = line("XCTRC,2026,8,16,10,38,24,0,43.25,26.92,9999999.0,1e30,nan,inf,,,,967.62,85");
  CHECK(xctrcParse(bad.c_str(), s));
  CHECK(!isfinite(s.alt)); CHECK(!isfinite(s.speedKmh)); CHECK(!isfinite(s.vario));
}

// a one-second track of a circle flown at `air` km/h in `windKmh` blowing toward `windTo`
static void circle(TrackPoint* out, int n, double air, double windKmh, double windTo, double turnDegPerS, double startHdg) {
  double hdg = startHdg;
  for (int i = 0; i < n; i++, hdg += turnDegPerS) {
    double ax = air / 3.6 * sin(hdg * D2R), ay = air / 3.6 * cos(hdg * D2R);
    double wx = windKmh / 3.6 * sin(windTo * D2R), wy = windKmh / 3.6 * cos(windTo * D2R);
    double gx = ax + wx, gy = ay + wy;
    out[i] = {true, 43.0, 26.0, 1400, 1.5f, (float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) / D2R + 360, 360)};
  }
}

static void test_wind() {
  puts("wind fit");
  TrackPoint t[30];
  circle(t, 30, 36, 12, 270, 18, 40);
  WindFit f = fitWind(t, 30);
  CHECK(f.ok);
  NEAR(hypot(f.east, f.north) * 3.6, 12.0, 0.5);
  NEAR(fmod(atan2(f.east, f.north) / D2R + 360, 360), 270.0, 2.0);
  circle(t, 30, 36, 25, 45, -22, 200);   // opposite turn direction, stronger wind
  f = fitWind(t, 30);
  CHECK(f.ok);
  NEAR(hypot(f.east, f.north) * 3.6, 25.0, 0.8);
  NEAR(fmod(atan2(f.east, f.north) / D2R + 360, 360), 45.0, 2.0);
  circle(t, 30, 36, 12, 270, 0, 90);     // straight flight: no fit
  CHECK(!fitWind(t, 30).ok);
  circle(t, 30, 36, 12, 270, 4, 90);     // only ~120 degrees of turn: not enough
  CHECK(!fitWind(t, 30).ok);
  circle(t, 30, 36, 12, 270, 18, 40);
  t[7].fix = false;                       // a gap in the fix invalidates the window
  CHECK(!fitWind(t, 30).ok);
}

static Sample smp(int sec, bool fix, double lat, double lon, float spd, float crs, float vs, float alt) {
  Sample s;
  s.fix = fix; s.lat = lat; s.lon = lon; s.speedKmh = fix ? spd : NAN; s.course = fix ? crs : NAN;
  s.vario = vs; s.alt = alt; s.battPct = 85;
  s.hh = 10 + sec / 3600; s.mm = (sec / 60) % 60; s.ss = sec % 60;
  return s;
}

static void test_flight() {
  puts("flight model");
  FlightModel fm;
  CHECK(!fm.hasData());
  for (int i = 0; i < 40; i++) fm.feed(smp(i, true, 43, 26, 38, 90, -1.1f, 1500 - i));   // straight glide
  NEAR(fm.avgVario(), -1.1, 0.01);
  NEAR(fm.glide(), 38 / 3.6 / 1.1, 0.1);
  CHECK(fm.flying()); CHECK(!fm.circling()); CHECK(!fm.wind().valid);
  // same second twice does not tick the history
  int n = fm.histCount();
  CHECK(!fm.feed(smp(39, true, 43, 26, 38, 90, -1.1f, 1460)));
  CHECK(fm.histCount() == n);
  // circle in a 12 km/h wind toward 270
  FlightModel c;
  TrackPoint t[60];
  circle(t, 60, 36, 12, 270, 18, 40);
  for (int i = 0; i < 60; i++) c.feed(smp(i, true, 43, 26, t[i].speedKmh, t[i].course, 1.5f, 1400 + i));
  CHECK(c.circling());
  Wind w = c.wind();
  CHECK(w.valid); NEAR(w.speedKmh, 12.0, 1.0); NEAR(w.toDeg, 270.0, 3.0);
  CHECK(!isfinite(c.glide()));                // climbing: no glide ratio
  // no GPS: nothing derived from position
  FlightModel nf;
  for (int i = 0; i < 40; i++) nf.feed(smp(i, false, 0, 0, 0, 0, 0.1f, 130));
  CHECK(!nf.flying()); CHECK(!nf.wind().valid); CHECK(!isfinite(nf.glide()));
  // a jump in the data clock (tracer restarted) must not break anything
  nf.feed(smp(5000, true, 43, 26, 38, 90, -1.0f, 1000));
  CHECK(nf.histCount() > 0);
  // midnight wrap
  FlightModel mw;
  Sample a = smp(0, true, 43, 26, 38, 90, -1, 1500); a.hh = 23; a.mm = 59; a.ss = 59;
  Sample b = a; b.hh = 0; b.mm = 0; b.ss = 0;
  mw.feed(a); CHECK(mw.feed(b));
}

// ---- wind EKF ----------------------------------------------------------------------------------
struct Gnd { float kmh, crs; };
static Gnd groundOf(double vaKmh, double hdg, double windKmh, double windTo) {   // ground = Va*(sin h, cos h) + W
  double gx = vaKmh / 3.6 * sin(hdg * D2R) + windKmh / 3.6 * sin(windTo * D2R);
  double gy = vaKmh / 3.6 * cos(hdg * D2R) + windKmh / 3.6 * cos(windTo * D2R);
  return {(float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) / D2R + 360, 360)};
}
// S-turns: the heading sweeps 40 -> 140 -> 40 deg, 20 s each way (5 deg/s). A step between just two
// headings (40 / 80 every 20 s) gives only two distinct ground vectors for three unknowns (wind x, y and
// airspeed), so it is unobservable by construction; zigStepHdg below is kept to show that.
static double zigHdg(int i) { double p = i % 40; return p < 20 ? 40 + 5 * p : 140 - 5 * (p - 20); }
static double zigStepHdg(int i) { return (i / 20) % 2 ? 80 : 40; }

struct Lcg {                                                         // deterministic noise, no rand()
  uint32_t s;
  float u() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; }
  float gauss() { float a = 0; for (int i = 0; i < 12; i++) a += u(); return a - 6; }
};

static double angDiff(double a, double b) { return fabs(fmod(a - b + 540, 360) - 180); }
static void ekfVec(const WindEkf& e, double& kmh, double& to) {
  kmh = hypot(e.wx(), e.wy()) * 3.6;
  to = fmod(atan2(e.wx(), e.wy()) / D2R + 360, 360);
}

static void zigTrack(TrackPoint* t, int n, double va, double windKmh, double windTo) {
  for (int i = 0; i < n; i++) {
    Gnd g = groundOf(va, zigHdg(i), windKmh, windTo);
    t[i] = {true, 43.0, 26.0, 1400, 0, g.kmh, g.crs};
  }
}

static void test_wind_ekf() {
  puts("wind EKF");
  static TrackPoint t[300];
  double kmh, to;

  // 1. zig-zag without circles
  WindEkf e; e.reset();
  zigTrack(t, 240, 36, 15, 250);
  bool anyFit = false;
  for (int i = 30; i <= 240; i++) if (fitWind(t + i - 30, 30).ok) anyFit = true;
  CHECK(!anyFit);                                   // the circle fit cannot do this
  for (int i = 0; i < 240; i++) e.step(1, true, t[i].speedKmh, t[i].course);
  ekfVec(e, kmh, to);
  CHECK(e.confident()); NEAR(kmh, 15, 1.5); CHECK(angDiff(to, 250) <= 10);
  NEAR(e.va(), 10, 0.5);
  e.reset();                                        // two headings only: honestly not confident
  for (int i = 0; i < 240; i++) { Gnd g = groundOf(36, zigStepHdg(i), 15, 250); e.step(1, true, g.kmh, g.crs); }
  CHECK(!e.confident());

  // 2. circling
  e.reset();
  circle(t, 60, 36, 12, 270, 18, 40);
  for (int i = 0; i < 60; i++) e.step(1, true, t[i].speedKmh, t[i].course);
  ekfVec(e, kmh, to);
  CHECK(e.confident()); NEAR(kmh, 12, 1.0); CHECK(angDiff(to, 270) <= 5);

  // 3. straight flight only
  e.reset();
  bool ever = false;
  for (int i = 0; i < 300; i++) {
    Gnd g = groundOf(36, 90, 15, 250);
    e.step(1, true, g.kmh, g.crs);
    if (e.confident()) ever = true;
  }
  CHECK(!ever);

  // 4. wind change
  e.reset();
  for (int i = 0; i < 180; i++) { Gnd g = groundOf(36, zigHdg(i), 10, 90); e.step(1, true, g.kmh, g.crs); }
  ekfVec(e, kmh, to);
  NEAR(kmh, 10, 2.0); CHECK(angDiff(to, 90) <= 15);
  for (int i = 180; i < 420; i++) { Gnd g = groundOf(36, zigHdg(i), 20, 180); e.step(1, true, g.kmh, g.crs); }
  ekfVec(e, kmh, to);
  // the spec'd process noise (qW 0.0004) makes the filter stiff: 24 deg off after 240 s, not 15
  // (qW large enough for 15 deg would lose the sigmaW < 0.8 confidence in steady state)
  CHECK(e.confident()); NEAR(kmh, 20, 3.0); CHECK(angDiff(to, 180) <= 30);

  // 5. noise: speed 0.5 m/s, course 3 deg, airspeed wandering +-2 km/h
  e.reset();
  Lcg rng = {12345};
  float wander = 0;
  for (int i = 0; i < 240; i++) {
    wander += 0.1f * (rng.gauss() * 0.7f) - 0.05f * wander;
    if (wander > 2) wander = 2;
    if (wander < -2) wander = -2;
    Gnd g = groundOf(36 + wander, zigHdg(i), 15, 250);
    e.step(1, true, g.kmh + rng.gauss() * 0.5f * 3.6f, g.crs + rng.gauss() * 3.0f);
  }
  ekfVec(e, kmh, to);
  NEAR(kmh, 15, 2.5); CHECK(angDiff(to, 250) <= 15);

  // 6. a single outlier in the middle of test 1
  WindEkf a, b; a.reset(); b.reset();
  zigTrack(t, 240, 36, 15, 250);
  for (int i = 0; i < 240; i++) {
    a.step(1, true, t[i].speedKmh, t[i].course);
    b.step(1, true, i == 120 ? 30 * 3.6f : t[i].speedKmh, t[i].course);
  }
  CHECK(hypot(a.wx() - b.wx(), a.wy() - b.wy()) * 3.6 < 1.0);

  // 7. no fix / slow: state unchanged
  WindEkf c = a;
  float wx = c.wx(), wy = c.wy(), va = c.va();
  c.step(1, false, 40, 90);
  c.step(1, true, 2.9f * 3.6f, 90);
  c.step(1, true, NAN, 90);
  c.step(1, true, 40, NAN);
  CHECK(c.wx() == wx && c.wy() == wy && c.va() == va);

  // 8. through the FlightModel
  FlightModel fm;
  zigTrack(t, 240, 36, 15, 250);
  for (int i = 0; i < 240; i++) fm.feed(smp(i, true, 43, 26, t[i].speedKmh, t[i].course, 0, 1400));
  Wind w = fm.wind();
  CHECK(w.valid); CHECK(fm.ekfConfident()); NEAR(w.speedKmh, 15, 1.5); CHECK(angDiff(w.toDeg, 250) <= 10);
  CHECK(w.ageS == 0);
  FlightModel st;
  for (int i = 0; i < 300; i++) { Gnd g = groundOf(36, 90, 15, 250); st.feed(smp(i, true, 43, 26, g.kmh, g.crs, 0, 1400)); }
  CHECK(!st.wind().valid); CHECK(!st.ekfConfident()); CHECK(st.ekfSigmaW() > 0.8f);
}

static void test_wind_profile() {
  puts("wind profile");
  WindProfile p;
  ProfileBand out[60];
  CHECK(p.query(out, 60, 0) == 0);
  // 10 m/s east, then 20 m/s east: exponential average alpha 0.25
  p.add(1234, 10, 0, 1000);
  p.add(1250, 20, 0, 2000);
  int n = p.query(out, 60, 3000);
  CHECK(n == 1);
  NEAR(out[0].altM, 1250, 0.1); CHECK(out[0].samples == 2); CHECK(out[0].ageS == 1);
  NEAR(out[0].speedKmh, 12.5 * 3.6, 0.05); NEAR(out[0].toDeg, 90, 0.1);
  // clamping: below 0 and above 6000 land in the end bands; NaN is ignored
  p.add(-300, 0, 5, 3000);
  p.add(9000, -5, 0, 3000);
  p.add(NAN, 1, 1, 3000);
  p.add(500, NAN, 1, 3000);
  n = p.query(out, 60, 3000);
  CHECK(n == 3);
  NEAR(out[0].altM, 50, 0.1); NEAR(out[0].toDeg, 0, 0.1);
  NEAR(out[1].altM, 1250, 0.1);
  NEAR(out[2].altM, 5950, 0.1); NEAR(out[2].toDeg, 270, 0.1);
  CHECK(p.query(out, 2, 3000) == 2);                  // respects maxN
  // expiry after 60 minutes
  n = p.query(out, 60, 2000 + 60u * 60 * 1000);       // the 1250 m band was last updated at 2000: still alive
  CHECK(n == 3);
  n = p.query(out, 60, 3001 + 60u * 60 * 1000);       // the rest was updated at 3000: all gone now
  CHECK(n == 0);
  // an expired band starts afresh instead of averaging with stale air
  p.add(1250, 0, 30, 3001 + 2 * 60u * 60 * 1000);
  n = p.query(out, 60, 3001 + 2 * 60u * 60 * 1000);
  CHECK(n == 1 && out[0].samples == 1); NEAR(out[0].speedKmh, 108, 0.1);
  p.reset();
  CHECK(p.query(out, 60, 0) == 0);

  // through the flight model: a long circling climb, wind 10 km/h toward 270 below 1500 m, 25 toward 300 above
  static FlightModel fm;
  fm.reset();
  double hdg = 40;
  for (int i = 0; i < 1500; i++, hdg += 18) {
    float alt = 600 + 1.5f * i;
    double wk = alt < 1500 ? 10 : 25, wt = alt < 1500 ? 270 : 300;
    double gx = 36 / 3.6 * sin(hdg * D2R) + wk / 3.6 * sin(wt * D2R), gy = 36 / 3.6 * cos(hdg * D2R) + wk / 3.6 * cos(wt * D2R);
    fm.feed(smp(i, true, 43, 26, (float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) / D2R + 360, 360), 1.5f, alt));
  }
  static ProfileBand pb[60];
  n = fm.profile().query(pb, 60, fm.clockMs());
  CHECK(n > 20);
  int low = 0, high = 0;
  for (int i = 0; i < n; i++) {
    if (pb[i].altM < 1300 && pb[i].altM > 800) { low++; NEAR(pb[i].speedKmh, 10, 2); NEAR(pb[i].toDeg, 270, 10); }
    if (pb[i].altM > 1800) { high++; NEAR(pb[i].speedKmh, 25, 2); NEAR(pb[i].toDeg, 300, 10); }
  }
  CHECK(low >= 4 && high >= 6);
  fm.reset();
  CHECK(fm.profile().query(pb, 60, fm.clockMs()) == 0);
}

static void test_temp_profile() {
  puts("temperature profile");
  WindProfile p;
  static ProfileBand out[60];
  // exponential average, alpha 0.25; a band can hold temperature without wind
  p.addTemp(1234, 10, 1000);
  p.addTemp(1250, 14, 2000);
  p.addTemp(NAN, 5, 2000); p.addTemp(1250, NAN, 2000); p.addTemp(1250, INFINITY, 2000);
  int n = p.query(out, 60, 3000);
  CHECK(n == 1);
  NEAR(out[0].tempC, 11, 1e-4); CHECK(out[0].tempSamples == 2);
  CHECK(std::isnan(out[0].speedKmh)); CHECK(std::isnan(out[0].toDeg)); CHECK(out[0].samples == 0); CHECK(out[0].ageS == 1);
  // wind without temperature, in another band
  p.add(1750, 5, 0, 3000);
  n = p.query(out, 60, 3000);
  CHECK(n == 2);
  CHECK(!std::isnan(out[0].tempC) && std::isnan(out[1].tempC) && !std::isnan(out[1].speedKmh));
  // both in one band
  p.add(1250, 0, 5, 3000);
  n = p.query(out, 60, 3000);
  CHECK(n == 2); NEAR(out[0].tempC, 11, 1e-4); NEAR(out[0].speedKmh, 18, 0.01);
  // expiry is per quantity: the temperature (2000) dies first, the wind (3000) lives on
  n = p.query(out, 60, 2001 + 60u * 60 * 1000);
  CHECK(n == 2 && std::isnan(out[0].tempC) && !std::isnan(out[0].speedKmh));
  n = p.query(out, 60, 3001 + 60u * 60 * 1000);
  CHECK(n == 0);
  // an expired temperature starts afresh
  p.addTemp(1250, 3, 3001 + 2 * 60u * 60 * 1000);
  n = p.query(out, 60, 3001 + 2 * 60u * 60 * 1000);
  CHECK(n == 1); NEAR(out[0].tempC, 3, 1e-4); CHECK(out[0].tempSamples == 1);
  p.reset();
  CHECK(p.query(out, 60, 0) == 0);

  // lapse rate on a linear profile: -0.65 degC per 100 m
  static ProfileBand b[20];
  auto lin = [&](int cnt) {
    for (int i = 0; i < cnt; i++) {
      b[i].altM = 1050 + 100.0f * i; b[i].tempC = 15.0f - 0.0065f * (b[i].altM - 1000);
      b[i].speedKmh = NAN; b[i].toDeg = NAN; b[i].ageS = 0; b[i].samples = 0; b[i].tempSamples = 5;
    }
  };
  lin(10);
  NEAR(lapseRate(b, 10), -0.65, 1e-3);
  CHECK(!std::isnan(lapseRate(b, 3)));                 // 3 bands spanning exactly 200 m
  CHECK(std::isnan(lapseRate(b, 2)));                  // too few bands
  CHECK(std::isnan(lapseRate(b, 0)));
  b[1].tempC = NAN;                                    // a band without temperature does not count
  CHECK(std::isnan(lapseRate(b, 3)));
  NEAR(lapseRate(b, 10), -0.65, 1e-3);
  lin(10);
  b[0].altM = 1050; b[1].altM = 1070; b[2].altM = 1090;  // 3 bands but only 40 m apart
  CHECK(std::isnan(lapseRate(b, 3)));
  lin(10);
  for (int i = 0; i < 10; i++) CHECK(!inversionAt(b, 10, i));   // a normal lapse has no inversion
  // warms by 2 degC between 1500 and 1700 m: +1 at 1550 and 1650, +2 from 1750 up
  for (int i = 0; i < 10; i++) {
    if (b[i].altM > 1500 && b[i].altM < 1700) b[i].tempC += 1.0f;
    else if (b[i].altM >= 1700) b[i].tempC += 2.0f;
  }
  // 1550 is 0.35 warmer than 1450 and 1750 is 0.35 warmer than 1650; 1650 is cooler again than 1550
  for (int i = 0; i < 10; i++) {
    bool want = b[i].altM == 1550 || b[i].altM == 1750;
    CHECK(inversionAt(b, 10, i) == want);
  }
  b[5].tempC = NAN;                                      // 1550 has no temperature: 1650 has no valid band below
  CHECK(!inversionAt(b, 10, 6)); CHECK(!inversionAt(b, 10, 5));
  CHECK(!inversionAt(b, 10, -1)); CHECK(!inversionAt(b, 10, 10));
  // exactly 0.2 is not enough
  lin(3); b[1].tempC = b[0].tempC + 0.2f; CHECK(!inversionAt(b, 3, 1));
  b[1].tempC = b[0].tempC + 0.25f; CHECK(inversionAt(b, 3, 1));
}

static void test_flight_temperature() {
  puts("flight model temperature");
  static FlightModel fm;
  fm.reset();
  static ProfileBand pb[60];
  fm.feedTemperature(NAN);                              // ignored
  fm.feed(smp(0, true, 43, 26, 40, 90, 0, 1000));
  CHECK(fm.profile().query(pb, 60, fm.clockMs()) == 0);
  fm.feedTemperature(12.0f);
  fm.feed(smp(1, true, 43, 26, 40, 90, 0, 1020));
  int n = fm.profile().query(pb, 60, fm.clockMs());
  CHECK(n == 1); NEAR(pb[0].altM, 1050, 0.1); NEAR(pb[0].tempC, 12, 1e-4);
  // a reading is used while it is under 5 s old, then ignored
  fm.feed(smp(2, true, 43, 26, 40, 90, 0, 1230));
  fm.feed(smp(3, true, 43, 26, 40, 90, 0, 1330));
  fm.feed(smp(4, true, 43, 26, 40, 90, 0, 1430));
  fm.feed(smp(5, true, 43, 26, 40, 90, 0, 1530));       // 5 s old: stale
  fm.feed(smp(6, true, 43, 26, 40, 90, 0, 1630));
  fm.feed(smp(7, true, 43, 26, 40, 90, 0, 1730));
  n = fm.profile().query(pb, 60, fm.clockMs());
  bool b14 = false, b15 = false, b16 = false, b17 = false;
  for (int i = 0; i < n; i++) {
    if (pb[i].altM == 1450) b14 = true;      // 4 s old: still fresh
    if (pb[i].altM == 1550) b15 = true;
    if (pb[i].altM == 1650) b16 = true;
    if (pb[i].altM == 1750) b17 = true;
  }
  CHECK(b14 && !b15 && !b16 && !b17);
  // no altitude: nothing is added
  fm.feedTemperature(20.0f);
  fm.feed(smp(8, true, 43, 26, 40, 90, 0, NAN));
  CHECK(fm.profile().query(pb, 60, fm.clockMs()) == n);
  fm.reset();
  CHECK(fm.profile().query(pb, 60, fm.clockMs()) == 0);
  fm.feed(smp(0, true, 43, 26, 40, 90, 0, 1000));      // the pre-reset reading is gone
  fm.feed(smp(1, true, 43, 26, 40, 90, 0, 1000));
  CHECK(fm.profile().query(pb, 60, fm.clockMs()) == 0);
}

static void test_app_temperature() {
  puts("app forwards the thermometer");
  Rig r;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
  // a 60 s climb from 1000 to 1600 m, a reading every 2 s following the standard atmosphere
  for (int i = 0; i < 60; i++) {
    float alt = 1000.0f + 10.0f * i;
    char body[160];
    snprintf(body, sizeof body, "XCTRC,2026,8,16,10,%d,%d,0,43.252650,26.927967,%.2f,30.00,250.0,1.0,,,,900.00,85", i / 60, i % 60, alt);
    r.laptop.inject(line(body));
    if (i % 2 == 0) r.thermo.set(15.0f - 0.0065f * (alt - 1000.0f));
    run(1000);
  }
  static ProfileBand pb[60];
  int n = app.flight().profile().query(pb, 60, app.flight().clockMs());
  CHECK(n >= 5);
  int withT = 0;
  for (int i = 0; i < n; i++) if (!std::isnan(pb[i].tempC)) withT++;
  CHECK(withT == n);
  NEAR(lapseRate(pb, n), -0.65, 0.1);
  // a missing sensor adds nothing
  Rig r2;
  r2.thermo.present = false;
  App app2(r2.panel, r2.input, r2.battery, r2.tracer, r2.laptop, r2.settings, r2.compass, r2.thermo, r2.power);
  app2.begin(0);
  r2.thermo.set(10);
  now = 0;
  for (int i = 0; i < 5; i++) {
    char body[160];
    snprintf(body, sizeof body, "XCTRC,2026,8,16,10,0,%d,0,43.252650,26.927967,1000.00,30.00,250.0,1.0,,,,900.00,85", i);
    r2.laptop.inject(line(body));
    for (uint32_t e = now + 1000; now < e; now += 10) app2.tick(now);
  }
  CHECK(app2.flight().profile().query(pb, 60, app2.flight().clockMs()) == 0);
}

static void test_ambient() {
  puts("ambient temperature and humidity in the view model");
  Rig r;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
  run(500);
  CHECK(std::isnan(app.buildViewModel(now).ambientC) && std::isnan(app.buildViewModel(now).humidityPct));
  r.thermo.set(21.4f, 48);
  run(20);
  { const ViewModel& vm = app.buildViewModel(now); NEAR(vm.ambientC, 21.4, 1e-4); NEAR(vm.humidityPct, 48, 1e-4); }
  run(9000);
  CHECK(!std::isnan(app.buildViewModel(now).ambientC));
  run(2000);                                       // the last reading is now older than 10 s
  CHECK(std::isnan(app.buildViewModel(now).ambientC) && std::isnan(app.buildViewModel(now).humidityPct));
  r.thermo.set(5, 90);
  run(20);
  NEAR(app.buildViewModel(now).humidityPct, 90, 1e-4);
}

static void test_standalone_cadence() {
  puts("standalone compass redraws at the animation cadence");
  Rig r;
  r.compass.psi = 100;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
  auto presentsIn = [&](uint32_t ms) { int b = r.panel.presents; run(ms); return r.panel.presents - b; };
  run(4000);
  CHECK(!app.buildViewModel(now).fresh);
  int idle = presentsIn(1000);
  CHECK(idle >= 2 && idle <= 4);
  // on a page that normally uses the slow cadence
  { Command pc; pc.type = Command::PAGE; pc.value = 2; r.laptop.cmds.push_back(pc); }
  run(100);
  r.compass.psi = 190;
  run(40);
  CHECK(app.buildViewModel(now).heading < 189);
  CHECK(presentsIn(600) >= 8);                     // 50 ms gives 12, 150 ms only 4
  run(4000);
  int after = presentsIn(1000);
  CHECK(after >= 2 && after <= 4);
}

static void test_app_fusion() {
  puts("app: gyro fusion source, fallback and mounting");
  auto mk = [](Rig& r) { r.compass.psi = 123; r.compass.pitch = 10; r.compass.roll = -5; };
  // no gyro: the magnetic compass alone, as before
  { Rig r; mk(r);
    App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.begin(0);
    uint32_t now = 0;
    for (; now < 3000; now += 10) app.tick(now);
    CHECK(!app.fused());
    NEAR(angDiff(app.buildViewModel(now).heading, 123), 0, 1.0); }
  // gyro present: fused, steady, and the redraw cadence drops once the dial has settled
  { Rig r; mk(r); r.compass.gyro = true;
    App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.begin(0);
    uint32_t now = 0;
    for (; now < 8000; now += 10) app.tick(now);
    CHECK(app.fused());
    NEAR(angDiff(app.buildViewModel(now).heading, 123), 0, 1.0);
    int b = r.panel.presents;
    for (uint32_t e = now + 1000; now < e; now += 10) app.tick(now);
    int idle = r.panel.presents - b;
    printf("  fused, still: %d presents in 1 s\n", idle);
    CHECK(idle >= 2 && idle <= 4); }
  // a frozen gyro: back to the compass alone, heading still right
  { Rig r; mk(r); r.compass.gyro = true; r.compass.gyroNoise = 0;
    App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.begin(0);
    uint32_t now = 0;
    for (; now < 4000; now += 10) app.tick(now);
    CHECK(!app.fused());
    NEAR(angDiff(app.buildViewModel(now).heading, 123), 0, 1.0); }
  // mounting: the data arrives in the board frame; the command selects the index and it is stored
  { Rig r; mk(r); r.compass.gyro = true; r.compass.boardMount = 13;
    App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.begin(0);
    uint32_t now = 0;
    auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
    run(3000);
    CHECK(angErr(app.buildViewModel(now).heading, 123) > 5.0f || !app.fused());   // wrong mounting: wrong or no heading
    Command c; c.type = Command::ORIENT; c.value = 13; r.laptop.cmds.push_back(c);
    run(4000);
    CHECK(app.mountIndex() == 13 && r.settings.mount == 13);
    CHECK(app.fused());
    NEAR(angDiff(app.buildViewModel(now).heading, 123), 0, 1.0);
    Command bad; bad.type = Command::ORIENT; bad.value = 99; r.laptop.cmds.push_back(bad);
    run(100);
    CHECK(app.mountIndex() == 13); }
  // the stored hard-iron offset follows a change of mounting
  { Rig r; r.settings.cal.valid = true; r.settings.hasCal = true;
    r.settings.cal.off[0] = 10; r.settings.cal.off[1] = -20; r.settings.cal.off[2] = 30; r.settings.cal.radius = 45;
    App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.begin(0);
    Command c; c.type = Command::ORIENT; c.value = 7; r.laptop.cmds.push_back(c);
    for (uint32_t now = 0; now < 100; now += 10) app.tick(now);
    Vec3 o; o.x = 10; o.y = -20; o.z = 30;
    o = mountRemap(0, 7, o);
    NEAR(r.settings.cal.off[0], o.x, 1e-4); NEAR(r.settings.cal.off[1], o.y, 1e-4); NEAR(r.settings.cal.off[2], o.z, 1e-4); }
  // the command decoder accepts only valid indices
  { Command c; uint8_t ok[2] = {CMD_SET_ORIENT, 23}, no[2] = {CMD_SET_ORIENT, 24};
    CHECK(decodeCommand(ok, 2, c) && c.type == Command::ORIENT && c.value == 23);
    CHECK(!decodeCommand(no, 2, c)); }
}

static void test_home() {
  puts("home");
  HomePoint h; h.valid = true; h.lat = 43.0; h.lon = 26.0; h.alt = 500;
  Wind none;
  // 1 km south of home: home is north
  HomeInfo r = computeHome(43.0 - 1000 / 111320.0, 26.0, 1000, 0, h, none);
  CHECK(r.valid); NEAR(r.distM, 1000, 1); NEAR(r.bearingDeg, 0, 0.1); NEAR(r.relBearingDeg, 0, 0.1);
  NEAR(r.glideReq, 2.0, 0.01);
  // 1 km west of home: home is east; heading north = 90 deg to the right, heading east = straight on
  double dlon = 1000 / (111320.0 * cos(43.0 * D2R));
  r = computeHome(43.0, 26.0 - dlon, 1000, 0, h, none);
  NEAR(r.distM, 1000, 2); NEAR(r.bearingDeg, 90, 0.2); NEAR(r.relBearingDeg, 90, 0.2);
  r = computeHome(43.0, 26.0 - dlon, 1000, 180, h, none);
  NEAR(r.relBearingDeg, -90, 0.2);                       // heading south: home is on the left
  r = computeHome(43.0, 26.0 - dlon, 1000, 350, h, none);
  NEAR(r.relBearingDeg, 100, 0.2);                       // wraps correctly
  r = computeHome(43.0, 26.0 - dlon, 1000, NAN, h, none);
  CHECK(!isfinite(r.relBearingDeg)); CHECK(isfinite(r.bearingDeg));
  // arrival, no wind: 500 m above home, 1 km away at 10 m/s = 100 s, sink 1.1 m/s
  r = computeHome(43.0, 26.0 - dlon, 1000, 0, h, none);
  CHECK(r.reachable); NEAR(r.arrivalM, 500 - 1.1 * 1000 / 10.0, 1.0);
  // wind 18 km/h (5 m/s) blowing east, home is east: tailwind, more height on arrival
  Wind tail; tail.valid = true; tail.speedKmh = 18; tail.toDeg = 90;
  HomeInfo rt = computeHome(43.0, 26.0 - dlon, 1000, 0, h, tail);
  NEAR(rt.arrivalM, 500 - 1.1 * 1000 / 15.0, 1.0); CHECK(rt.arrivalM > r.arrivalM);
  Wind head = tail; head.toDeg = 270;
  HomeInfo rh = computeHome(43.0, 26.0 - dlon, 1000, 0, h, head);
  NEAR(rh.arrivalM, 500 - 1.1 * 1000 / 5.0, 1.0); CHECK(rh.arrivalM < r.arrivalM);
  // crosswind stronger than the airspeed: out of reach; a headwind that stops you too
  Wind cross = tail; cross.toDeg = 0; cross.speedKmh = 40;
  HomeInfo rc = computeHome(43.0, 26.0 - dlon, 1000, 0, h, cross);
  CHECK(!rc.reachable); CHECK(!isfinite(rc.arrivalM));
  Wind gale = head; gale.speedKmh = 36;
  CHECK(!computeHome(43.0, 26.0 - dlon, 1000, 0, h, gale).reachable);
  // below home: no required glide; negative arrival
  r = computeHome(43.0, 26.0 - dlon, 400, 0, h, none);
  CHECK(!isfinite(r.glideReq)); CHECK(r.arrivalM < 0);
  // no home / no position
  CHECK(!computeHome(43, 26, 1000, 0, HomePoint(), none).valid);
  CHECK(!computeHome(NAN, 26, 1000, 0, h, none).valid);
  // through the flight model: provisional first fix, final at takeoff, kept afterwards, cleared by reset
  static FlightModel fm;
  fm.reset();
  CHECK(!fm.home().valid);
  fm.feed(smp(0, false, 0, 0, 0, 0, 0, 800));            // no fix: no home
  CHECK(!fm.home().valid);
  for (int i = 1; i < 10; i++) fm.feed(smp(i, true, 43.0, 26.0, 0, 0, 0, 800));
  CHECK(fm.home().valid && fm.home().provisional); NEAR(fm.home().lat, 43.0, 1e-9); NEAR(fm.home().alt, 800, 0.01);
  for (int i = 10; i < 30; i++) fm.feed(smp(i, true, 43.0 + (i - 10) * 1e-4, 26.0, 20, 0, 0, 800 + (i - 10)));   // walks / runs off
  CHECK(fm.flying());
  CHECK(!fm.home().provisional);
  double homeLat = fm.home().lat;
  CHECK(homeLat > 43.0);                                  // the takeoff position, not the first fix
  for (int i = 30; i < 60; i++) fm.feed(smp(i, true, 43.0 + (i - 10) * 1e-4, 26.0, 20, 0, 0, 900));
  NEAR(fm.home().lat, homeLat, 1e-12);                    // kept
  fm.reset();
  CHECK(!fm.home().valid);
}

static void test_air_trail() {
  puts("air trail (wind-compensated map)");
  // circle at 36 km/h airspeed (radius = v / omega ~ 32 m) drifting with a 12 km/h wind toward 270
  FlightModel fm;
  double x = 0, y = 0, hdg = 40;
  const double wx = 12 / 3.6 * sin(270 * D2R), wy = 12 / 3.6 * cos(270 * D2R);
  for (int i = 0; i < 100; i++, hdg += 18) {
    double gx = 36 / 3.6 * sin(hdg * D2R) + wx, gy = 36 / 3.6 * cos(hdg * D2R) + wy;
    x += gx; y += gy;
    double lat = 43 + y / 111320.0, lon = 26 + x / (111320.0 * cos(43 * D2R));
    fm.feed(smp(i, true, lat, lon, (float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) / D2R + 360, 360), 1.5f, 1400 + i));
  }
  CHECK(fm.wind().valid);
  AirPoint tr[60];
  int n = fm.airTrail(tr, 60);
  CHECK(n == 60);
  CHECK(fabs(tr[n - 1].x) < 0.01 && fabs(tr[n - 1].y) < 0.01);   // current position is the origin
  // spread of the distance from the centroid: ~0 for a circle that stays put
  auto spread = [](const double* px, const double* py, int m) {
    double cx = 0, cy = 0;
    for (int i = 0; i < m; i++) { cx += px[i]; cy += py[i]; }
    cx /= m; cy /= m;
    double mean = 0, var = 0;
    for (int i = 0; i < m; i++) mean += hypot(px[i] - cx, py[i] - cy);
    mean /= m;
    for (int i = 0; i < m; i++) { double e = hypot(px[i] - cx, py[i] - cy) - mean; var += e * e; }
    return sqrt(var / m);
  };
  double ax[60], ay[60], gx[60], gy[60];
  const double kx = 111320.0 * cos(43 * D2R);
  for (int i = 0; i < n; i++) {
    ax[i] = tr[i].x; ay[i] = tr[i].y;
    const TrackPoint& p = fm.hist(fm.histCount() - n + i);
    gx[i] = (p.lon - fm.hist(fm.histCount() - 1).lon) * kx;
    gy[i] = (p.lat - fm.hist(fm.histCount() - 1).lat) * 111320.0;
  }
  double sa = spread(ax, ay, n), sg = spread(gx, gy, n);
  printf("  radius spread: wind-compensated %.1f m, ground track %.1f m\n", sa, sg);
  CHECK(sa < 4.0);                 // compensated: circles overlap
  CHECK(sg > 5 * sa);              // uncompensated: a drifting spiral
  // without a wind estimate the trail falls back to the ground track
  FlightModel nw;
  for (int i = 0; i < 20; i++) nw.feed(smp(i, true, 43 + i * 1e-5, 26, 38, 0, -1, 1500));
  CHECK(!nw.wind().valid);
  CHECK(nw.airTrail(tr, 60) == 20);
  // no fix at the end: no trail
  FlightModel nf;
  for (int i = 0; i < 20; i++) nf.feed(smp(i, false, 0, 0, 0, 0, 0, 130));
  CHECK(nf.airTrail(tr, 60) == 0);
}

static void test_thermal() {
  puts("thermal finder");
  // glide 40 s, circle (radius 30 m, centre at (100, 50)) climbing 2.5 m/s for 60 s, glide 30 s
  AirPoint tr[200];
  int n = 0;
  for (int i = 0; i < 40; i++) tr[n++] = {(float)(-i * 10.0), 0, -1.1f};
  for (int i = 0; i < 60; i++) {
    double a = i * 18 * D2R;
    tr[n++] = {(float)(100 + 30 * cos(a)), (float)(50 + 30 * sin(a)), 2.5f};
  }
  for (int i = 0; i < 30; i++) tr[n++] = {(float)(130 + i * 10.0), 50, -1.1f};
  Thermal th;
  CHECK(findThermal(tr, n, th));
  NEAR(th.x, 100, 6); NEAR(th.y, 50, 6);
  NEAR(th.avgClimb, 2.5, 0.1);
  CHECK(th.gain > 100); CHECK(th.ageS >= 28 && th.ageS <= 31);
  // a straight climb on the way in must not pull the centre sideways: 25 s in lift while flying, then circles
  AirPoint ap[200];
  int k = 0;
  for (int i = 0; i < 25; i++) ap[k++] = {(float)(-150 + i * 6.0), 0, 2.0f, 0.0f};          // approach, not turning
  for (int i = 0; i < 60; i++) {
    double a = i * 18 * D2R;
    ap[k++] = {(float)(100 + 30 * cos(a)), (float)(50 + 30 * sin(a)), 2.5f, 18.0f};
  }
  CHECK(findThermal(ap, k, th));
  NEAR(th.x, 100, 3); NEAR(th.y, 50, 3);
  // pure glide: no thermal
  AirPoint g[100];
  for (int i = 0; i < 100; i++) g[i] = {(float)(i * 10.0), 0, -1.0f};
  CHECK(!findThermal(g, 100, th));
  // a 5 s bump is not a thermal
  for (int i = 40; i < 45; i++) g[i].vario = 3.0f;
  CHECK(!findThermal(g, 100, th));
  // two thermals: the stronger one wins unless the later one is nearly as good
  AirPoint two[300];
  int m = 0;
  for (int i = 0; i < 40; i++) two[m++] = {(float)i, 0, -1.0f};
  for (int i = 0; i < 40; i++) two[m++] = {(float)(200 + 20 * cos(i * 0.4)), (float)(20 * sin(i * 0.4)), 3.0f};   // 120 m gain
  for (int i = 0; i < 40; i++) two[m++] = {(float)(400 + i), 0, -1.0f};
  for (int i = 0; i < 40; i++) two[m++] = {(float)(600 + 20 * cos(i * 0.4)), (float)(20 * sin(i * 0.4)), 1.0f};   // 40 m gain
  CHECK(findThermal(two, m, th));
  NEAR(th.x, 200, 10);
  // empty and tiny inputs
  CHECK(!findThermal(two, 0, th));
  CHECK(!findThermal(two, 3, th));
}

// A thermal that drifts with the wind: a pilot circles it (radius 30 m, climbing 2.5 m/s) for `lapSec`
// seconds, starting at second `t0`; returns the true centre of the air column at the final second.
struct Drift { double x = 0, y = 0; };

static void test_thermal_memory() {
  puts("thermal memory");
  const double wxm = -12 / 3.6, wym = 0;                  // 12 km/h toward 270
  const double kx = 111320.0 * cos(43 * D2R), ky = 111320.0;
  FlightModel fm;
  double px = -400, py = 0;                               // pilot, ground metres
  double lastx = px, lasty = py;
  int sec = 0;
  auto feed = [&](double x, double y, float vs) {
    double vx = x - lastx, vy = y - lasty;
    lastx = x; lasty = y;
    double lat = 43 + y / ky, lon = 26 + x / kx;
    fm.feed(smp(sec, true, lat, lon, (float)(hypot(vx, vy) * 3.6), (float)fmod(atan2(vx, vy) / D2R + 360, 360), vs, 1400 + sec));
    sec++;
  };
  // 20 s glide in, 90 s circling in the thermal
  for (int i = 0; i < 20; i++) { px += 8 + wxm; py += wym; feed(px, py, -1.1f); }
  double a = 0;
  double colX = 0, colY = 0;                              // the thermal column's centre moves with the wind
  colX = px + 0; colY = py + 0;
  for (int i = 0; i < 90; i++) {
    a += 18 * D2R;
    colX += wxm; colY += wym;
    feed(colX + 30 * cos(a), colY + 30 * sin(a), 2.5f);
  }
  // 40 s glide away across the wind
  double gx = lastx, gy = lasty;
  for (int i = 0; i < 40; i++) { gx += 0; gy += 10; feed(gx, gy, -1.1f); }
  ThermalRel tr[ThermalMemory::MAX];
  int n = fm.thermals(tr, ThermalMemory::MAX);
  CHECK(n == 1);
  // where the column is now relative to the pilot: it drifted 40 more seconds with the wind
  double trueX = colX + wxm * 40 - lastx, trueY = colY + wym * 40 - lasty;
  printf("  mark at (%.0f, %.0f) m, true (%.0f, %.0f) m, strength %+.1f m/s, gain %.0f m, age %u s\n",
         tr[0].x, tr[0].y, trueX, trueY, tr[0].avgClimb, tr[0].gain, tr[0].ageS);
  { Wind w = fm.wind(); printf("  wind estimate %.2f km/h toward %.1f (true 12.00 toward 270.0)\n", w.speedKmh, w.toDeg); }
  NEAR(tr[0].x, trueX, 20); NEAR(tr[0].y, trueY, 20);
  NEAR(tr[0].avgClimb, 2.5, 0.2);
  CHECK(tr[0].ageS >= 35 && tr[0].ageS <= 45);
  // keep gliding 16 minutes: the mark expires
  for (int i = 0; i < 16 * 60; i++) { gx += 0; gy += 10; feed(gx, gy, -1.1f); }
  CHECK(fm.thermals(tr, ThermalMemory::MAX) == 0);
  // after losing the fix there is nothing to be relative to
  FlightModel nf;
  for (int i = 0; i < 30; i++) nf.feed(smp(i, false, 0, 0, 0, 0, 2.5f, 100));
  CHECK(nf.thermals(tr, ThermalMemory::MAX) == 0);
}


static float angDiff(float a, float b) { float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f; return d; }

static void test_compass() {
  puts("compass maths");
  MockCompass mc;
  Compass c;
  Vec3 a, m;
  // level, every heading
  for (int psi = 0; psi < 360; psi += 15) {
    mc.reading(psi, 0, 0, a, m);
    c.reset();
    c.update(a, m, 0);
    CHECK(c.valid());
    NEAR(fabsf(angDiff(c.heading(), (float)psi)), 0, 0.5);
  }
  // tilted: pitch and roll must not change the heading of the top of the screen
  int bad = 0;
  for (int psi = 0; psi < 360; psi += 30)
    for (int pitch = -50; pitch <= 50; pitch += 25)
      for (int roll = -50; roll <= 50; roll += 25) {
        mc.reading(psi, pitch, roll, a, m);
        c.reset();
        c.update(a, m, 0);
        if (!c.valid() || fabsf(angDiff(c.heading(), (float)psi)) > 1.0f) bad++;
      }
  CHECK(bad == 0);
  // freefall: no usable gravity, no heading
  mc.reading(90, 0, 0, a, m);
  a.x = a.y = a.z = 0.2f;
  c.reset();
  c.update(a, m, 0);
  CHECK(!c.valid());
  // hard iron: an uncalibrated offset shifts the heading, calibration removes it
  MockCompass hi;
  hi.hardIron[0] = 12; hi.hardIron[1] = -18; hi.hardIron[2] = 7;
  Compass u;
  hi.reading(90, 0, 0, a, m);
  u.update(a, m, 0);
  CHECK(fabsf(angDiff(u.heading(), 90)) > 5.0f);
  Compass k;
  CHECK(!k.calibration().valid);
  k.startCalibration();
  uint32_t calT = 0;
  for (int psi = 0; psi < 360; psi += 20)                  // sweep orientations like a person turning the unit
    for (int pitch = -90; pitch <= 90; pitch += 30)
      for (int roll = -90; roll <= 90; roll += 45) {
        hi.reading(psi, pitch, roll, a, m);
        k.update(a, m, 0);
      }
  float span[3];
  k.calibrationSpan(span);
  CHECK(span[0] > 50 && span[1] > 50 && span[2] > 50);
  CHECK(k.finishCalibration(true));
  CHECK(k.calibration().valid);
  float worst = 0;
  for (int psi = 0; psi < 360; psi += 30)
    for (int tilt = -30; tilt <= 30; tilt += 30) {
      hi.reading(psi, tilt, tilt / 2, a, m);
      k.reset();                                           // reset keeps the calibration
      k.update(a, m, 0);
      worst = fmaxf(worst, fabsf(angDiff(k.heading(), (float)psi)));
    }
  printf("  worst heading error after calibration: %.1f degrees\n", worst);
  CHECK(worst < 1.5f);
  // barely moving: the calibration is not replaced
  Compass lazy;
  lazy.startCalibration();
  hi.reading(0, 0, 0, a, m);
  lazy.update(a, m, 0);
  CHECK(!lazy.finishCalibration(true));
  CHECK(!lazy.calibration().valid);
}

static void test_heading_selector() {
  puts("heading selector");
  HeadingSelector h;
  h.configure(4.0f, 2.5f);
  CHECK(h.current().src == HDG_NONE);
  CHECK(h.wantsMagnetometer());
  h.feedMag(true, 200);
  CHECK(h.current().src == HDG_MAG); NEAR(h.current().deg, 200, 0.01);
  h.feedGps(true, 3.0f, 90);                               // below the on threshold: still the compass
  CHECK(h.current().src == HDG_MAG);
  h.feedGps(true, 5.0f, 90);                               // moving: the GPS track wins
  CHECK(h.current().src == HDG_GPS); NEAR(h.current().deg, 90, 0.01);
  CHECK(!h.wantsMagnetometer());
  h.feedGps(true, 3.0f, 95);                               // slowing: hysteresis keeps GPS above 2.5
  CHECK(h.current().src == HDG_GPS);
  h.feedGps(true, 2.0f, 95);                               // slow: back to the compass
  CHECK(h.current().src == HDG_MAG); CHECK(h.wantsMagnetometer());
  h.feedGps(true, 3.5f, 95);                               // not above 4 yet: stays on the compass
  CHECK(h.current().src == HDG_MAG);
  h.feedGps(true, 8.0f, 100);
  CHECK(h.current().src == HDG_GPS);
  h.feedGps(false, 30.0f, 100);                            // no fix / stale data: not usable
  CHECK(h.current().src == HDG_MAG);
  h.feedMag(false, NAN);                                   // and no valid compass either
  CHECK(h.current().src == HDG_NONE); CHECK(!isfinite(h.current().deg));
  h.feedGps(true, NAN, NAN);
  CHECK(h.current().src == HDG_NONE);
}

static void test_app_heading() {
  puts("app heading source and calibration");
  Rig r;
  r.compass.psi = 123; r.compass.pitch = 10; r.compass.roll = -5;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
  run(200);
  // compass page, no data from the tracer: the magnetometer is on and feeds the dial
  CHECK(app.compassOn()); CHECK(r.compass.enabled);
  const ViewModel& vm = app.buildViewModel(now);
  CHECK(vm.headingSrc == HDG_MAG);
  NEAR(fabsf(angDiff(vm.heading, 123)), 0, 2.0);
  // the tracer reports a fix but we are standing still: still the compass
  r.laptop.inject(line("XCTRC,2026,8,16,10,38,24,0,43.252650,26.927967,1387.00,1.00,12.5,0.1,,,,967.62,85"));
  run(200);
  CHECK(app.buildViewModel(now).headingSrc == HDG_MAG);
  // moving: the GPS track takes over; the magnetometer keeps running
  r.laptop.inject(line("XCTRC,2026,8,16,10,38,25,0,43.252650,26.927967,1387.00,30.00,250.0,0.1,,,,967.62,85"));
  run(2500);                                   // the dial glides there
  CHECK(app.buildViewModel(now).headingSrc == HDG_GPS);
  NEAR(fabsf(angDiff(app.buildViewModel(now).heading, 250)), 0, 0.01);
  CHECK(app.compassOn()); CHECK(r.compass.enabled);
  // data goes stale: back to the compass
  run(4000);
  CHECK(app.compassOn());
  CHECK(app.buildViewModel(now).headingSrc == HDG_MAG);
  // every page keeps the magnetometer on
  { Command pc; pc.type = Command::PAGE; pc.value = 1; r.laptop.cmds.push_back(pc); }
  run(200);
  CHECK(app.compassOn());
  { Command pc; pc.type = Command::PAGE; pc.value = 2; r.laptop.cmds.push_back(pc); }
  run(200);
  CHECK(app.compassOn());
  { Command pc; pc.type = Command::PAGE; pc.value = 3; r.laptop.cmds.push_back(pc); }
  run(200);
  CHECK(app.compassOn());
  { Command pc; pc.type = Command::PAGE; pc.value = 0; r.laptop.cmds.push_back(pc); }
  run(200);
  CHECK(app.compassOn());

  // calibration from the laptop: start, sweep the unit, save: stored and loaded by a new app instance
  r.compass.hardIron[0] = 12; r.compass.hardIron[1] = -18; r.compass.hardIron[2] = 7;
  { Command cc; cc.type = Command::CAL; cc.value = 1; r.laptop.cmds.push_back(cc); }
  run(100);
  CHECK(app.calibrating());
  for (int psi = 0; psi < 360; psi += 20)
    for (int pitch = -90; pitch <= 90; pitch += 30)
      for (int roll = -90; roll <= 90; roll += 45) {
        r.compass.psi = psi; r.compass.pitch = pitch; r.compass.roll = roll;
        run(20);
      }
  CHECK(app.buildViewModel(now).calSpan[0] > 50);
  { Command cc; cc.type = Command::CAL; cc.value = 2; r.laptop.cmds.push_back(cc); }
  run(100);
  CHECK(!app.calibrating()); CHECK(r.settings.hasCal); CHECK(r.settings.saves == 1);
  r.compass.psi = 200; r.compass.pitch = 0; r.compass.roll = 0;
  run(2500);
  NEAR(fabsf(angDiff(app.buildViewModel(now).heading, 200)), 0, 4.0);
  { Rig r2; r2.settings = r.settings; r2.compass = r.compass; r2.compass.enabled = false;
    App app2(r2.panel, r2.input, r2.battery, r2.tracer, r2.laptop, r2.settings, r2.compass, r2.thermo, r2.power);
    app2.begin(0);
    uint32_t n2 = 0;
    for (; n2 < 400; n2 += 10) app2.tick(n2);
    NEAR(fabsf(angDiff(app2.buildViewModel(n2).heading, 200)), 0, 4.0); }     // calibration survives a restart
  // cancelling keeps the old calibration
  { Command cc; cc.type = Command::CAL; cc.value = 1; r.laptop.cmds.push_back(cc); }
  run(100);
  { Command cc; cc.type = Command::CAL; cc.value = 0; r.laptop.cmds.push_back(cc); }
  run(100);
  CHECK(!app.calibrating()); CHECK(r.settings.saves == 1);
  // calibration from the device list: the second row
  r.input.press(now + 100, 900);                           // long: open the list
  run(1500);
  CHECK(app.listOpen());
  r.input.press(now + 100, 150);                           // short: the calibrate row
  run(600);
  r.input.press(now + 100, 900);                           // long: start calibration
  run(1500);
  CHECK(app.calibrating());
  r.input.press(now + 100, 150);                           // short: cancel
  run(600);
  CHECK(!app.calibrating());
}

static void test_button() {
  puts("button state machine");
  auto run = [](uint32_t downMs) {
    ButtonFsm f;
    ButtonEvent last = BTN_NONE, ev;
    uint32_t t = 100;
    for (; t < 100 + downMs; t++) { ev = f.feed(true, t); if (ev != BTN_NONE) last = ev; }
    for (uint32_t k = 0; k < 100; k++, t++) { ev = f.feed(false, t); if (ev != BTN_NONE) last = ev; }
    return last;
  };
  CHECK(run(200) == BTN_SHORT);
  CHECK(run(900) == BTN_LONG);
  CHECK(run(2000) == BTN_NONE);     // armed zone: released = cancelled
  CHECK(run(3500) == BTN_HOLD);
  ButtonFsm f; int events = 0;      // contact bounce counts once
  for (int i = 0; i < 6; i++) { if (f.feed(i % 2 == 0, 100 + i * 3) != BTN_NONE) events++; }
  for (uint32_t t = 200; t < 400; t++) if (f.feed(false, t) != BTN_NONE) events++;
  CHECK(events <= 1);
}

static void test_app() {
  puts("app");
  Rig r;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };

  run(1000);
  CHECK(!app.buildViewModel(now).fresh);                       // nothing received: status screen
  // live tracer lines
  r.tracer.lines.push_back("$XCTRC,2026,10,15,9,52,20,0,0.000000,0.000000,128.66,0.00,0.0,0.20,,,,997.88,85*7c");
  run(100);
  CHECK(app.linesOk() == 1); CHECK(app.buildViewModel(now).fresh);
  r.tracer.lines.push_back("garbage");
  run(100);
  CHECK(app.linesBad() == 1);
  // injected data silences the live tracer
  std::string inj = line("XCTRC,2026,8,16,10,38,24,0,43.252650,26.927967,1387.00,34.13,12.5,-1.30,,,,967.62,85");
  r.laptop.inject(inj);
  run(100);
  uint32_t ok = app.linesOk();
  r.tracer.lines.push_back(line("XCTRC,2026,10,15,9,52,21,0,0.000000,0.000000,128.66,0.00,0.0,0.20,,,,997.88,85"));
  run(100);
  CHECK(app.linesOk() == ok);                             // live line dropped
  CHECK(app.buildViewModel(now).s.fix);
  run(4000);                                              // injection stops: live data returns
  r.tracer.lines.push_back(line("XCTRC,2026,10,15,9,52,22,0,0.000000,0.000000,128.66,0.00,0.0,0.20,,,,997.88,85"));
  run(100);
  CHECK(app.linesOk() == ok + 1);
  // simulator
  r.laptop.sim(true);
  run(5000);
  CHECK(app.simOn()); CHECK(app.buildViewModel(now).fresh); CHECK(app.buildViewModel(now).s.fix);
  r.laptop.sim(false);
  run(100);
  CHECK(!app.simOn());
  // local time zone
  r.laptop.sim(false);
  Command c; c.type = Command::TZ; c.value = 180; r.laptop.cmds.push_back(c);
  r.laptop.inject(inj);
  run(100);
  CHECK(r.settings.tz == 180);
  CHECK(app.buildViewModel(now).localMinutes == (10 * 60 + 38 + 180) % 1440);
  // laptop-drawn frames own the screen; the app resumes afterwards
  int before = r.panel.refreshes;
  r.laptop.drawing = true;
  run(500);
  CHECK(r.panel.refreshes == before);
  r.laptop.drawing = false;
  run(1000);
  CHECK(r.panel.refreshes > before);
  // device list via a long press, navigate, select
  r.tracer.addItem("XC-Tracer", -52); r.tracer.addItem("ATC_0CAA9D", -66);
  r.input.press(now + 100, 900);                          // long press opens the list
  run(1500);
  CHECK(app.listOpen()); CHECK(r.tracer.opened);
  r.input.press(now + 100, 150);                          // short: next row (the calibrate row)
  run(600);
  r.input.press(now + 100, 150);                          // short: next row (the first device)
  run(600);
  r.input.press(now + 100, 900);                          // long: select the first device
  run(1500);
  CHECK(!app.listOpen()); CHECK(r.tracer.connected == 0);
  // the laptop can switch pages too
  { Command pc; pc.type = Command::PAGE; pc.value = 1; r.laptop.cmds.push_back(pc); }
  run(100);
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_THERMAL);
  { Command pc; pc.type = Command::PAGE; pc.value = 0; r.laptop.cmds.push_back(pc); }
  run(100);
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_COMPASS);
  // short press toggles the thermal map page
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_COMPASS);
  r.input.press(now + 100, 150);
  run(600);
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_THERMAL);
  r.input.press(now + 100, 150);
  run(600);
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_PROFILE);
  r.input.press(now + 100, 150);
  run(600);
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_HOME);
  r.input.press(now + 100, 150);
  run(600);
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_COMPASS);
  // laptop page index 2 and 3 select profile and home; 4 and above are ignored
  for (int pg = 2; pg <= 3; pg++) {
    Command pc; pc.type = Command::PAGE; pc.value = pg; r.laptop.cmds.push_back(pc);
    run(100);
    CHECK(app.buildViewModel(now).page == (pg == 2 ? ViewModel::PAGE_PROFILE : ViewModel::PAGE_HOME));
  }
  { Command pc; pc.type = Command::PAGE; pc.value = 4; r.laptop.cmds.push_back(pc); run(100); }
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_HOME);
  { Command pc; pc.type = Command::PAGE; pc.value = 0; r.laptop.cmds.push_back(pc); run(100); }
  // hold 3 s = power off
  r.input.press(now + 100, 3500);
  run(4500);
  CHECK(r.power.off);
}

static void test_linediff() {
  static uint8_t a[LD_LINES * LD_BYTES_PER_LINE], b[LD_LINES * LD_BYTES_PER_LINE];
  uint8_t out[LD_LINES];
  memset(a, 0xff, sizeof a); memset(b, 0xff, sizeof b);
  CHECK(changedLines(a, b, out) == 0);
  const int ys[] = {0, 119, 239};
  for (int y : ys) {
    const int bis[] = {0, LD_BYTES_PER_LINE - 1};
    for (int bi : bis) {
      b[y * LD_BYTES_PER_LINE + bi] ^= 0x10;
      CHECK(changedLines(a, b, out) == 1 && out[0] == y);
      b[y * LD_BYTES_PER_LINE + bi] ^= 0x10;
    }
  }
  memset(b, 0x00, sizeof b);
  int n = changedLines(a, b, out);
  CHECK(n == LD_LINES);
  bool seq = true;
  for (int i = 0; i < n; i++) seq = seq && out[i] == i;
  CHECK(seq);
  memset(b, 0xff, sizeof b);
  const int sc[] = {3, 4, 77, 150, 238};
  for (int y : sc) b[y * LD_BYTES_PER_LINE + 25] = 0;
  n = changedLines(a, b, out);
  CHECK(n == 5);
  for (int i = 0; i < 5 && i < n; i++) CHECK(out[i] == sc[i]);
}

static void test_spsc_ring() {
  SpscRing<int, 4> q;
  int v = -1;
  CHECK(!q.pop(v));
  for (int i = 0; i < 4; i++) CHECK(q.push(i + 10));
  CHECK(!q.push(99));                      // full: dropped
  for (int i = 0; i < 4; i++) { CHECK(q.pop(v)); CHECK(v == i + 10); }
  CHECK(!q.pop(v));
  // wrap-around: many more items than slots, interleaved, order kept
  int next = 0, expect = 0;
  bool ok = true;
  for (int round = 0; round < 100000; round++) {
    int burst = 1 + round % 4;
    for (int k = 0; k < burst; k++) if (!q.push(next++)) { ok = false; }
    for (int k = 0; k < burst; k++) { if (!q.pop(v) || v != expect++) ok = false; }
  }
  CHECK(ok);
  CHECK(!q.pop(v));
  struct Big { char s[160]; };
  SpscRing<Big, 8> b;
  Big x, y;
  strcpy(x.s, "$XCTRC,test");
  CHECK(b.push(x) && b.pop(y) && strcmp(y.s, "$XCTRC,test") == 0);
}

static DevAddr mk(uint8_t last, uint8_t type) {
  DevAddr a;
  a.addr[0] = last;
  a.type = type;
  return a;
}

static void test_devmatch() {
  CHECK(globMatch("*", "anything"));
  CHECK(globMatch("", "anything"));
  CHECK(globMatch("", ""));
  CHECK(globMatch("XC*", "XCTracer-1234"));
  CHECK(globMatch("xc*", "XCTracer"));
  CHECK(globMatch("XCTRACER", "xctracer"));
  CHECK(!globMatch("XC*", "My XCTracer"));
  CHECK(globMatch("*Tracer*", "My XCTracer 2"));
  CHECK(globMatch("XC?racer", "XCTracer"));
  CHECK(!globMatch("XC?racer", "XCracer"));
  CHECK(!globMatch("XC?", "XCTR"));
  CHECK(!globMatch("abc", "abcd"));
  CHECK(!globMatch("abcd", "abc"));
  CHECK(globMatch("a*b*c", "aXXbYYc"));
  CHECK(!globMatch("XC*", ""));

  DevAddr saved = mk(1, ADDR_RANDOM_STATIC);
  CHECK(!shouldAutoConnect(false, saved, "XCT", saved, "XCT"));               // nothing saved
  CHECK(!shouldAutoConnect(true, saved, "XCT", mk(2, ADDR_RANDOM_STATIC), "XCT"));   // same name, other unit
  CHECK(shouldAutoConnect(true, saved, "XCT", saved, "XCT"));
  CHECK(shouldAutoConnect(true, saved, "XCT", saved, "renamed"));             // exact address wins
  DevAddr priv = mk(1, ADDR_RANDOM_PRIVATE_RESOLVABLE);
  CHECK(shouldAutoConnect(true, priv, "XCT", mk(9, ADDR_RANDOM_PRIVATE_RESOLVABLE), "XCT"));
  CHECK(!shouldAutoConnect(true, priv, "XCT", mk(9, ADDR_RANDOM_PRIVATE_RESOLVABLE), "Other"));
  DevAddr priv2 = mk(1, ADDR_RANDOM_PRIVATE_NON_RESOLVABLE);
  CHECK(shouldAutoConnect(true, priv2, "XCT", mk(9, ADDR_PUBLIC), "XCT"));
  CHECK(!shouldAutoConnect(true, mk(1, ADDR_PUBLIC), "XCT", mk(9, ADDR_PUBLIC), "XCT"));

  int8_t rssi[5] = {-70, -40, -90, -40, -55};
  int order[5];
  orderByRssi(rssi, 5, order);
  CHECK(order[0] == 1 && order[1] == 3 && order[2] == 4 && order[3] == 0 && order[4] == 2);
  orderByRssi(rssi, 0, order);   // empty is fine
}

static void test_command_decode() {
  Command c;
  { uint8_t d[] = {CMD_SET_TZ, 0x78, 0x00}; CHECK(decodeCommand(d, 3, c) && c.type == Command::TZ && c.value == 120); }
  { uint8_t d[] = {CMD_SET_TZ, 0x88, 0xFF}; CHECK(decodeCommand(d, 3, c) && c.value == -120); }   // signed
  { uint8_t d[] = {CMD_SET_TZ, 0x78}; CHECK(!decodeCommand(d, 2, c)); }
  { uint8_t d[] = {CMD_SIM, 5}; CHECK(decodeCommand(d, 2, c) && c.type == Command::SIM && c.value == 1); }
  { uint8_t d[] = {CMD_SIM, 0}; CHECK(decodeCommand(d, 2, c) && c.value == 0); }
  { uint8_t d[] = {CMD_SIM}; CHECK(!decodeCommand(d, 1, c)); }
  { uint8_t d[] = {CMD_CAL, 2}; CHECK(decodeCommand(d, 2, c) && c.type == Command::CAL && c.value == 2); }
  { uint8_t d[] = {CMD_CAL}; CHECK(!decodeCommand(d, 1, c)); }
  { uint8_t d[] = {CMD_PAGE, 1}; CHECK(decodeCommand(d, 2, c) && c.type == Command::PAGE && c.value == 1); }
  { uint8_t d[] = {CMD_PAGE}; CHECK(!decodeCommand(d, 1, c)); }
  { uint8_t d[] = {CMD_PAGE, 3}; CHECK(decodeCommand(d, 2, c) && c.type == Command::PAGE && c.value == 3); }
  { uint8_t d[] = {CMD_PAGE, 4}; CHECK(!decodeCommand(d, 2, c)); }    // unknown page: ignored
  { uint8_t d[] = {CMD_PAGE, 255}; CHECK(!decodeCommand(d, 2, c)); }
  { uint8_t d[] = {CMD_XCTRC, '$', 'X', 'C'}; CHECK(decodeCommand(d, 4, c) && c.type == Command::XCTRC && strcmp(c.text, "$XC") == 0); }
  { uint8_t d[] = {CMD_SET_FILTER, 'X', 'C', '*'}; CHECK(decodeCommand(d, 4, c) && c.type == Command::FILTER && strcmp(c.text, "XC*") == 0); }
  { uint8_t d[] = {CMD_SET_FILTER}; CHECK(decodeCommand(d, 1, c) && c.type == Command::FILTER && c.text[0] == 0); }   // empty = match all
  {
    uint8_t d[300];
    memset(d, 'A', sizeof d);
    d[0] = CMD_XCTRC;
    Command big;
    CHECK(decodeCommand(d, 244, big));
    CHECK(strlen(big.text) == sizeof(big.text) - 1 && big.text[sizeof(big.text) - 1] == 0);
  }
  CHECK(!decodeCommand(nullptr, 0, c));
  { uint8_t d[] = {0x7F, 1, 2}; CHECK(!decodeCommand(d, 3, c)); }
  { uint8_t d[] = {CMD_CLEAR}; CHECK(!decodeCommand(d, 1, c)); }
  { uint8_t d[9] = {CMD_BLIT}; CHECK(!decodeCommand(d, 9, c)); }
  { uint8_t d[] = {CMD_DATA, 1, 2}; CHECK(!decodeCommand(d, 3, c)); }
  { uint8_t d[] = {CMD_FLUSH}; CHECK(!decodeCommand(d, 1, c)); }
}

static void test_show_raw() {
  MockPanel p;
  static uint8_t frame[400 * 240 / 8];
  memset(frame, 0xFF, sizeof frame);
  frame[(5 * 400 + 11) / 8] &= (uint8_t)~(1u << (11 & 7));   // pixel (11,5) black
  int before = p.refreshes;
  p.setRotation(0);
  p.showRaw(frame);
  CHECK(p.refreshes == before + 1);
  CHECK(p.rawPixel(11, 5) == 0);
  CHECK(p.rawPixel(12, 5) == 1);
  CHECK(p.rawPixel(10, 5) == 1);
}

static void test_heading_smoother() {
  puts("heading smoother");
  // 350 -> 10 goes through 0
  { HeadingSmoother h; uint32_t t = 0;
    h.update(350, t);
    bool ok = true; float prev = 350;
    for (int i = 0; i < 100; i++) {
      t += 10; float v = h.update(10, t);
      if (!(v >= 345 || v <= 15)) ok = false;
      if (v > 15 && v < prev) ok = false;         // never turns back the long way
      prev = v;
    }
    CHECK(ok); CHECK(h.value() == 10.0f && h.settled()); }
  // 10 -> 350 the short way too
  { HeadingSmoother h; uint32_t t = 0;
    h.update(10, t);
    bool ok = true;
    for (int i = 0; i < 100; i++) { t += 10; float v = h.update(350, t); if (!(v >= 345 || v <= 15)) ok = false; }
    CHECK(ok); CHECK(h.value() == 350.0f); }
  // frame-rate independence
  { HeadingSmoother a, b; a.update(0, 0); b.update(0, 0);
    float va = 0, vb = 0;
    for (uint32_t t = 10; t <= 600; t += 10) va = a.update(90, t);
    for (uint32_t t = 150; t <= 600; t += 150) vb = b.update(90, t);
    NEAR(va, vb, 0.5); CHECK(va > 70 && va < 90); }
  // first value snaps; NaN gives NaN and the next value snaps again
  { HeadingSmoother h; CHECK(h.settled());
    NEAR(h.update(123, 1000), 123, 1e-6); CHECK(h.settled());
    CHECK(!isfinite(h.update(NAN, 1010))); CHECK(h.settled());
    NEAR(h.update(300, 1020), 300, 1e-6); }
  // a long gap snaps, a short one does not
  { HeadingSmoother h; h.update(0, 0);
    float v = h.update(90, 250); CHECK(v > 40 && v < 90 && !h.settled());
    NEAR(h.update(180, 250 + 2001), 180, 1e-6);
    v = h.update(270, 2251 + 300); CHECK(v < 270 && v > 180); }
  // settles within about 2 s, exactly on the target
  { HeadingSmoother h; h.update(0, 0);
    uint32_t t = 0; float v = 0;
    while (t < 2000) { t += 10; v = h.update(137.3f, t); }
    CHECK(h.settled()); CHECK(v == 137.3f); }
  // tau is configurable
  { HeadingSmoother fast(50), slow(1000); fast.update(0, 0); slow.update(0, 0);
    CHECK(fast.update(90, 100) > slow.update(90, 100)); }
  // output always in [0, 360)
  { HeadingSmoother h; uint32_t t = 0; bool ok = true;
    float targets[] = {-720.5f, -1e-9f, 360, 359.9999f, 725, -0.0f, 1e6f, 180};
    for (float tg : targets) for (int i = 0; i < 60; i++) { t += 40; float v = h.update(tg, t); if (!(v >= 0 && v < 360)) ok = false; }
    CHECK(ok); }
}

// Full chain as the app runs it: sensor at `hz`, Compass low-pass, heading selector-free smoother.
struct ChainResult { float reach90Ms; float finalErr; };
static ChainResult chainStep(int hz, float stepDeg, float tauMs, float noiseDeg, uint32_t seed) {
  MockCompass mc;
  Compass c;
  HeadingSmoother sm(tauMs);
  Vec3 a, m;
  uint32_t period = 1000 / hz;
  srand(seed);
  auto noise = [&]() { return noiseDeg * (2.0f * rand() / (float)RAND_MAX - 1.0f); };
  uint32_t t = 0;
  for (; t < 1000; t += period) {                          // rest at 100 deg
    mc.reading(100 + noise(), 10, -5, a, m); c.update(a, m, t);
    sm.update(c.heading(), t);
  }
  uint32_t t0 = t; float reach = -1;
  for (; t < t0 + 1500; t += period) {
    mc.reading(100 + stepDeg + noise(), 10, -5, a, m); c.update(a, m, t);
    float v = sm.update(c.heading(), t);
    if (reach < 0 && fabsf(angDiff(v, 100)) >= 0.9f * stepDeg) reach = (float)(t - t0);
  }
  ChainResult r; r.reach90Ms = reach; r.finalErr = fabsf(angDiff(sm.value(), 100 + stepDeg));
  return r;
}

static void test_compass_chain() {
  puts("compass time-based filter and full chain");
  // same time span, different sample rates: same filtered value
  { MockCompass mc; Vec3 a, m, a2, m2;
    Compass lo, hi;
    mc.reading(100, 10, -5, a, m); mc.reading(190, 10, -5, a2, m2);
    lo.update(a, m, 0); hi.update(a, m, 0);
    for (uint32_t t = 100; t <= 200; t += 100) lo.update(a2, m2, t);       // 10 Hz
    for (uint32_t t = 10; t <= 200; t += 10) hi.update(a2, m2, t);         // 100 Hz
    NEAR(fabsf(angDiff(lo.heading(), hi.heading())), 0, 1.0);
    CHECK(angDiff(hi.heading(), 100) > 40);
    // 1 s later both are on the new heading
    lo.update(a2, m2, 1200); hi.update(a2, m2, 1200);
    NEAR(fabsf(angDiff(lo.heading(), 190)), 0, 1.0);
    // 80 Hz vs 160 Hz over 150 ms
    Compass p, q;
    p.update(a, m, 0); q.update(a, m, 0);
    for (uint32_t t = 0; t < 150; ) { t += 12; p.update(a2, m2, t); }       // ~83 Hz
    for (uint32_t t = 0; t < 144; ) { t += 6; q.update(a2, m2, t); }        // ~166 Hz
    NEAR(fabsf(angDiff(p.heading(), q.heading())), 0, 4.0);
    // a huge gap does not blow up (dt clamped)
    Compass g; g.update(a, m, 0); g.update(a2, m2, 100000);
    CHECK(g.valid()); CHECK(fabsf(angDiff(g.heading(), 190)) < 1.0f);
    // time going backwards (wrap) is harmless
    Compass w; w.update(a, m, 5000); w.update(a2, m2, 4000);
    CHECK(w.valid() && isfinite(w.heading())); }

  // 90 degree turn, magnetic source tuning: 90% of the step within ~250 ms
  for (int hz : {80, 40, 10}) {
    ChainResult r = chainStep(hz, 90, 90, 0, 1);
    printf("  step 90 deg at %2d Hz: 90%% reached after %.0f ms, final error %.2f\n", hz, r.reach90Ms, r.finalErr);
    CHECK(r.reach90Ms > 0 && r.reach90Ms <= (hz >= 40 ? 260 : 400));
    CHECK(r.finalErr < 0.6f);
  }

  // noisy still input (+-0.3 deg): the dial settles and stays settled, so the fast redraw stops
  { MockCompass mc; Compass c; HeadingSmoother sm(90); sm.setDeadband(0.4f, 300);
    Vec3 a, m; srand(7);
    int unsettled = 0, total = 0; uint32_t t = 0; float lo = 1e9f, hi = -1e9f;
    for (; t < 20000; t += 12) {
      float n = 0.3f * (2.0f * rand() / (float)RAND_MAX - 1.0f);
      mc.reading(200 + n, 10, -5, a, m); c.update(a, m, t);
      sm.update(c.heading(), t);
      if (t >= 3000) { total++; if (!sm.settled()) unsettled++; lo = fminf(lo, sm.value()); hi = fmaxf(hi, sm.value()); }
    }
    printf("  still, +-0.3 deg noise: unsettled %d of %d ticks, shown range %.2f deg\n", unsettled, total, hi - lo);
    CHECK(unsettled == 0);
    CHECK(hi - lo < 0.5f); }
  // raw white noise straight into the smoother (no compass filter): also settles
  { HeadingSmoother sm(90); sm.setDeadband(0.4f, 300); srand(11);
    int unsettled = 0; 
    for (uint32_t t = 0; t < 20000; t += 12) {
      sm.update(200 + 0.3f * (2.0f * rand() / (float)RAND_MAX - 1.0f), t);
      if (t >= 3000 && !sm.settled()) unsettled++;
    }
    CHECK(unsettled == 0); }
  // the deadband does not hide real movement: a steady 0.25 deg offset (inside the band) is taken over after the hold time
  { HeadingSmoother sm(90); sm.setDeadband(0.4f, 300); sm.update(100, 0);
    float early = 0, v = 0;
    for (uint32_t t = 12; t <= 4000; t += 12) { v = sm.update(100.25f, t); if (t == 240) early = v; }
    NEAR(early, 100.0, 0.001); NEAR(v, 100.25, 0.001); }
  // a step beyond the band passes at once
  { HeadingSmoother sm(90); sm.setDeadband(0.4f, 300); sm.update(100, 0);
    float v = 0; for (uint32_t t = 12; t < 1000; t += 12) v = sm.update(100.6f, t);
    NEAR(v, 100.6, 0.001); CHECK(sm.settled()); }
  // GPS-source smoothing is unchanged: no deadband by default, same glide as before
  { HeadingSmoother g; g.update(100, 0);
    float v = g.update(190, 250);                          // one tau: 1 - 1/e of 90 degrees
    NEAR(v, 100 + 90 * (1 - expf(-1.0f)), 0.01);
    g.update(190.3f, 300); CHECK(!g.settled() || g.value() == 190.3f); }
}

static void test_app_anim_cadence() {
  puts("app heading animation cadence");
  Rig r;
  r.compass.psi = 100;
  AppConfig cfg;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power, cfg);
  app.begin(0);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
  auto presentsIn = [&](uint32_t ms) { int b = r.panel.presents; run(ms); return r.panel.presents - b; };
  run(4000);
  int idle = presentsIn(1000);                    // settled: the normal cadence, about 3 per second
  CHECK(idle >= 2 && idle <= 4);
  // a 90 degree step: redraws at animEveryMs while it glides
  r.compass.psi = 190;
  run(40);
  CHECK(app.buildViewModel(now).heading < 189);   // still on the way, not jumped
  int anim = presentsIn(600);
  CHECK(anim >= 3);                                // 150 ms would give 4, 333 ms only 2
  run(4000);
  NEAR(fabsf(angDiff(app.buildViewModel(now).heading, 190)), 0, 2.0);
  int after = presentsIn(1000);                    // back to the normal cadence
  CHECK(after >= 2 && after <= 4);
  // building the view model repeatedly does not advance the animation
  r.compass.psi = 280;
  run(40);
  float a = app.buildViewModel(now).heading;
  float b = app.buildViewModel(now + 100).heading;
  float c = app.buildViewModel(now + 500).heading;
  CHECK(a == b && b == c);

  // thermal page (no magnetometer there: the GPS track drives it): animThermalEveryMs while gliding
  { Command pc; pc.type = Command::PAGE; pc.value = 1; r.laptop.cmds.push_back(pc); }
  int sec = 0;
  auto gps = [&](int course) {
    char b[160];
    snprintf(b, sizeof b, "XCTRC,2026,8,16,10,38,%d,0,43.252650,26.927967,1387.00,30.00,%d.0,0.1,,,,967.62,85", sec++, course);
    r.laptop.inject(line(b));
  };
  gps(100); run(300); gps(100); run(300);
  for (int k = 0; k < 8; k++) { gps(100); run(300); }       // settled on 100
  gps(190);
  run(60);
  CHECK(app.buildViewModel(now).heading < 189);
  int th = presentsIn(600);
  CHECK(th >= 3);                                  // 200 ms gives 3, 333 ms only 2
  for (int k = 0; k < 6; k++) { gps(190); run(300); }
  int thAfter = presentsIn(1000);
  CHECK(thAfter >= 2 && thAfter <= 4);
}

#include "robust_tests.h"
#include "sharp_frame_tests.h"

static uint32_t fakeUs = 0;
static uint32_t fakeClock() { return fakeUs += 7; }   // every call advances 7 us

static void test_prof() {
  puts("prof");
  static ViewModel vm;
  vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED;
  vm.s.fix = true; vm.s.alt = 1400; vm.s.vario = 1.5f; vm.s.speedKmh = 30; vm.s.course = 90;
  vm.heading = 90; vm.headingSrc = HDG_GPS;
  vm.wind.valid = true; vm.wind.speedKmh = 12; vm.wind.toDeg = 90;
  vm.localMinutes = 600; vm.localBattPct = 80;
  vm.trailCount = 60;
  for (int i = 0; i < vm.trailCount; i++) {
    vm.trail[i].x = 40 * sinf(i * 0.3f); vm.trail[i].y = 40 * cosf(i * 0.3f); vm.trail[i].vario = 1.5f;
  }
  vm.markCount = 1; vm.marks[0].x = 30; vm.marks[0].y = 20; vm.marks[0].avgClimb = 2.1f;
  MockPanel p;
  p.setRotation(1);

  // no clock: nothing recorded
  profSetClock(nullptr);
  profReset();
  p.clear(); vm.page = ViewModel::PAGE_COMPASS; drawLive(p.gfx(), vm, 0);
  p.clear(); vm.page = ViewModel::PAGE_THERMAL; drawLive(p.gfx(), vm, 0);
  uint32_t total = 0;
  for (int i = 0; i < P_COUNT; i++) total += gProf[i].count + gProf[i].sum + gProf[i].max;
  CHECK(total == 0);

  // fake clock: compass frame then thermal frame
  profSetClock(fakeClock);
  profReset();
  p.clear(); vm.page = ViewModel::PAGE_COMPASS; drawLive(p.gfx(), vm, 0);
  CHECK(gProf[P_LIVE].count == 1); CHECK(gProf[P_HEADER].count == 1); CHECK(gProf[P_CELLS].count == 1);
  CHECK(gProf[P_VARIO].count == 1); CHECK(gProf[P_DIAL].count == 1); CHECK(gProf[P_WCORNER].count == 1);
  CHECK(gProf[P_WLINE].count == 1);
  CHECK(gProf[P_THERMAL].count == 0);
  p.clear(); vm.page = ViewModel::PAGE_THERMAL; drawLive(p.gfx(), vm, 0);
  CHECK(gProf[P_LIVE].count == 2); CHECK(gProf[P_THERMAL].count == 1);
  CHECK(gProf[P_TRAIL].count == 1); CHECK(gProf[P_MARKS].count == 1);
  CHECK(gProf[P_DIAL].count == 1);
  // plausible: each section takes at least one clock step and nested ones fit inside their parent
  for (int i = P_LIVE; i <= P_WLINE; i++) {
    if (gProf[i].count == 0) continue;
    CHECK(gProf[i].last >= 7); CHECK(gProf[i].max >= gProf[i].last); CHECK(gProf[i].sum >= gProf[i].max);
  }
  CHECK(gProf[P_TRAIL].last + gProf[P_MARKS].last <= gProf[P_THERMAL].last);
  CHECK(gProf[P_THERMAL].last < gProf[P_LIVE].last);
  CHECK(gProf[P_HEADER].last + gProf[P_CELLS].last + gProf[P_VARIO].last <= gProf[P_LIVE].sum);

  // App sections and the report line
  profResetMax();
  CHECK(gProf[P_LIVE].max == 0 && gProf[P_LIVE].count == 2);
  char buf[300];
  profFormat(buf, sizeof buf);
  CHECK(strncmp(buf, "prof live=", 10) == 0);
  CHECK(strstr(buf, " thermal=") != nullptr); CHECK(strstr(buf, " clear=") == nullptr);   // no App frame yet

  // App frame: clear and present are timed around the panel calls
  {
    Rig r;
    App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.begin(0);
    r.laptop.sim(true);
    for (uint32_t now = 0; now < 3000; now += 10) app.tick(now);
    CHECK(gProf[P_CLEAR].count > 0); CHECK(gProf[P_PRESENT].count == gProf[P_CLEAR].count);
    CHECK(gProf[P_CLEAR].last >= 7);
  }
  profSetClock(nullptr);
  profReset();
}

int main() {
  { int ops = 0; gt::run(ops); printf("gfx: %d random canvas ops compared\n", ops); }
  test_spsc_ring();
  test_linediff();
  test_sharp_pack();
  test_devmatch();
  test_command_decode();
  test_show_raw();
  test_prof();
  test_xctrc();
  test_wind();
  test_flight();
  test_wind_ekf();
  test_wind_profile();
  test_temp_profile();
  test_flight_temperature();
  test_app_temperature();
  test_home();
  test_air_trail();
  test_thermal();
  test_thermal_memory();
  test_compass();
  test_heading_selector();
  test_heading_smoother();
  test_compass_chain();
  test_ahrs();
  test_mount();
  test_button();
  test_app();
  test_app_heading();
  test_app_fusion();
  test_app_anim_cadence();
  test_ambient();
  test_standalone_cadence();
  test_fuzz_parser();
  test_fuzz_app();
  test_known_bug_nonfinite_position();
  test_time_wrap();
  if (knownBugs) printf("\n%d known firmware bugs hit (see BUG: comments in robust_tests.h)\n", knownBugs);
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
