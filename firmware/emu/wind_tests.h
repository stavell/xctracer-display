#pragma once
// Regression tests for the wind estimate (EKF lock-on in glide, lock-out after a wrong state, restart from the circle
// fit). Included by tests.cpp after CHECK / NEAR / circle() / smp(); run() is called from main.
// Background: NOTES.md "Wind EKF locked high".
#include <math.h>
#include <stdio.h>
#include "apps/vario/core/flight.h"
#include "apps/vario/core/windekf.h"
#include "wind_fixtures.h"

namespace wind_tests {

static double kmhVec(double e, double n) { return hypot(e, n); }      // e, n already in km/h

// Feeds a fixture (speed 0.1 km/h, course 0.1 deg per second) through a FlightModel and calls `each(sec)` after every second.
template <class F>
static void replay(FlightModel& fm, const uint16_t (*sc)[2], int n, F each) {
  double x = 0, y = 0;
  fm.reset();
  for (int i = 0; i < n; i++) {
    double v = sc[i][0] / 36.0, c = sc[i][1] * 0.1 * D2R;       // m/s, rad
    x += v * sin(c); y += v * cos(c);
    fm.feed(smp(i, true, 43.0 + y / 111320.0, 26.0 + x / (111320.0 * cos(43 * D2R)), sc[i][0] * 0.1f, sc[i][1] * 0.1f, 0, 1400));
    each(i);
  }
}

// Wind vector (east, north) in km/h of the shown wind.
static void shown(const FlightModel& fm, double& e, double& n) {
  Wind w = fm.wind();
  double r = w.toDeg * D2R;
  e = w.speedKmh * sin(r); n = w.speedKmh * cos(r);
}

// The cause of the bug, minimal: a straight glide whose course wanders by +-20 degrees and whose airspeed swings between
// 6 and 13 m/s (pilot on and off the bar), in a 12 km/h wind. There is no turn, so the wind is not observable. The old
// filter explained the scatter with a huge wind and an airspeed pinned at its 5 m/s floor and was "confident" at 50 km/h.
static void test_glide_scatter_teaches_nothing() {
  puts("wind EKF in a scattered glide");
  WindEkf e;
  e.reset();
  double wx = 12 / 3.6 * sin(280 * D2R), wy = 12 / 3.6 * cos(280 * D2R);
  int confident = 0;
  for (int i = 0; i < 300; i++) {
    double va = 10 + 2.5 * sin(2 * M_PI * i / 17.0) + 1.0 * sin(2 * M_PI * i / 5.3);
    double hdg = 325 + 14 * sin(2 * M_PI * i / 23.0) + 6 * sin(2 * M_PI * i / 7.1);
    double gx = va * sin(hdg * D2R) + wx, gy = va * cos(hdg * D2R) + wy;
    e.step(1, true, (float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) / D2R + 360, 360));
    if (e.confident()) confident++;
  }
  CHECK(confident == 0);
  CHECK(e.va() > 7);                       // not driven to the floor
}

// A wrong state with a small covariance rejected every measurement (3 sigma gate) for ever. A run of outliers in turning
// flight must reopen the covariance so the next turns pull the state back.
static void test_ekf_recovers_from_wrong_state() {
  puts("wind EKF recovers from a wrong confident state");
  static TrackPoint t[60];
  WindEkf e;
  e.reset();
  circle(t, 60, 36, 12, 270, 18, 40);
  for (int i = 0; i < 60; i++) e.step(1, true, t[i].speedKmh, t[i].course);
  CHECK(e.confident()); NEAR(hypot(e.wx(), e.wy()) * 3.6, 12, 1.5);
  for (int k = 0; k < 3; k++) {            // wind jumps to 30 km/h from the east: far outside 3 sigma
    circle(t, 60, 36, 30, 90, 18, 40 + 60 * k);
    for (int i = 0; i < 60; i++) e.step(1, true, t[i].speedKmh, t[i].course);
  }
  double kmh = hypot(e.wx(), e.wy()) * 3.6, to = fmod(atan2(e.wx(), e.wy()) / D2R + 360, 360);
  printf("  after 3 circles in the new wind: %.1f km/h toward %.0f, sigma %.2f\n", kmh, to, e.sigmaW());
  NEAR(kmh, 30, 4); CHECK(fabs(fmod(to - 90 + 540, 360) - 180) <= 15);
}

// The real flights. SVE-09 from 4000 s: the first circle ends at about 340 s, wind there 8-16 km/h toward 260-300. The old
// EKF was "confident" at 45 km/h from 205 s on. sopot from 1500 s: wind 5-7 km/h toward about 300; the old EKF was confident at 27 km/h.
// Whatever is shown must be absent or close to the independent reference (host/wind_ref.py); never confidently far off.
static void checkReplay(const char* name, const uint16_t (*sc)[2], int n, double refKmh, double refTo, double tol, int firstCircleBy) {
  printf("wind from a replay: %s\n", name);
  static FlightModel fm;
  double re = refKmh * sin(refTo * D2R), rn = refKmh * cos(refTo * D2R);
  int shownSecs = 0, worstSec = -1, confidentBeforeCircle = 0;
  double worst = 0;
  replay(fm, sc, n, [&](int i) {
    if (i < firstCircleBy && fm.ekfConfident()) confidentBeforeCircle++;
    Wind w = fm.wind();
    if (!w.valid || w.ageS > 120) return;
    double e, nn;
    shown(fm, e, nn);
    shownSecs++;
    double err = kmhVec(e - re, nn - rn);
    if (err > worst) { worst = err; worstSec = i; }
  });
  printf("  shown %d s, worst error %.1f km/h at %d s, EKF confident before the first circle: %d s\n", shownSecs, worst, worstSec, confidentBeforeCircle);
  CHECK(confidentBeforeCircle == 0);       // no turn yet: nothing to be confident about
  CHECK(shownSecs > 30);                   // and it does get a wind once the circles are there
  CHECK(worst <= tol);
  double e, nn;
  shown(fm, e, nn);
  CHECK(fm.wind().valid);
  CHECK(kmhVec(e - re, nn - rn) <= tol);
}

static void test_replays() {
  checkReplay("SVE-09 from 4000 s", sve_SC, sve_N, 13, 300, 8, 300);
  checkReplay("sopot from 1500 s", sopot_SC, sopot_N, 7, 300, 8, 200);
}

static void run() {
  test_glide_scatter_teaches_nothing();
  test_ekf_recovers_from_wrong_state();
  test_replays();
}

}  // namespace wind_tests
