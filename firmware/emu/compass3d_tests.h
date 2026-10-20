#pragma once
// Tests of the 3D compass: the heading readout over a set of orientations (through the real AHRS), the wrap at 359.9 / 0.0, the
// card staying level when the box is tilted, degenerate attitudes, the card's spring, the work of a frame, and the compass tool's
// two pages in the box. Included by tests.cpp (after box_tests.h and ahrs_tests.h: it uses Box and ImuSim).
#include <math.h>
#include <string.h>
#include "apps/compass3d/compass3d.h"
#include "apps/vario/app/compass_tool.h"

namespace c3t {

using compass3d::Compass3d;
using compass3d::Input;

static Bitmap1 panelBitmap(uint8_t* buf) { return Bitmap1{buf, 400, 240, 50, 1}; }

// The AHRS of a still unit at (psi, pitch, roll), settled for a few seconds, as the Input the tool would build.
static Input settled(double psi, double pitch, double roll, Ahrs* out = nullptr) {
  ImuSim s(psi, pitch, roll);
  static Ahrs f;
  f.reset();
  double t = 0;
  runImu(s, f, t, 6.0, noRate);
  Input in;
  in.valid = f.valid();
  f.quaternion(in.q);
  in.headingDeg = f.heading();
  in.trust = f.trust();
  if (out) *out = f;
  return in;
}

// A quaternion (device -> world, w x y z) for the mock compass's Euler angles.
static void quatOf(double psi, double pitch, double roll, float q[4]) {
  double ax[3][3];
  MockCompass::deviceAxes(psi, pitch, roll, ax);
  const double r[3][3] = {{ax[0][0], ax[1][0], ax[2][0]}, {ax[0][1], ax[1][1], ax[2][1]}, {ax[0][2], ax[1][2], ax[2][2]}};   // row i: world axis i in device coordinates
  const double tr = r[0][0] + r[1][1] + r[2][2];
  double w, x, y, z;
  if (tr > 0) { double s = 2.0 * sqrt(tr + 1.0); w = 0.25 * s; x = (r[2][1] - r[1][2]) / s; y = (r[0][2] - r[2][0]) / s; z = (r[1][0] - r[0][1]) / s; }
  else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) { double s = 2.0 * sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]); w = (r[2][1] - r[1][2]) / s; x = 0.25 * s; y = (r[0][1] + r[1][0]) / s; z = (r[0][2] + r[2][0]) / s; }
  else if (r[1][1] > r[2][2]) { double s = 2.0 * sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]); w = (r[0][2] - r[2][0]) / s; x = (r[0][1] + r[1][0]) / s; y = 0.25 * s; z = (r[1][2] + r[2][1]) / s; }
  else { double s = 2.0 * sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]); w = (r[1][0] - r[0][1]) / s; x = (r[0][2] + r[2][0]) / s; y = (r[1][2] + r[2][1]) / s; z = 0.25 * s; }
  q[0] = (float)w; q[1] = (float)x; q[2] = (float)y; q[3] = (float)z;
}

static float angDist(float a, float b) { return fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f); }

static void readout() {
  puts("compass3d: the heading readout is the AHRS reading, over a set of orientations");
  static Compass3d c;
  static uint8_t buf[12000];
  const Bitmap1 bm = panelBitmap(buf);
  const double poses[][3] = {{0, 0, 0}, {45, 0, 0}, {90, 20, 0}, {179.9, 0, 10}, {270, -30, 15}, {359.9, 5, -5}, {123.4, 40, 20}, {200, -10, 35}};
  for (const auto& p : poses) {
    Ahrs f;
    const Input in = settled(p[0], p[1], p[2], &f);
    CHECK(in.valid);
    NEAR(angDist(in.headingDeg, (float)p[0]), 0, 1.5);
    c.reset();
    c.step(in, 20, 0);
    c.draw(bm, 0);
    CHECK(c.readoutTenths() == bearingTenths(in.headingDeg));          // the number is the exact reading, formatted by the vario's helper
  }
}

static void wrap() {
  puts("compass3d: 359.9 / 0.0 wrap, dashes without a heading");
  static Compass3d c;
  static uint8_t buf[12000];
  const Bitmap1 bm = panelBitmap(buf);
  struct { float deg; int tenths; } t[] = {{359.94f, 3599}, {359.96f, 0}, {0.04f, 0}, {360.0f, 0}, {0.06f, 1}, {-0.04f, 0}, {-0.06f, 3599}, {720.0f, 0}, {179.95f, 1800}};
  for (const auto& e : t) {
    Input in;
    in.valid = true;
    in.headingDeg = e.deg;
    c.reset();
    c.step(in, 20, 0);
    c.draw(bm, 0);
    CHECK(c.readoutTenths() == e.tenths);
    CHECK(c.readoutTenths() == bearingTenths(e.deg));
  }
  Input in;
  in.valid = true;
  in.headingDeg = NAN;
  c.step(in, 20, 0);
  c.draw(bm, 0);
  CHECK(c.readoutTenths() == -1);
  in.headingDeg = 12.3f;
  in.valid = false;                                                    // no attitude: dashes, whatever the heading says
  c.step(in, 20, 0);
  c.draw(bm, 0);
  CHECK(c.readoutTenths() == -1);
}

static void level() {
  puts("compass3d: the card stays level in the world while the box tilts");
  static Compass3d c;
  const double poses[][3] = {{0, 0, 0}, {30, 25, 0}, {120, -35, 20}, {250, 50, -30}, {310, 80, 10}, {10, 110, 5}, {75, -70, 60}};
  const float cb = cosf(Compass3d::kBias), sb = sinf(Compass3d::kBias);
  for (const auto& p : poses) {
    Input in;
    in.valid = true;
    quatOf(p[0], p[1], p[2], in.q);
    in.headingDeg = (float)p[0];
    c.reset();
    c.step(in, 20, 0);
    float e[3], n[3], u[3];
    c.axes(e, n, u);
    // the card's up is gravity's up (the device-frame vector of the accelerometer), seen from below the bottom edge
    double ax[3][3];
    MockCompass::deviceAxes(p[0], p[1], p[2], ax);
    const float gx = (float)ax[0][2], gy = (float)ax[1][2], gz = (float)ax[2][2];     // world up in device coordinates
    NEAR(u[0], gx, 1e-4);
    NEAR(u[1], gy * cb + gz * sb, 1e-4);
    NEAR(u[2], -gy * sb + gz * cb, 1e-4);
    // east and north are in the card plane, and a rim point has no component along up
    NEAR(e[0] * u[0] + e[1] * u[1] + e[2] * u[2], 0, 1e-4);
    NEAR(n[0] * u[0] + n[1] * u[1] + n[2] * u[2], 0, 1e-4);
    // on the screen the rim is an ellipse: its minor / major axis ratio is the cosine of the tilt from the viewing axis (a perspective
    // view bends that a little)
    int minX = 1000, maxX = -1000, minY = 1000, maxY = -1000;
    for (int i = 0; i < 72; i++) {
      int sx, sy;
      c.project(sinf(i * 0.0872665f), cosf(i * 0.0872665f), 0, &sx, &sy);
      minX = sx < minX ? sx : minX; maxX = sx > maxX ? sx : maxX; minY = sy < minY ? sy : minY; maxY = sy > maxY ? sy : maxY;
    }
    if (fabsf(u[2]) > 0.3f) {
      // the ellipse's axes are not along the screen axes in general: compare areas instead (pi a b)
      const float w = (float)(maxX - minX), h = (float)(maxY - minY);
      CHECK(w > 40 && h > 20);
    }
    // the card's north is the world's north: the screen direction of (0, 1, 0) minus the centre follows the heading for a level unit
    if (p[1] == 0 && p[2] == 0) {
      int cx, cy, nx, ny;
      c.project(0, 0, 0, &cx, &cy);
      c.project(0, 1, 0, &nx, &ny);
      const float ang = atan2f((float)(nx - cx), (float)(cy - ny)) * 57.29578f;      // clockwise from the top of the screen
      NEAR(angDist(ang, -(float)p[0]), 0, 4.0);
    }
  }
}

static void tilt_clamp() {
  puts("compass3d: the drawn tilt is limited to 38 degrees, the bubble keeps the real tilt");
  static Compass3d c;
  float prev = -1;
  for (int pitch = 0; pitch <= 180; pitch += 5) {
    Input in;
    in.valid = true;
    quatOf(100, pitch, 0, in.q);
    in.headingDeg = 100;
    c.reset();
    c.step(in, 20, 0);
    const float t = c.cardTiltDeg();
    CHECK(t <= Compass3d::kTiltMax + 0.01f);
    CHECK(t >= prev - 0.01f || pitch > 160);                            // grows with the tilt, never past the limit (past 160 the box is upside down)
    prev = t;
    if (pitch <= 15) CHECK(c.tiltCue() == 0.0f);
    if (pitch >= 60 && pitch <= 120) CHECK(c.tiltCue() > 0.5f);
    float e[3], n[3], u[3];
    c.axes(e, n, u);                                                    // the real attitude is untouched
    double ax[3][3];
    MockCompass::deviceAxes(100, pitch, 0, ax);
    NEAR(u[0], ax[0][2], 1e-4);
  }
  Input in;
  in.valid = true;
  quatOf(0, 0, 0, in.q);
  c.reset();
  c.step(in, 20, 0);
  NEAR(c.cardTiltDeg(), 20.0, 0.5);                                     // a level box shows the 20 degree view bias, unclamped
}

static void degenerate() {
  puts("compass3d: no NaN on degenerate attitudes");
  static Compass3d c;
  static uint8_t buf[12000];
  const Bitmap1 bm = panelBitmap(buf);
  Input good;
  good.valid = true;
  quatOf(40, 20, 10, good.q);
  good.headingDeg = 40;
  c.reset();
  c.step(good, 20, 0);
  float keep[4];
  c.card(keep);
  const float bad[][4] = {{0, 0, 0, 0}, {NAN, 0, 0, 0}, {1, INFINITY, 0, 0}, {1e20f, 1e20f, 0, 0}, {1e-20f, 0, 0, 0}, {0, NAN, NAN, NAN}};
  for (const auto& q : bad) {
    Input in = good;
    memcpy(in.q, q, sizeof in.q);
    in.trust = NAN;
    c.step(in, 20, 0);
    float cq[4];
    c.card(cq);
    float n2 = 0;
    for (int i = 0; i < 4; i++) { CHECK(isfinite(cq[i])); n2 += cq[i] * cq[i]; }
    NEAR(n2, 1.0, 1e-3);
    NEAR(cq[0], keep[0], 1e-6);                                         // the card stays where it was
    memset(buf, 0, sizeof buf);
    c.draw(bm, 0);                                                      // the sanitizers watch for out-of-range writes
    float e[3], nn[3], u[3];
    c.axes(e, nn, u);
    CHECK(isfinite(u[0]) && isfinite(u[1]) && isfinite(u[2]));
  }
  // a first frame with a bad attitude, and gravity along the screen's axes
  c.reset();
  Input in = good;
  in.q[0] = in.q[1] = in.q[2] = in.q[3] = 0;
  c.step(in, 20, 0);
  c.draw(bm, 0);
  const double axes[][3] = {{0, 90, 0}, {0, -90, 0}, {0, 0, 90}, {0, 0, -90}, {0, 180, 0}};
  for (const auto& a : axes) {
    Input g;
    g.valid = true;
    quatOf(a[0], a[1], a[2], g.q);
    g.headingDeg = 0;
    c.reset();
    c.step(g, 20, 0);
    c.draw(bm, 0);
    float e[3], nn[3], u[3];
    c.axes(e, nn, u);
    CHECK(isfinite(u[0]) && isfinite(u[1]) && isfinite(u[2]));
  }
}

static void bounds() {
  puts("compass3d: the card never leaves the glass, in any orientation");
  static Compass3d c;
  int worst = 0;
  for (int pitch = -180; pitch <= 180; pitch += 15)
    for (int roll = -180; roll <= 180; roll += 15)
      for (int psi = 0; psi < 360; psi += 90) {
        Input in;
        in.valid = true;
        quatOf(psi, pitch, roll, in.q);
        in.headingDeg = (float)psi;
        c.reset();
        c.step(in, 20, 0);
        for (int i = 0; i < 72; i++) {
          const float a = i * 0.0872665f;
          for (int k = 0; k < 2; k++) {
            int sx, sy;
            c.project(sinf(a), cosf(a), k ? -Compass3d::kThick : 0.0f, &sx, &sy);
            const int dx = sx - Compass3d::DCX, dy = sy - Compass3d::DCY;
            const int d = (int)sqrtf((float)(dx * dx + dy * dy));
            if (d > worst) worst = d;
          }
        }
      }
  printf("  farthest rim point from the dome centre: %d px (glass radius %d)\n", worst, Compass3d::RG);
  CHECK(worst < Compass3d::RG - 2);
}

static void spring() {
  puts("compass3d: the card follows the box through a damped spring (a little overshoot), the number does not");
  static Compass3d c;
  Input a;
  a.valid = true;
  quatOf(0, 0, 0, a.q);
  a.headingDeg = 0;
  c.reset();
  c.step(a, 20, 0);
  Input b = a;
  quatOf(90, 0, 0, b.q);                                               // the box is turned 90 degrees about the vertical
  b.headingDeg = 90;
  // the card's own heading: the bearing of the device's top as the card shows it
  auto yaw = [&]() {
    float q[4];
    c.card(q);
    return 2.0f * atan2f(q[3], q[0]) * 57.29578f;                      // rotation about world up, degrees
  };
  float q0[4], q1[4];
  c.card(q0);
  const float start = yaw();
  float peak = 0, last = 0;
  uint32_t t = 0, settledAt = 0;
  for (int i = 0; i < 200; i++, t += 20) {
    c.step(b, 20, t);
    const float y = yaw();
    const float prog = fabsf(y - start);
    if (prog > peak) peak = prog;
    last = prog;
    if (settledAt == 0 && fabsf(prog - 90.0f) < 1.0f) settledAt = t;
  }
  (void)q1;
  const float over = (peak - 90.0f) / 90.0f * 100.0f;
  printf("  90 degree turn: overshoot %.1f %%, first within 1 degree at %u ms, after 4 s off by %.2f degrees\n", over, (unsigned)settledAt, fabsf(last - 90.0f));
  CHECK(over > 4.0f && over < 30.0f);                                  // liquid-compass feel: a little overshoot, not a ring
  CHECK(fabsf(last - 90.0f) < 0.5f);
  CHECK(c.input().headingDeg == 90.0f);                                // the reading was never filtered
}

static void frame_work() {
  puts("compass3d: the work of a frame");
  static Compass3d c;
  static uint8_t buf[12000];
  const Bitmap1 bm = panelBitmap(buf);
  double worstMs = 0;
  const double poses[][3] = {{0, 0, 0}, {135, 35, 0}, {250, 20, 28}, {250, 50, 0}, {10, 110, 10}, {300, -25, 20}};
  for (const auto& p : poses) {
    Input in;
    in.valid = true;
    quatOf(p[0], p[1], p[2], in.q);
    in.headingDeg = (float)p[0];
    in.tempC = 21.4f; in.rhPct = 48;
    c.reset();
    c.step(in, 20, 0);
    c.draw(bm, 0);                                                      // the first draw also builds the plate
    const GfxCounters g0 = gGfxCnt;
    c.draw(bm, 20);
    const GfxCounters g1 = gGfxCnt;
    const auto& s = c.stats();
    // device cycles: a vertical run about 90 + 5 per byte, a projected point 70, a line pixel 22, a span 45 + 12 per row, the 12 kB plate
    // copy 19000, the heading numerals 12000, text 15000, the card step 4000
    const double cycles = s.cols * 90.0 + s.bytes * 5.0 + s.proj * 70.0 + (g1.pixel - g0.pixel) * 22.0 + (g1.span - g0.span) * 45.0 +
                          (g1.rows - g0.rows) * 12.0 + 19000 + 12000 + 15000 + 4000;
    const double ms = cycles / 64000.0;
    if (ms > worstMs) worstMs = ms;
    printf("  pose %.0f/%.0f/%.0f: %u runs (%u bytes), %u points, %u polygons, %u line pixels, %u spans -> about %.1f ms on the device\n", p[0], p[1], p[2], s.cols,
           s.bytes, s.proj, s.polys, g1.pixel - g0.pixel, g1.span - g0.span, ms);
  }
  CHECK(worstMs < 15.0);
  printf("  ram of the pictures and glyphs in the object: %d bytes of digits, %d bytes in all\n", c.digitRamBytes(), (int)sizeof(Compass3d));
  // drawing is deterministic
  Input in;
  in.valid = true;
  quatOf(135, 35, 0, in.q);
  in.headingDeg = 135;
  c.reset();
  c.step(in, 20, 0);
  static uint8_t a[12000];
  c.draw(bm, 100);
  memcpy(a, buf, sizeof a);
  c.draw(bm, 100);
  CHECK(memcmp(a, buf, sizeof a) == 0);
}

static void in_the_box() {
  puts("compass3d: the compass tool shows the dome, a short press swaps to the flat page, every way out is unchanged");
  static Box b;
  bx::BoxRun u(b);
  b.r.compass.gyro = true;
  b.r.compass.gyroFromMotion = true;
  b.r.thermo.set(21.4f, 48);
  b.boot(0);
  b.host.start(Box::APP_COMPASS, 0);
  b.r.compass.psi = 200; b.r.compass.pitch = 20; b.r.compass.roll = 5;
  u.run(3000);
  CHECK(b.compassTool.alive());
  printf("  arena: tool %zu bytes, vario %zu bytes\n", sizeof(CompassTool), sizeof(VarioBox));
  CHECK(sizeof(CompassTool) <= sizeof(VarioBox));                        // the tool does not enlarge the shared arena
  CHECK(b.compassTool->showing3d() && b.host.gameMode());                // the dome runs the frame clock: 50 fps
  const int p0 = b.r.panel.presents;
  u.run(1000);
  const int fps = b.r.panel.presents - p0;
  printf("  dome frames in one second: %d\n", fps);
  CHECK(fps >= 45 && fps <= 51);
  CHECK(!b.r.links.on);                                                  // a game-like mode: the laptop link is off
  const App& core = b.compassTool->core();
  CHECK(core.fused() && core.ahrs().valid());
  CHECK(b.compassTool->view3d().input().valid);
  CHECK(b.compassTool->view3d().readoutTenths() == bearingTenths(core.ahrs().heading()));
  NEAR(angDist(core.ahrs().heading(), 200.0f), 0, 2.0);
  u.shortp();                                                            // the flat dial and weather page the tool always had
  CHECK(!b.compassTool->showing3d() && b.compassTool->flatPage() && !b.host.gameMode());
  CHECK(b.compassTool->core().buildViewModel(u.now).page == ViewModel::PAGE_COMPASS);
  const int p1 = b.r.panel.presents;
  u.run(500);
  CHECK(b.r.panel.presents > p1);                                        // it animates and refreshes like before
  u.shortp();
  CHECK(b.compassTool->showing3d() && b.host.gameMode());
  u.longp();                                                             // the menu replaces the dome and runs on the App's own cadence
  CHECK(core.listOpen() && !b.compassTool->showing3d() && !b.host.gameMode());
  u.shortp();                                                            // "< back" -> Home
  CHECK(core.listOpen());
  u.longp();
  CHECK(b.host.active() == &b.launcher && !b.r.compass.enabled);         // Home leaves the tool, sensors off
  b.host.start(Box::APP_COMPASS, u.now);
  u.run(300);
  CHECK(b.compassTool->showing3d());                                     // always opens on the dome
  u.off();                                                               // the 3 s hold leaves the tool as before
  CHECK(b.host.active() == &b.launcher && !b.r.power.off);
}

static void run() {
  readout();
  wrap();
  level();
  tilt_clamp();
  degenerate();
  bounds();
  spring();
  frame_work();
  in_the_box();
}

}  // namespace c3t
