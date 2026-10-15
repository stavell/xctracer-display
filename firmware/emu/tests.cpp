// Unit tests for the pure layers and the app, run on the PC.
#include <stdio.h>
#include <string>
#include "app/app.h"
#include "core/compass.h"
#include "core/flight.h"
#include "core/heading.h"
#include "core/sim.h"
#include "core/thermal.h"
#include "core/wind.h"
#include "core/xctrc.h"
#include "mocks.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))

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
  double cx = 0, cy = 0;                                  // centre of the air column, ground metres
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
  for (int i = 0; i < 20; i++) { px += 8 + wxm; py += wym; cx += wxm; cy += wym; feed(px, py, -1.1f); }
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
    c.update(a, m);
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
        c.update(a, m);
        if (!c.valid() || fabsf(angDiff(c.heading(), (float)psi)) > 1.0f) bad++;
      }
  CHECK(bad == 0);
  // freefall: no usable gravity, no heading
  mc.reading(90, 0, 0, a, m);
  a.x = a.y = a.z = 0.2f;
  c.reset();
  c.update(a, m);
  CHECK(!c.valid());
  // hard iron: an uncalibrated offset shifts the heading, calibration removes it
  MockCompass hi;
  hi.hardIron[0] = 12; hi.hardIron[1] = -18; hi.hardIron[2] = 7;
  Compass u;
  hi.reading(90, 0, 0, a, m);
  u.update(a, m);
  CHECK(fabsf(angDiff(u.heading(), 90)) > 5.0f);
  Compass k;
  CHECK(!k.calibration().valid);
  k.startCalibration();
  for (int psi = 0; psi < 360; psi += 20)                  // sweep orientations like a person turning the unit
    for (int pitch = -90; pitch <= 90; pitch += 30)
      for (int roll = -90; roll <= 90; roll += 45) {
        hi.reading(psi, pitch, roll, a, m);
        k.update(a, m);
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
      k.update(a, m);
      worst = fmaxf(worst, fabsf(angDiff(k.heading(), (float)psi)));
    }
  printf("  worst heading error after calibration: %.1f degrees\n", worst);
  CHECK(worst < 1.5f);
  // barely moving: the calibration is not replaced
  Compass lazy;
  lazy.startCalibration();
  hi.reading(0, 0, 0, a, m);
  lazy.update(a, m);
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
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.power);
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
  // moving: the GPS track takes over and the sensors power down
  r.laptop.inject(line("XCTRC,2026,8,16,10,38,25,0,43.252650,26.927967,1387.00,30.00,250.0,0.1,,,,967.62,85"));
  run(200);
  CHECK(app.buildViewModel(now).headingSrc == HDG_GPS);
  NEAR(fabsf(angDiff(app.buildViewModel(now).heading, 250)), 0, 0.01);
  CHECK(!app.compassOn()); CHECK(!r.compass.enabled);
  // data goes stale: back to the compass
  run(4000);
  CHECK(app.compassOn());
  CHECK(app.buildViewModel(now).headingSrc == HDG_MAG);
  // the thermal page needs no magnetometer
  { Command pc; pc.type = Command::PAGE; pc.value = 1; r.laptop.cmds.push_back(pc); }
  run(200);
  CHECK(!app.compassOn());
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
  run(400);
  NEAR(fabsf(angDiff(app.buildViewModel(now).heading, 200)), 0, 4.0);
  { Rig r2; r2.settings = r.settings; r2.compass = r.compass; r2.compass.enabled = false;
    App app2(r2.panel, r2.input, r2.battery, r2.tracer, r2.laptop, r2.settings, r2.compass, r2.power);
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
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.power);
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
  CHECK(app.buildViewModel(now).page == ViewModel::PAGE_COMPASS);
  // hold 3 s = power off
  r.input.press(now + 100, 3500);
  run(4500);
  CHECK(r.power.off);
}

int main() {
  test_xctrc();
  test_wind();
  test_flight();
  test_air_trail();
  test_thermal();
  test_thermal_memory();
  test_compass();
  test_heading_selector();
  test_button();
  test_app();
  test_app_heading();
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
