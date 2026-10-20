#pragma once
// Tests for the flight simulator (apps/games/sim) and the platform's game mode (fixed-timestep host). Included by
// tests.cpp after CHECK / NEAR; run() is called from main.
#include <math.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "apps/games/sim/accel_tilt.h"
#include "platform/gfx3d/air.h"
#include "platform/gfx3d/glider.h"
#include "platform/gfx3d/height_cache.h"
#include "apps/games/sim/sim_app.h"
#include "platform/game_probe.h"
#include "platform/gfx3d/terrain_source.h"
#include "apps/games/sim/tilt.h"
#include "apps/games/sim/topdown.h"
#include "platform/gfx3d/voxel.h"
#include "platform/gfx3d/world.h"
#include "sim_reach.h"
#include "sim_shots.h"
#include "box.h"
#include "apps/vario/app/terrain_kit.h"

namespace sim_tests {
using namespace sim;

// ---- synthetic terrains
struct Flat : IHeightSource {
  float h = 1000;
  bool at(float, float, float* o) override { *o = h; return true; }
};
// A ridge along y at x = 0, 300 m high, Gaussian, on a 1000 m plain.
struct Ridge : IHeightSource {
  bool at(float x, float, float* o) override { *o = 1000.0f + 300.0f * expf(-(x * x) / (600.0f * 600.0f)); return true; }
};
// A plane rising to the north with the given rise / run.
struct Slope : IHeightSource {
  float g = 0.5f;
  bool at(float, float y, float* o) override { *o = 1000.0f + g * y; return true; }
};
// Hills: something for the thermal generator to pick triggers from.
struct Hills : IHeightSource {
  bool at(float x, float y, float* o) override {
    *o = 1200.0f + 250.0f * sinf(x * 0.0011f) * cosf(y * 0.0013f) + 120.0f * sinf((x + y) * 0.003f);
    return true;
  }
};

// Runs fn(i, result) for i < n (at most 64, 8 at a time) in forked children, one core each, and fills out[i] with what they report. The
// children inherit the statics copy-on-write, so a test keeps its single-process shape; their CHECK counts are added to ours
// (a child that dies counts as a failure). For the slow scripted-pilot runs.
struct ForkResult { double a = 0, b = 0; };
template <class F> static void forkEach(int n, F fn, ForkResult* out) {
  fflush(stdout);
  static const int kMax = 64, kPar = 8;
  int fds[kMax], running = 0;
  if (n > kMax) { CHECK(!"forkEach: too many jobs"); n = kMax; }
  for (int i = 0; i < n; i++) {
    if (running >= kPar) { if (wait(nullptr) > 0) running--; }
    int p[2];
    if (pipe(p) != 0) { CHECK(!"pipe"); fds[i] = -1; continue; }
    const pid_t pid = fork();
    if (pid == 0) {
      close(p[0]);
      checks = 0; failures = 0;
      ForkResult r;
      fn(i, r);
      const double m[4] = {(double)checks, (double)failures, r.a, r.b};
      const ssize_t w = write(p[1], m, sizeof m);
      fflush(stdout);
      _exit(w == (ssize_t)sizeof m ? 0 : 1);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); fds[i] = -1; CHECK(!"fork"); continue; }
    fds[i] = p[0];
    running++;
  }
  for (int i = 0; i < n; i++) {
    double m[4];
    size_t got = 0;
    while (fds[i] >= 0 && got < sizeof m) {
      const ssize_t k = read(fds[i], (char*)m + got, sizeof m - got);
      if (k <= 0) break;
      got += (size_t)k;
    }
    if (fds[i] >= 0) close(fds[i]);
    if (got != sizeof m) { printf("  FAIL forked job %d of %d died\n", i, n); checks++; failures++; continue; }
    checks += (int)m[0]; failures += (int)m[1];
    out[i].a = m[2]; out[i].b = m[3];
  }
  while (wait(nullptr) > 0) {}
}

static HeightCache gCache;          // 48 kB: static, the tests are not the loop task but keep the habit
static VoxelView gVoxel;

// fexp / flog2 / fpow (sim_math.h) replace expf / logf / powf, which cost 1300 - 2500 cycles each on the device.
static void fast_math() {
  double worstE = 0, worstL = 0, worstP = 0;
  for (float x = -30.0f; x <= 10.0f; x += 0.0137f) worstE = fmax(worstE, fabs((double)fexp(x) / exp((double)x) - 1.0));
  for (float x = 1e-4f; x < 1e4f; x *= 1.013f) worstL = fmax(worstL, fabs((double)flog2(x) - log2((double)x)));
  for (float u = 0.0001f; u <= 1.0f; u += 0.0007f) worstP = fmax(worstP, fabs((double)fpow(u, 1.2f) / pow((double)u, 1.2) - 1.0));
  CHECK(worstE < 2e-6);               // grows with |x|: the argument itself is only good to 1e-7 relative
  CHECK(worstL < 1e-6);
  CHECK(worstP < 1e-6);
  CHECK(fpow(0.0f, 1.2f) == 0.0f && fexp(0.0f) == 1.0f && fexp(-1000.0f) < 1e-30f);
  NEAR(lagFactor(0.02f, 0.5f), 1.0 - exp(-0.04), 1e-7);
}

static void polar() {
  NEAR(Polar::sinkKmh(33.0f), 1.10f, 0.001);
  NEAR(Polar::sinkKmh(37.0f), 1.20f, 0.001);
  NEAR(Polar::sinkKmh(52.0f), 2.20f, 0.001);
  NEAR(Polar::sinkKmh(35.0f), 1.15f, 0.001);               // halfway between two table points
  NEAR(Polar::sinkKmh(10.0f), Polar::sinkKmh(20.0f), 1e-6); // clamped below the table
  NEAR(Polar::sinkKmh(90.0f), Polar::sinkKmh(60.0f), 1e-6);
  // Minimum sink is at 33 km/h, and the polar rises on both sides.
  float best = 99, at = 0;
  for (float v = 27; v < 58; v += 0.5f)
    if (Polar::sinkKmh(v) < best) { best = Polar::sinkKmh(v); at = v; }
  NEAR(at, 33.0f, 0.51);
  CHECK(Polar::sinkKmh(30.0f) > best && Polar::sinkKmh(40.0f) > Polar::sinkKmh(37.0f));
  float ratio;
  const float vbg = Polar::bestGlideKmh(&ratio);
  CHECK(vbg >= 36.0f && vbg <= 41.0f);
  CHECK(ratio > 8.0f && ratio < 10.0f);                    // a plausible paraglider
  // Load factor: induced drag grows with n squared.
  const float n = 1.3f;
  NEAR(Polar::sink(37.0f, n), Polar::sinkKmh(37.0f) + (n * n - 1.0f) * Polar::kInduced / (37.0f / 3.6f), 1e-5);
  CHECK(Polar::sink(37.0f, 1.0f) < Polar::sink(37.0f, 1.3f));
}

static Glider flyTurn(float bankCmd, float seconds, float bar = 0, float brake = 0) {
  Glider g;
  g.reset(0, 0, 2000, 0);
  Controls c;
  c.bank = bankCmd; c.bar = bar; c.brake = brake;
  for (int i = 0; i < (int)(seconds * 50); i++) g.step(c, 0, 0, 0, 0.02f);
  return g;
}

static void turn_and_sink() {
  // Coordinated turn: omega = g tan(bank) / V once the roll has settled.
  Glider g = flyTurn(1.0f, 8.0f);
  NEAR(g.bank, Glider::kMaxBank, 0.01);
  NEAR(g.turnRate, kG * tanf(g.bank) / g.airspeed, 1e-3);
  CHECK(g.turnRate > 0.4f && g.turnRate < 0.8f);           // 30 - 45 deg/s at 37 km/h and 40 deg of bank
  Glider l = flyTurn(-0.5f, 8.0f);
  CHECK(l.turnRate < 0);
  NEAR(l.turnRate, kG * tanf(l.bank) / l.airspeed, 1e-3);
  // The heading really changes with that rate: one full circle in about 2 pi / omega.
  Glider c;
  c.reset(0, 0, 2000, 0);
  Controls cmd; cmd.bank = 1.0f;
  float turned = 0;
  for (int i = 0; i < 50 * 20; i++) { const float h0 = c.heading; c.step(cmd, 0, 0, 0, 0.02f); turned += wrapPi(c.heading - h0); }
  NEAR(turned, c.turnRate * 20.0f, 1.5);                   // within the first second's roll-in
  // Roll lag: after one time constant the bank has covered 63 % of the way.
  Glider r;
  r.reset(0, 0, 2000, 0);
  Controls full; full.bank = 1.0f;
  for (int i = 0; i < 20; i++) r.step(full, 0, 0, 0, 0.02f);   // 0.4 s
  NEAR(r.bank / Glider::kMaxBank, 1.0 - exp(-1.0), 0.03);
  // Sink in a turn is larger by about sqrt(n) (and the speed bar changes it by the polar).
  Glider lev = flyTurn(0.0f, 6.0f);
  CHECK(lev.sinkRate > 1.15f && lev.sinkRate < 1.25f);
  CHECK(g.load > 1.18f && g.load < 1.26f);
  CHECK(g.sinkRate > lev.sinkRate * 1.10f);
  NEAR(g.sinkRate, Polar::sink(g.airspeedKmh(), g.load), 1e-4);
  // Full bar: 52 km/h, 2.2 m/s.
  Glider bar = flyTurn(0.0f, 20.0f, 1.0f);
  NEAR(bar.airspeedKmh(), 52.0, 0.5);
  NEAR(bar.sinkRate, 2.2, 0.05);
  // Vertical speed adds the air: 2 m/s of lift against 1.2 m/s of sink climbs 0.8.
  Glider up = lev;
  up.step(Controls(), 0, 0, 2.0f, 0.02f);
  NEAR(up.vz, 2.0f - up.sinkRate, 1e-4);
  // Wind carries the glider over the ground.
  Glider w;
  w.reset(0, 0, 2000, 0);
  for (int i = 0; i < 50; i++) w.step(Controls(), 5.0f, 0, 0, 0.02f);
  CHECK(w.x > 4.5f && w.x < 5.5f);
}

static void stall() {
  // Full brake in a straight glide stays above the stall speed (28 km/h against 26); in a 35 degree bank the stall speed rises to
  // 26 * sqrt(1.22) = 28.7 and the wing stalls: heavy sink and the warning flag.
  Glider a = flyTurn(0.0f, 12.0f, 0, 1.0f);
  CHECK(!a.stalled);
  Glider b = flyTurn(1.0f, 12.0f, 0, 1.0f);
  CHECK(b.stalled);
  CHECK(b.sinkRate > 4.0f);
  // Recovery: let go of the brake and the bank, the speed comes back.
  Glider c = b;
  Controls rel;
  for (int i = 0; i < 50 * 12; i++) c.step(rel, 0, 0, 0, 0.02f);
  CHECK(!c.stalled);
  CHECK(c.sinkRate < 1.5f);
}

static void tilt_map() {
  // Deadzone: nothing below 3 degrees from the zero.
  NEAR(TiltMap::bank(2.9f, 0), 0, 1e-6);
  NEAR(TiltMap::bank(-2.9f, 0), 0, 1e-6);
  CHECK(TiltMap::bank(4.0f, 0) > 0);
  // Full bank at 38 degrees, clamped beyond.
  NEAR(TiltMap::bank(38.0f, 0), 1.0, 1e-5);
  NEAR(TiltMap::bank(70.0f, 0), 1.0, 1e-6);
  NEAR(TiltMap::bank(-90.0f, 0), -1.0, 1e-6);
  // Odd symmetry and monotonic, with an expo curve: gentler than linear in the first half.
  float last = 0;
  for (float d = 0; d <= 40; d += 0.5f) {
    const float v = TiltMap::bank(d, 0);
    CHECK(v >= last - 1e-6f);
    NEAR(TiltMap::bank(-d, 0), -v, 1e-6);
    last = v;
  }
  const float mid = TiltMap::bank(3.0f + 0.5f * 35.0f, 0);
  CHECK(mid < 0.5f && mid > 0.3f);
  // Zero offset: the same command around another resting angle.
  NEAR(TiltMap::bank(15.0f, 15.0f), 0, 1e-6);
  NEAR(TiltMap::bank(15.0f + 12.0f, 15.0f), TiltMap::bank(12.0f, 0), 1e-6);
  NEAR(TiltMap::bank(15.0f - 12.0f, 15.0f), -TiltMap::bank(12.0f, 0), 1e-6);
  // Pitch: top away = bar, top toward = brake, never both.
  float bar, brake;
  TiltMap::pitch(25.0f, 5.0f, &bar, &brake);
  NEAR(bar, 1.0, 1e-6); NEAR(brake, 0, 1e-6);
  TiltMap::pitch(-30.0f, 5.0f, &bar, &brake);
  NEAR(bar, 0, 1e-6); NEAR(brake, 1.0, 1e-6);
  TiltMap::pitch(6.0f, 5.0f, &bar, &brake);
  NEAR(bar, 0, 1e-6); NEAR(brake, 0, 1e-6);
  TiltMap::pitch(5.0f + 12.0f, 5.0f, &bar, &brake);
  CHECK(bar > 0.3f && bar < 0.8f && brake == 0);
}

static void height_cache() {
  Hills hills;
  static HeightCache a, b;
  a.attach(&hills);
  const int n = a.fill(0, 0);
  CHECK(n == HeightCache::N * HeightCache::N);
  CHECK(a.complete());
  float h, want;
  hills.at(1234.0f, -2345.0f, &want);
  CHECK(a.height(1234.0f, -2345.0f, &h));
  NEAR(h, want, 3.0);                                      // bilinear on 200 m cells of a smooth surface
  CHECK(!a.height(40000.0f, 0, &h));                       // outside the window: no value
  // Moving the window by refresh() in small budgets ends with the same cells as a fresh fill there.
  a.setCentre(3300.0f, -1900.0f);
  int guard = 0, total = 0, k;
  while ((k = a.refresh(37)) > 0 && guard++ < 10000) {
    total += k;
    CHECK(k <= 37);
    // Whatever is valid is right at every moment.
    float p;
    if (a.height(3000.0f, -1900.0f, &p)) { hills.at(3000.0f, -1900.0f, &want); CHECK(fabsf(p - want) < 3.0f); }
  }
  CHECK(a.complete());
  CHECK(total < HeightCache::N * HeightCache::N / 2);      // incremental: far fewer cells than a whole window
  b.attach(&hills);
  b.fill(3300.0f, -1900.0f);
  bool same = a.x0() == b.x0() && a.x1() == b.x1() && a.y0() == b.y0() && a.y1() == b.y1();
  for (int iy = a.y0(); same && iy < a.y1(); iy++)
    for (int ix = a.x0(); ix < a.x1(); ix++)
      if (a.cellH(ix, iy) != b.cellH(ix, iy)) { same = false; break; }
  CHECK(same);
  // Shade: a flat plain is level 11; a slope facing the sun (south-west) is lighter than one facing away.
  Flat flat;
  static HeightCache f;
  f.attach(&flat);
  f.fill(0, 0);
  CHECK(f.cellShade(0, 0) == 11);
  Slope rise;                                              // rises to the north: the face looks south
  static HeightCache s;
  s.attach(&rise);
  s.fill(0, 0);
  rise.g = -0.5f;
  static HeightCache s2;
  s2.attach(&rise);
  s2.fill(0, 0);                                           // falls to the north: the face looks north, away from the sun
  CHECK(s.cellShade(0, 0) > 11 && s2.cellShade(0, 0) < 11);
  rise.g = 0.5f;
}

static void air_model() {
  Hills hills;
  gCache.attach(&hills);
  gCache.fill(0, 0);
  Air a(11), b(11), c(12);
  a.setField(&gCache); b.setField(&gCache); c.setField(&gCache);
  a.setWind(200.0f, 5.0f, 1400.0f); b.setWind(200.0f, 5.0f, 1400.0f); c.setWind(200.0f, 5.0f, 1400.0f);
  // Same seed, same day: identical thermals and identical air at any time and place; a different seed differs.
  ThermalInfo ta[Air::kMaxInfo], tb[Air::kMaxInfo], tc[Air::kMaxInfo];
  const int na = a.nearby(0, 0, 1600, 700, 6000, ta, Air::kMaxInfo);
  const int nb = b.nearby(0, 0, 1600, 700, 6000, tb, Air::kMaxInfo);
  const int nc = c.nearby(0, 0, 1600, 700, 6000, tc, Air::kMaxInfo);
  CHECK(na > 2);
  CHECK(na == nb);
  bool same = na == nb;
  for (int i = 0; i < na && same; i++) same = ta[i].x == tb[i].x && ta[i].y == tb[i].y && ta[i].strength == tb[i].strength && ta[i].id == tb[i].id;
  CHECK(same);
  bool diff = na != nc;
  for (int i = 0; i < na && i < nc && !diff; i++) diff = ta[i].x != tc[i].x || ta[i].strength != tc[i].strength;
  CHECK(diff);
  int sameSamples = 0;
  for (int i = 0; i < 200; i++) {
    const float x = -4000 + 41.7f * i, y = 3000 - 29.3f * i, z = 1300 + 3.1f * i, t = 100 + 7.9f * i;
    AirSample sa, sb;
    a.sample(x, y, z, t, &sa);
    b.sample(x, y, z, t, &sb);
    sameSamples += sa.wz == sb.wz && sa.thermal == sb.thermal && sa.ridge == sb.ridge;
  }
  CHECK(sameSamples == 200);
  // Thermals follow the rules: 2-4.6 m/s, 80-200 m cores (wider higher up), a life of 15-25 minutes (checked through the strength envelope).
  for (int i = 0; i < na; i++) {
    CHECK(ta[i].strength >= 0 && ta[i].strength <= 4.6f);
    CHECK(ta[i].radius >= 80.0f && ta[i].radius <= 200.0f * 1.9f);
  }
  // In the core the thermal lifts: at the column centre the thermal part is about the strength; far away it is not.
  const ThermalInfo& t0 = ta[0];
  AirSample core, away;
  a.sample(t0.x, t0.y, 1600, 700, &core);
  a.sample(t0.x + 4000, t0.y + 4000, 1600, 700, &away);
  CHECK(core.thermal > 0.5f * t0.strength);
  CHECK(core.wz > away.wz);
  // Cloud base: nothing lifts above it.
  AirSample high;
  a.sample(t0.x, t0.y, a.cloudBase() + 300.0f, 700, &high);
  NEAR(high.thermal, 0, 0.05);
  CHECK(a.cloudBase() >= 2200.0f && a.cloudBase() <= 2800.0f);
  // Between thermals the air sinks 0.3 - 0.6 m/s.
  CHECK(a.baseSink() >= 0.3f && a.baseSink() <= 0.6f);
  // Life cycle: a thermal that is alive at some time has no lift long before its start nor long after its end.
  int alive = 0, dead = 0;
  for (float t = 0; t < 6000; t += 60) {
    ThermalInfo x[Air::kMaxInfo];
    const int n = a.nearby(t0.x, t0.y, 1600, t, 300, x, Air::kMaxInfo);
    (n > 0 ? alive : dead)++;
  }
  CHECK(alive > 0 && dead > 0);
  // Triggers prefer sun-facing slopes: score is higher on a slope facing south than on one facing north.
  Slope rise;
  static HeightCache sc;
  sc.attach(&rise);
  sc.fill(0, 0);
  Air s(1);
  s.setField(&sc);
  const float sunny = s.triggerScore(0, 0);                // terrain rises to the north: the face looks south, to the sun
  rise.g = -0.5f;
  static HeightCache sc2;
  sc2.attach(&rise);
  sc2.fill(0, 0);
  s.setField(&sc2);
  const float shady = s.triggerScore(0, 0);
  CHECK(sunny > shady + 0.2f);
}

static void ridge_lift() {
  Ridge ridge;
  gCache.attach(&ridge);
  gCache.fill(0, 0);
  Air air(3);
  air.setField(&gCache);
  air.setWind(270.0f, 8.0f, 1000.0f);                      // from the west, blowing east: the west flank is windward
  AirSample windward, lee, crest, farUp, farAway;
  air.sample(-500.0f, 0, 1000.0f + 300.0f * expf(-(500.0f * 500.0f) / (600.0f * 600.0f)) + 60.0f, 0, &windward);
  air.sample(500.0f, 0, 1000.0f + 300.0f * expf(-(500.0f * 500.0f) / (600.0f * 600.0f)) + 60.0f, 0, &lee);
  CHECK(windward.ridge > 1.0f);                            // lift into the slope
  CHECK(lee.ridge < -0.8f);                                // sink behind it
  // Lift decays with height above the ground and vanishes far away from the ridge.
  AirSample high;
  air.sample(-500.0f, 0, 1600.0f, 0, &high);
  CHECK(high.ridge < windward.ridge * 0.3f);
  air.sample(-5000.0f, 0, 1080.0f, 0, &farAway);
  NEAR(farAway.ridge, 0, 0.05);
  // No wind, no ridge lift.
  air.setWind(270.0f, 0.0f, 1000.0f);
  air.sample(-500.0f, 0, 1300.0f, 0, &crest);
  NEAR(crest.ridge, 0, 1e-4);
  air.setWind(270.0f, 8.0f, 1000.0f);
  // The wind grows with height.
  float w0x, w0y, w1x, w1y;
  air.wind(1000.0f, &w0x, &w0y);
  air.wind(2000.0f, &w1x, &w1y);
  CHECK(w1x > w0x);
  CHECK(w0x > 7.5f && w0x < 8.5f);
  (void)farUp;
}

static void landing() {
  Flat flat;
  static World w;
  w.beginAt(&flat, 1, 0, 0, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  CHECK(w.status() == Status::FLYING);
  NEAR(w.agl(), 5.0f, 0.01);
  for (int i = 0; i < 50 * 10 && w.status() == Status::FLYING; i++) w.step(Controls(), 0.02f);
  CHECK(w.status() == Status::LANDED);                     // 5 m at 1.2 m/s: a soft landing
  NEAR(w.glider().z, 1000.0, 0.01);
  const float t = w.time();
  w.step(Controls(), 0.02f);
  CHECK(w.time() == t);                                    // the flight is over: nothing advances
  // A steep face is not a landing place.
  Slope steep;
  steep.g = 1.2f;
  static World w2;
  w2.beginAt(&steep, 1, 0, 0, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f);
                                                           // heading north: straight into the face
  for (int i = 0; i < 50 * 30 && w2.status() == Status::FLYING; i++) w2.step(Controls(), 0.02f);
  CHECK(w2.status() == Status::CRASHED);
  // Too hard: a stalled wing sinking at more than 4.5 m/s hits the ground.
  static World w3;
  w3.beginAt(&flat, 1, 0, 0, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  Controls c; c.bank = 1.0f; c.brake = 1.0f;
  for (int i = 0; i < 50 * 40 && w3.status() == Status::FLYING; i++) w3.step(c, 0.02f);
  CHECK(w3.status() == Status::CRASHED);
}

static void voxel() {
  Hills hills;
  gCache.attach(&hills);
  gCache.fill(0, 0);
  static uint8_t fa[50 * 240], fb[50 * 240], fc[50 * 240];
  memset(fa, 0, sizeof fa); memset(fb, 0xFF, sizeof fb);
  Bitmap1 a{fa, 400, 240, 50, 1}, b{fb, 400, 240, 50, 1};
  Camera cam;
  cam.x = 100; cam.y = -200; cam.z = 1500; cam.heading = 0.7f; cam.roll = 0.3f; cam.pitch = 0.05f;
  gVoxel.render(a, gCache, cam);
  const VoxelView::Stats st = gVoxel.stats();
  gVoxel.render(b, gCache, cam);                           // from a different starting buffer: every pixel is written
  CHECK(memcmp(fa, fb, sizeof fa) == 0);
  CHECK(st.columns == 240 && st.samples > 240 * 10 && st.spans >= 240);
  CHECK(st.steps <= 240 * gVoxel.stepCount());
  // Something is drawn: not all one colour, sky (white) at the top of an upright view.
  int black = 0;
  for (size_t i = 0; i < sizeof fa; i++) black += __builtin_popcount((unsigned)(uint8_t)~fa[i]);
  CHECK(black > 2000 && black < 400 * 240 - 2000);
  Camera level = cam;
  level.roll = 0;
  gVoxel.render(a, gCache, level);
  int topWhite = 0;
  for (int x = 0; x < 240; x++) topWhite += a.getPixel(x, 2);
  CHECK(topWhite > 120);                                   // the sky: a light dither, mostly white at the top
  // A different camera gives a different picture; the same camera the same one.
  gVoxel.render(b, gCache, cam);
  gVoxel.render(a, gCache, level);
  CHECK(memcmp(fa, fb, sizeof fa) != 0);
  memcpy(fc, fa, sizeof fa);
  gVoxel.render(a, gCache, level);
  CHECK(memcmp(fa, fc, sizeof fa) == 0);
  // A layout that is not the panel's draws nothing and does not crash.
  uint8_t other[8 * 8];
  Bitmap1 o{other, 64, 8, 8, 0};
  gVoxel.render(o, gCache, cam);
  CHECK(gVoxel.stats().columns == 0);
}


// ---- clouds and thermal views (sky3d)
// Hills with a 4 km wall along y = 0 (|y| < 250): whatever lies north of it is hidden from the south.
struct WallHills : IHeightSource {
  bool at(float x, float y, float* o) override {
    Hills h;
    h.at(x, y, o);
    if (fabsf(y) < 250.0f) *o += 4000.0f;
    return true;
  }
};

static int diffBits(const uint8_t* a, const uint8_t* b, size_t n) {
  int d = 0;
  for (size_t i = 0; i < n; i++) d += __builtin_popcount((unsigned)(uint8_t)(a[i] ^ b[i]));
  return d;
}

static void sky_fx() {
  static WallHills wall;
  gCache.attach(&wall);
  gCache.fill(0, 0);
  static Air air(5);
  air.setField(&gCache);
  air.setWind(190.0f, 5.0f, 1200.0f);
  // Cumulus life cycle follows the thermal: none until a fifth of its life, full at 0.4 - 0.6, none from 0.95; never listed outside its life.
  static ColumnInfo ci[48];
  int early = 0, full = 0, late = 0, seen = 0;
  uint32_t trackId = 0;
  float d1 = -1, d2 = -1, w0, w1;
  for (float t = 0; t < 5000.0f; t += 25.0f) {
    const int n = air.columns(0, 0, t, 6000.0f, ci, 48);
    for (int i = 0; i < n; i++) {
      const float a = t - ci[i].t0, L = ci[i].life;
      CHECK(a > 0 && a < L);
      CHECK(ci[i].cloud >= 0 && ci[i].cloud <= 1.0f);
      if (a < 0.2f * L) { CHECK(ci[i].cloud == 0); early++; }
      if (a > 0.42f * L && a < 0.6f * L) { CHECK(ci[i].cloud > 0.99f); CHECK(ci[i].width > 0.99f * (450.0f + 140.0f * ci[i].S)); full++; }
      if (a > 0.95f * L) { CHECK(ci[i].cloud == 0); late++; }
      CHECK(ci[i].cloudY > -1e6f);
      seen++;
      // the cumulus leaves its column's top downwind as it ages (wind blows to the NNE: to 10 degrees)
      if (!trackId && ci[i].life > 1000.0f && a > 0.45f * L && a < 0.5f * L) {
        trackId = ci[i].id;
        d1 = (ci[i].cloudX - ci[i].cx) * 0.17f + (ci[i].cloudY - ci[i].cy) * 0.98f;
        w0 = ci[i].width;
      }
      if (trackId && ci[i].id == trackId && a > 0.8f * L && a < 0.85f * L && d2 < 0) {
        d2 = (ci[i].cloudX - ci[i].cx) * 0.17f + (ci[i].cloudY - ci[i].cy) * 0.98f;
        w1 = ci[i].width;
      }
    }
  }
  CHECK(seen > 50 && early > 5 && full > 5 && late > 1);
  CHECK(d1 >= 0 && d2 > d1 + 50.0f);                       // drifted further downwind later
  CHECK(w1 < w0);                                          // thinning in the decay phase

  // Rendering: deterministic, and a cloud behind the wall is hidden by it (the depth buffer), the same cloud seen from beyond it is not.
  ColumnInfo pick;
  float pickT = 0;
  bool have = false;
  for (float t = 200.0f; t < 4000.0f && !have; t += 25.0f) {
    const int n = air.columns(0, 2500.0f, t, 4000.0f, ci, 48);
    for (int i = 0; i < n && !have; i++)
      if (ci[i].cloud > 0.95f && ci[i].y0 > 1500.0f && ci[i].cloudY > 2500.0f && ci[i].strength > 2.0f) { pick = ci[i]; pickT = t; have = true; }
  }
  CHECK(have);
  if (!have) return;
  static uint8_t fa[50 * 240], fb[50 * 240], fc[50 * 240];
  Bitmap1 a{fa, 400, 240, 50, 1}, b{fb, 400, 240, 50, 1}, c{fc, 400, 240, 50, 1};
  Glider g;
  g.reset(pick.cloudX, pick.cloudY - 3000.0f, pick.cloudY > 0 ? 1600.0f : 1600.0f, 0.0f);
  static SkyFx sky;
  sky.setUnlimited(true);                                  // every cloud in view, so this one is drawn whatever else is near
  auto scene = [&](const Bitmap1& bm, float camY, bool assist, SkyFx::Style st) {
    Camera cam;
    cam.x = pick.cloudX; cam.y = camY; cam.z = air.cloudBase() - 400.0f; cam.heading = 0.0f; cam.pitch = 0.09f;
    gVoxel.render(bm, gCache, cam);
    sky.draw(bm, gVoxel, air, g, cam, pickT, assist, st);
  };
  auto terrainOnly = [&](const Bitmap1& bm, float camY) {
    Camera cam;
    cam.x = pick.cloudX; cam.y = camY; cam.z = air.cloudBase() - 400.0f; cam.heading = 0.0f; cam.pitch = 0.09f;
    gVoxel.render(bm, gCache, cam);
  };
  // Beyond the wall (camera at y = 500, wall at y = 0): the cumulus is drawn.
  scene(a, 500.0f, false, SkyFx::Style::BOTH);
  SkyFx::CloudShot cs[SkyFx::kMaxCols];
  int n = sky.cloudShots(cs, SkyFx::kMaxCols);
  bool found = false, hidden = true;
  for (int i = 0; i < n; i++) if (cs[i].id == pick.id) { found = true; hidden = cs[i].hidden; }
  CHECK(found && !hidden);
  terrainOnly(b, 500.0f);
  CHECK(diffBits(fa, fb, sizeof fa) > 100);                // clouds changed the picture
  scene(c, 500.0f, false, SkyFx::Style::BOTH);
  CHECK(memcmp(fa, fc, sizeof fa) == 0);                   // deterministic
  scene(a, 500.0f, true, SkyFx::Style::BUBBLES);
  scene(c, 500.0f, true, SkyFx::Style::BUBBLES);
  CHECK(memcmp(fa, fc, sizeof fa) == 0);
  CHECK(sky.stats().clouds >= 1 && sky.stats().cloudCycles <= SkyFx::kCloudBudget + 1.0f);
  // In front of the wall (camera at y = -1500): the same cumulus is behind the 4 km wall; nothing of it, its shaft or its bubbles shows.
  scene(a, -1500.0f, true, SkyFx::Style::BOTH);
  terrainOnly(b, -1500.0f);
  n = sky.cloudShots(cs, SkyFx::kMaxCols);
  found = false; hidden = false;
  for (int i = 0; i < n; i++) if (cs[i].id == pick.id) { found = true; hidden = cs[i].hidden; }
  CHECK(found && hidden);
  bool nearer = false;                                     // a cumulus this side of the wall (the wall is 1250 - 1750 m away) is still drawn
  for (int i = 0; i < n; i++) {
    if (cs[i].dist > 1800.0f) CHECK(cs[i].hidden);
    else nearer = true;
  }
  if (!nearer) {
    CHECK(sky.stats().cloudCols == 0 && sky.stats().bubbles == 0);
    CHECK(memcmp(fa, fb, sizeof fa) == 0);                 // the picture is the bare terrain
  }
  // The assist adds to the clouds-only picture, and the styles differ.
  scene(a, 500.0f, false, SkyFx::Style::BOTH);
  scene(b, 500.0f, true, SkyFx::Style::COLUMNS);
  scene(c, 500.0f, true, SkyFx::Style::BUBBLES);
  CHECK(diffBits(fa, fb, sizeof fa) > 50 && diffBits(fa, fc, sizeof fa) > 20 && diffBits(fb, fc, sizeof fa) > 50);
  // The budget: no style over the budgets; a heavy case (a camera right under three thermals) degrades by level, never past them.
  for (int st = 0; st < SkyFx::kStyles; st++) {
    scene(a, 500.0f, true, (SkyFx::Style)st);
    CHECK(sky.stats().assistCycles <= SkyFx::kAssistBudget * 1.3f);
    CHECK(sky.stats().bubbles <= SkyFx::kMaxBubbles + 10);
  }
  // Bubbles rise: the picture changes with time (a bubble at a given height moves up).
  Camera cam;
  cam.x = pick.cloudX; cam.y = 500.0f; cam.z = air.cloudBase() - 400.0f; cam.heading = 0.0f; cam.pitch = 0.09f;
  gVoxel.render(a, gCache, cam);
  sky.draw(a, gVoxel, air, g, cam, pickT, true, SkyFx::Style::BUBBLES);
  gVoxel.render(b, gCache, cam);
  sky.draw(b, gVoxel, air, g, cam, pickT + 6.0f, true, SkyFx::Style::BUBBLES);
  CHECK(diffBits(fa, fb, sizeof fa) > 20);
  // The terrain depth buffer itself: nothing nearer than the first step; a wall in front covers the rows above the horizon.
  Camera low = cam;
  low.y = -1500.0f;
  gVoxel.render(a, gCache, low);
  CHECK(gVoxel.nearerRow(120, 0) == VoxelView::kH);
  CHECK(gVoxel.nearerRow(120, gVoxel.stepCount() - 1) == 0);
}

static void topdown() {
  Hills hills;
  gCache.attach(&hills);
  gCache.fill(0, 0);
  static TopDownView v1, v2;
  v1.reset(); v2.reset();
  v1.renderNow(gCache, 0, 0, 0.0f, 40.0f);
  CHECK(v1.ready() && v1.renders() == 1);
  v2.renderNow(gCache, 0, 0, 0.0f, 40.0f);
  bool same = true;
  for (int x = 0; x < TopDownView::kW && same; x += 3)
    for (int y = 0; y < TopDownView::kH; y += 5) if (v1.shownPixel(x, y) != v2.shownPixel(x, y)) { same = false; break; }
  CHECK(same);
  int black = 0;
  for (int x = 0; x < TopDownView::kW; x++) for (int y = 0; y < TopDownView::kH; y++) black += !v1.shownPixel(x, y);
  CHECK(black > 3000 && black < TopDownView::kW * TopDownView::kH - 3000);
  // Progressive: a small budget needs many calls and the shown picture stays valid meanwhile.
  static TopDownView p;
  p.reset();
  p.renderNow(gCache, 0, 0, 0.0f, 40.0f);
  int calls = 0;
  while (calls < 100000) {
    p.update(gCache, 700.0f, 0.0f, 0.0f, 40.0f, 3000);          // 17.5 px away: stale
    calls++;
    CHECK(p.ready());
    if (!p.rendering() && p.renders() == 2) break;
  }
  CHECK(calls > 10 && calls < 100);
  CHECK(p.renders() == 2);
  // Blit: the glider stays at its anchor on the screen, whatever the offset between picture and glider.
  static uint8_t fb[50 * 240];
  memset(fb, 0xFF, sizeof fb);
  Bitmap1 bm{fb, 400, 240, 50, 1};
  p.blit(bm, 700.0f + 130.0f, 0.0f + 90.0f);               // the glider moved on since the picture was made
  float sx, sy;
  p.worldToScreen(830.0f, 90.0f, &sx, &sy);
  NEAR(sx, TopDownView::kGliderX, 0.51);
  NEAR(sy, TopDownView::kTopY + TopDownView::kGliderY, 0.51);
  // The shifted copy equals the picture displaced by the offset (spot checks).
  int bad = 0, checked = 0;
  float gx, gy;
  p.worldToScreen(700.0f, 0.0f, &gx, &gy);                 // where the picture's anchor point ended up
  const int ox = (int)lroundf(gx) - TopDownView::kGliderX, oy = (int)lroundf(gy) - (TopDownView::kTopY + TopDownView::kGliderY);
  for (int x = 20; x < 220; x += 7)
    for (int y = 30; y < 340; y += 11) {
      const int sxp = x + ox, syp = y + oy + TopDownView::kTopY;
      if (sxp < 0 || sxp >= 240 || syp < TopDownView::kTopY || syp >= 400) continue;
      checked++;
      bad += bm.getPixel(sxp, syp) != p.shownPixel(x, y);
    }
  CHECK(checked > 300 && bad == 0);
  // No blank strip at the edges while the glider is up to kMargin pixels from where the picture was made: every pixel of the map
  // area comes from the picture (the margin is rendered), the first and last rows and columns included.
  static TopDownView e;
  e.reset();
  e.renderNow(gCache, 0, 0, 0.0f, 40.0f);
  for (int dxp = -TopDownView::kMargin; dxp <= TopDownView::kMargin; dxp += 4)
    for (int dyp = -TopDownView::kMargin; dyp <= TopDownView::kMargin; dyp += 4) {
      memset(fb, 0xFF, sizeof fb);
      e.blit(bm, 40.0f * dxp, 40.0f * dyp);                  // north up: east is right, north is up
      const int ox = e.offsetX(), oy = e.offsetY();
      CHECK(abs(ox) <= TopDownView::kMargin && abs(oy) <= TopDownView::kMargin);
      int diff = 0, n = 0;
      for (int x = 0; x < 240; x++)
        for (int y = 0; y < TopDownView::kH; y += (x == 0 || x == 239) ? 1 : 9) {
          n++;
          diff += bm.getPixel(x, y + TopDownView::kTopY) != e.shownPixel(x - ox, y - oy);
        }
      CHECK(n > 1000 && diff == 0);
    }
}

// ---- game mode of the host
struct GameApp : IApp {
  uint32_t interval = 20;
  int steps = 0, draws = 0, ticks = 0;
  uint32_t lastSim = 0, dtSeen = 0;
  const char* name() const override { return "game"; }
  void begin(uint32_t) override { steps = draws = ticks = 0; }
  void end() override {}
  void tick(uint32_t) override { ticks++; }
  uint32_t frameIntervalMs() const override { return interval; }
  void step(uint32_t simMs, uint32_t dt) override { steps++; lastSim = simMs; dtSeen = dt; }
  void draw(Bitmap1&, Adafruit_GFX&, uint32_t) override { draws++; }
};
struct BusyPanel : MockPanel {
  bool busyNow = false;
  bool ready() override { return !busyNow; }
};
struct PlainApp : IApp {                                   // change-driven, like the vario and Tetris
  int draws = 0;
  const char* name() const override { return "plain"; }
  void begin(uint32_t) override {}
  void end() override {}
  void tick(uint32_t) override {}
  bool wantsFrame(uint32_t) override { return true; }
  void draw(Bitmap1&, Adafruit_GFX&, uint32_t) override { draws++; }
};

static void game_mode() {
  // N steps per second whatever the host's own tick period.
  for (uint32_t period : {1u, 2u, 5u, 10u, 13u}) {
    BusyPanel p;
    MockInput in;
    AppHost h(p, in);
    GameApp g;
    h.add(g);
    h.start(0, 1000);
    for (uint32_t t = 1000; t <= 11000; t += period) h.tick(t);
    CHECK(g.steps >= 498 && g.steps <= 501);               // 10 s at 50 Hz
    CHECK(g.dtSeen == 20);
    CHECK(h.stats().steps == (uint32_t)g.steps);
    CHECK(h.gameMode() && h.loopDelayMs() == 1);
    CHECK(h.stats().dropped == 0);
    CHECK(h.stats().fps >= 49 && h.stats().fps <= 51);
    if (period <= 10) CHECK(g.draws >= 498);               // one frame per interval
  }
  // Physics time is independent of the draw time: a draw that takes 15 ms (the host is ticked late) loses no steps.
  {
    BusyPanel p;
    MockInput in;
    AppHost h(p, in);
    GameApp g;
    h.add(g);
    h.start(0, 0);
    uint32_t t = 0;
    while (t < 10000) { h.tick(t); t += 20 + 15; }          // every tick arrives 15 ms late
    CHECK(g.lastSim <= 10020 && g.lastSim >= 9900);         // the sim clock kept pace: about 10 s of steps ran
    CHECK(g.steps > 480 && g.steps <= 501);                 // catch-up: 50 per second really ran
  }
  // A panel that is busy delays the frame, never loses sim time.
  {
    BusyPanel p;
    MockInput in;
    AppHost h(p, in);
    GameApp g;
    h.add(g);
    h.start(0, 0);
    for (uint32_t t = 0; t <= 10000; t++) { p.busyNow = (t % 25) < 12; h.tick(t); }
    CHECK(g.steps >= 480 && g.steps <= 501);
    CHECK(g.draws <= g.steps && g.draws > 300);
  }
  // A host that falls far behind (a 500 ms stall) catches up at most kMaxCatchUp steps and counts the rest as dropped.
  {
    BusyPanel p;
    MockInput in;
    AppHost h(p, in);
    GameApp g;
    h.add(g);
    h.start(0, 0);
    for (uint32_t t = 0; t < 1000; t += 10) h.tick(t);
    const int before = g.steps;
    h.tick(1500);
    CHECK(g.steps - before == AppHost::kMaxCatchUp);
    CHECK(h.stats().dropped > 0);
  }
  // The status-line counters: a late tick runs extra steps (catchUp), a busy panel is counted, GameProbe formats them.
  {
    BusyPanel p;
    MockInput in;
    AppHost h(p, in);
    GameApp g;
    h.add(g);
    static uint32_t fakeUs = 0;
    GameProbe probe([]() -> uint32_t { return fakeUs; });
    h.setProbe(&probe);
    h.start(0, 0);
    probe.enable(true, 0);
    h.tick(0);
    p.busyNow = true;
    h.tick(20);                                            // due while the panel is still sending: stepped and drawn at once, present waits
    CHECK(h.stats().panelBusy == 1 && h.stats().frames == 2);
    p.busyNow = false;
    h.tick(60);                                            // 2 slots late (40 and 60): 2 steps, one of them catch-up
    CHECK(h.stats().catchUp == 1 && h.stats().dropped == 0);
    char line[400];
    probe.noteTick(1000);
    probe.report(line, sizeof line, 1000, "t", h.stats(), "x=1");
    CHECK(strstr(line, "gm app=t ") && strstr(line, "catchup=1 dropped=0 busy=1") && strstr(line, "x=1"));
    probe.report(line, sizeof line, 2000, "t", h.stats(), "");
    CHECK(strstr(line, "catchup=0 dropped=0 busy=0"));      // per-second deltas
  }
  // An app that does not ask for game mode runs exactly as before: no steps, change-driven frames, 10 ms loop.
  {
    BusyPanel p;
    MockInput in;
    AppHost h(p, in);
    PlainApp pa;
    h.add(pa);
    h.start(0, 0);
    for (uint32_t t = 0; t < 1000; t += 10) h.tick(t);
    CHECK(!h.gameMode() && h.loopDelayMs() == 10);
    CHECK(pa.draws == 100 && h.stats().frames == 0 && h.stats().steps == 0);
    CHECK(h.nextWakeMs(0) == 0xFFFFFFFFu);
  }
}

// ---- the app
struct StaticTilt : ITilt {
  bool on = true;
  float r = 0, p = 0;
  bool read(float* ro, float* pi) override { if (!on) return false; *ro = r; *pi = p; return true; }
};

// Time acceleration: x3 = three 20 ms substeps per frame, equal to x1 for three times as long; stable at x5.
static void time_scale() {
  Hills hills;
  static SimApp a, b, c;
  SimApp* all[3] = {&a, &b, &c};
  for (SimApp* s : all) { s->setSource(&hills); s->begin(0); }
  CHECK(a.timeScale() == 3);                                // default
  a.cycleTimeScale(); CHECK(a.timeScale() == 5);
  a.cycleTimeScale(); CHECK(a.timeScale() == 1);
  a.cycleTimeScale(); CHECK(a.timeScale() == 3);
  a.setTimeScale(2); CHECK(a.timeScale() == 1);
  // Same seed, same start, no keys (no ramps): x3 for 20 s vs x1 for 60 s is the same flight.
  for (SimApp* s : all) s->startAt(0, 0, 400, 90, 270, 5, 11, 300);
  a.setTimeScale(3); b.setTimeScale(1);
  uint32_t sm = 0, sm2 = 0;
  simshots::run(a, 1000, sm);
  simshots::run(b, 3000, sm2);
  NEAR(a.world().time() - 300.0f, 60.0f, 0.5);             // 20 real seconds = 60 sim seconds
  NEAR(a.world().time(), b.world().time(), 1e-3);
  NEAR(a.world().glider().x, b.world().glider().x, 0.01);
  NEAR(a.world().glider().y, b.world().glider().y, 0.01);
  NEAR(a.world().glider().z, b.world().glider().z, 0.01);
  NEAR(a.world().distanceFlown(), b.world().distanceFlown(), 0.05);
  // HUD: flight time counts from the start of the flight on the sim clock (x3 shows sim time); distance is the straight line.
  NEAR(a.world().flightTime(), 60.0f, 0.5);
  NEAR(a.world().flightTime(), b.world().flightTime(), 1e-3);
  NEAR(a.world().distanceFromStart(), sqrtf(a.world().glider().x * a.world().glider().x + a.world().glider().y * a.world().glider().y), 0.01);
  CHECK(a.world().distanceFromStart() <= a.world().distanceFlown() + 0.01f);
  CHECK(a.world().trailCount() == b.world().trailCount() && a.world().trailCount() >= 58);
  // A held key: the ramp is real time, the wing physics sim time; the flights stay close.
  a.startAt(0, 0, 900, 0, 270, 0, 11, 300);
  b.startAt(0, 0, 900, 0, 270, 0, 11, 300);
  a.onKey(KEY_RIGHT, true, 0); b.onKey(KEY_RIGHT, true, 0);
  sm = sm2 = 0;
  simshots::run(a, 500, sm);
  simshots::run(b, 1500, sm2);
  NEAR(a.world().glider().bank, b.world().glider().bank, 0.02);
  NEAR(a.world().time(), b.world().time(), 1e-3);
  CHECK(fabsf(wrapPi(a.world().glider().heading - b.world().glider().heading)) < 0.5f);
  CHECK(a.mode() == SimApp::Mode::FLYING);
  // x5, tight turn at full bank with the bar: finite and bounded over 5 sim minutes.
  c.startAt(0, 0, 1500, 0, 270, 8, 3, 300);
  c.setTimeScale(5);
  c.onKey(KEY_RIGHT, true, 0); c.onKey(KEY_UP, true, 0);
  uint32_t sm3 = 0;
  for (int i = 0; i < 6; i++) {
    simshots::run(c, 250, sm3);
    const Glider& g = c.world().glider();
    CHECK(isfinite(g.x) && isfinite(g.y) && isfinite(g.z) && isfinite(g.heading));
    CHECK(fabsf(g.bank) <= Glider::kMaxBank + 1e-4f && g.sinkRate < 8.0f && g.load < 1.3f);
    CHECK(g.groundSpeed() < 30.0f);
  }
  NEAR(c.world().time() - 300.0f, 6 * 250 * 0.02f * 5, 0.5);
  // Landing at x5 is still detected once and the sim time stops.
  Slope rise;
  c.setSource(&rise);
  c.onKey(KEY_RIGHT, false, 0); c.onKey(KEY_UP, false, 0);
  c.startAt(0, -400, 30, 0, 270, 0, 3, 300);
  uint32_t sm4 = 0;
  simshots::run(c, 1500, sm4);
  CHECK(c.mode() == SimApp::Mode::ENDED && c.world().status() == Status::LANDED);
  const float tEnd = c.world().time();
  simshots::run(c, 50, sm4);
  CHECK(c.world().time() == tEnd);
  // Pause menu row: Speed cycles.
  c.setTimeScale(3);
  c.startAt(0, 0, 900, 0, 270, 0, 3, 300);
  c.onKey(KEY_B, true, 0);
  for (int i = 0; i < 7; i++) c.onKey(KEY_DOWN, true, 0);
  c.onKey(KEY_ACTION, true, 0);
  CHECK(c.timeScale() == 5);
  c.onKey(KEY_B, true, 0);
}

// The device's tilt source: roll / pitch from gravity in the board frame through the mounting; sensors on only while enabled.
static Vec3 V3(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static void accel_tilt() {
  puts("sim: accelerometer tilt");
  MockCompass c;
  AccelTilt at;
  at.attach(&c, nullptr);
  float r = 0, p = 0;
  CHECK(!at.read(&r, &p));                                   // not enabled yet: no sample
  at.enable(true);
  CHECK(c.enabled);
  CHECK(AccelTilt::angles(V3(0, 9.81f, 0), &r, &p) && fabsf(r) < 0.01f && fabsf(p) < 0.01f);   // upright, facing the pilot
  CHECK(AccelTilt::angles(V3(-9.81f * 0.5f, 9.81f * 0.866f, 0), &r, &p)); NEAR(r, 30.0, 0.05);   // right side down 30
  CHECK(AccelTilt::angles(V3(0, 9.81f * 0.866f, 9.81f * 0.5f), &r, &p)); NEAR(p, 30.0, 0.05);    // top away 30
  CHECK(!AccelTilt::angles(V3(0, 0.5f, 0), &r, &p));       // free fall: no direction
  c.pitch = 90; c.roll = 0; c.psi = 0;                       // the mock's upright box
  for (int i = 0; i < 40; i++) at.read(&r, &p);
  CHECK(fabsf(r) < 1.0f && fabsf(p) < 1.0f);
  at.enable(false);
  CHECK(!c.enabled && !at.read(&r, &p));
}

static void app_logic() {
  Slope rise;                                              // a mountain side to fly in front of
  SimApp* app = new SimApp;
  StaticTilt tilt;
  app->setSource(&rise);
  app->setTilt(&tilt);
  uint32_t sm = 0;
  app->begin(0);
  // The Day screen comes first: Site, Thermals, Wind from, Wind speed, Start.
  CHECK(app->mode() == SimApp::Mode::DAY && app->dayRow() == 0);
  CHECK(app->level() == 1 && app->windDir() == 0 && app->windSpeedIdx() == 0);
  app->onKey(KEY_DOWN, true, 0);
  app->onKey(KEY_RIGHT, true, 0);                          // thermals: medium -> strong
  CHECK(app->level() == 2);
  app->onKey(KEY_DOWN, true, 0);
  app->onKey(KEY_RIGHT, true, 0);                          // wind from: usual -> N
  app->onKey(KEY_DOWN, true, 0);
  app->onKey(KEY_ACTION, true, 0);                         // wind speed: usual -> 0 km/h (A also steps forward)
  CHECK(app->windDir() == 1 && app->windSpeedIdx() == 1);
  app->onKey(KEY_LEFT, true, 0);
  CHECK(app->windSpeedIdx() == 0);
  app->onKey(KEY_DOWN, true, 0);
  CHECK(app->dayRow() == 4);
  app->onKey(KEY_ACTION, true, 0);                         // Start
  CHECK(app->mode() == SimApp::Mode::FLYING && app->view() == SimApp::View::TOP);
  CHECK(app->world().level() == 2 && app->world().air().level() == 2);
  NEAR(app->world().windFromDeg(), 0.0, 0.01);
  app->setDay(1, 0, 0);
  app->startFlight();
  CHECK(app->world().level() == 1);
  CHECK(app->frameIntervalMs() == 20);
  // D-pad: holding right ramps the bank command up, releasing brings it back.
  app->onKey(KEY_RIGHT, true, 0);
  simshots::run(*app, 10, sm);
  CHECK(app->controls().bank > 0.5f);
  simshots::run(*app, 30, sm);
  NEAR(app->controls().bank, 1.0f, 1e-6);
  app->onKey(KEY_RIGHT, false, 0);
  simshots::run(*app, 60, sm);
  NEAR(app->controls().bank, 0.0f, 1e-6);
  app->onKey(KEY_UP, true, 0);
  simshots::run(*app, 80, sm);
  NEAR(app->controls().bar, 1.0f, 1e-6);
  app->onKey(KEY_UP, false, 0);
  // A toggles the view; B opens the menu and the world stops.
  app->onKey(KEY_ACTION, true, 0);
  CHECK(app->view() == SimApp::View::CHASE);
  app->onKey(KEY_ACTION, true, 0);
  CHECK(app->view() == SimApp::View::TOP);
  app->onKey(KEY_B, true, 0);
  CHECK(app->mode() == SimApp::Mode::PAUSED);
  const float t0 = app->world().time();
  simshots::run(*app, 50, sm);
  CHECK(app->world().time() == t0);
  // Menu: down twice = Assist, A toggles it; B resumes.
  app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0);
  app->onKey(KEY_ACTION, true, 0);
  CHECK(app->assist());
  app->onKey(KEY_B, true, 0);
  CHECK(app->mode() == SimApp::Mode::FLYING);
  // Tilt: the menu's Controls row switches to tilt and opens the calibration page, A sets the zero.
  app->onKey(KEY_B, true, 0);
  app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0);
  app->onKey(KEY_ACTION, true, 0);                         // row 3: the thermals view cycles columns -> bubbles -> both
  CHECK(app->thermalStyle() == SkyFx::Style::BOTH);          // from the default, bubbles
  app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0);
  app->onKey(KEY_ACTION, true, 0);
  CHECK(app->control() == SimApp::Control::TILT && app->mode() == SimApp::Mode::CALIB);
  tilt.r = 12.0f; tilt.p = -8.0f;                          // how the pilot holds the box
  app->onKey(KEY_ACTION, true, 0);
  CHECK(app->mode() == SimApp::Mode::FLYING);
  simshots::run(*app, 5, sm);
  NEAR(app->controls().bank, 0.0f, 1e-6);                  // held at the zero: nothing
  NEAR(app->controls().bar, 0.0f, 1e-6);
  tilt.r = 12.0f + 38.0f;
  simshots::run(*app, 5, sm);
  NEAR(app->controls().bank, 1.0f, 1e-5);                  // 38 degrees past the zero: full bank right
  tilt.r = 12.0f; tilt.p = -8.0f + 20.0f;
  simshots::run(*app, 5, sm);
  NEAR(app->controls().bar, 1.0f, 1e-5);
  tilt.p = -8.0f - 20.0f;
  simshots::run(*app, 5, sm);
  NEAR(app->controls().brake, 1.0f, 1e-5);
  // B on the calibration page falls back to the buttons.
  app->setControl(SimApp::Control::TILT);
  CHECK(app->mode() == SimApp::Mode::CALIB);
  app->onKey(KEY_B, true, 0);
  CHECK(app->control() == SimApp::Control::BUTTONS && app->mode() == SimApp::Mode::FLYING);
  // No tilt source: stays on the buttons.
  app->setTilt(nullptr);
  app->setControl(SimApp::Control::TILT);
  CHECK(app->control() == SimApp::Control::BUTTONS);
  delete app;
}

// ---- playability of the air: reachable lift from every takeoff, a scripted pilot that climbs, the feel of a thermal's edge
static bool openPack(const char* path, std::vector<uint8_t>& bytes, terrain::MemReader& rd, terrain::Terrain& t) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  bytes.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  const bool ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  fclose(f);
  if (!ok) return false;
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  return t.open(&rd) == terrain::Terrain::OK;
}

// The playability numbers of NOTES.md ("Flight simulator: playable air"): the same analysis as `build.sh simreach`, a few seeds.
//   Per run: at least 2 working thermals reachable by a straight trim glide from the launch point even when the air sinks 0.5 m/s more
//   than it does (>= 30 m above the ground on arrival), the nearest one within 1.5 km; the scripted pilot (assist on: nearest reachable
//   thermal, circle, soar the slope when nothing is in reach) gains >= 500 m in 10 sim minutes in at least 80 % of the runs.
static void playable_site(terrain::Terrain& t, int site, int seed0, int seeds, const float* times, int nTimes, int* runs, int* gainOk) {
  static World w;
  TerrainSource src(&t, kTakeoffs[site].lat, kTakeoffs[site].lon);
  for (int ti = 0; ti < nTimes; ti++)
    for (int seed = seed0; seed < seed0 + seeds; seed++) {
      w.begin(&src, (uint32_t)seed, kTakeoffs[site], -1.0f, times[ti]);
      CHECK(w.air().houseCount() >= 2);
      const simreach::Reach r = simreach::analyse(w);
      CHECK(r.withMargin >= 2);
      CHECK(r.nearest > 0 && r.nearest < 1500.0f);
      const simreach::Flight f = simreach::fly(w, 600.0f);
      (*runs)++;
      if (f.gain >= 500.0f) (*gainOk)++;
    }
}

static void playability() {
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static terrain::FullTerrain t;
  static std::vector<uint8_t> bytes2;
  static terrain::MemReader rd2;
  static terrain::FullTerrain t2;
  const float times[] = {180.0f, 1800.0f};
  const bool haveSopot = openPack("fixtures/sopot.pack", bytes, rd, t);
  // The real pack (maps/bulgaria.pack): every takeoff of the table.
  const char* big = "../../maps/bulgaria.pack";
  const bool haveBig = openPack(big, bytes2, rd2, t2);
  // One process per run: Sopot 2 start times x 8 seeds, then 8 takeoffs x 4 seeds of the big pack.
  const int nSopot = haveSopot ? 16 : 0, nBig = haveBig ? kTakeoffCount * 4 : 0;
  ForkResult res[16 + kTakeoffCount * 4];
  forkEach(nSopot + nBig, [&](int i, ForkResult& r) {
    int runs = 0, ok = 0;
    if (i < nSopot) playable_site(t, 0, i % 8 + 1, 1, times + i / 8, 1, &runs, &ok);
    else { const float tt[] = {1800.0f}; playable_site(t2, (i - nSopot) / 4, (i - nSopot) % 4 + 1, 1, tt, 1, &runs, &ok); }
    r.a = runs; r.b = ok;
  }, res);
  int runs = 0, ok = 0;
  for (int i = 0; i < nSopot; i++) { runs += (int)res[i].a; ok += (int)res[i].b; }
  CHECK(runs == 16);
  printf("  playability, Sopot fixture: scripted pilot gained >= 500 m in 10 min in %d of %d runs\n", ok, runs);
  CHECK(ok * 100 >= runs * 80);
  if (haveBig) {
    int r2 = 0, ok2 = 0;
    for (int i = nSopot; i < nSopot + nBig; i++) { r2 += (int)res[i].a; ok2 += (int)res[i].b; }
    printf("  playability, Bulgaria pack, all %d takeoffs: gained >= 500 m in %d of %d runs\n", kTakeoffCount, ok2, r2);
    CHECK(ok2 * 100 >= r2 * 80);
  }
}

// Air facts the playability rests on, on synthetic ground.
static void air_playable() {
  Hills hills;
  gCache.attach(&hills);
  gCache.fill(0, 0);
  // House thermals: three, within about 1.5 km of the launch, and at any moment of the day at least one is mature and strong.
  for (uint32_t seed = 1; seed <= 6; seed++) {
    Air a(seed);
    a.setField(&gCache);
    a.setWind(200.0f, 5.0f, 1400.0f);
    a.setHome(0, 0);
    CHECK(a.houseCount() == Air::kHouses);
    for (int i = 0; i < a.houseCount(); i++) {
      float x, y;
      CHECK(a.house(i, &x, &y));
      CHECK(hypotf(x, y) < 2000.0f);
    }
    int weak = 0;
    for (float tm = 0; tm < 7200.0f; tm += 60.0f) {
      ThermalInfo th[Air::kMaxInfo];
      const int n = a.nearby(0, 0, 1500.0f, tm, 2500.0f, th, Air::kMaxInfo);
      float best = 0;
      for (int i = 0; i < n; i++) best = fmaxf(best, th[i].strength);
      if (best < 2.0f) weak++;
    }
    CHECK(weak == 0);
  }
  // Strength and widening: cores 2 - 4.6 m/s, wider higher up.
  Air a(5);
  a.setField(&gCache);
  a.setWind(200.0f, 5.0f, 1400.0f);
  ThermalInfo lo[Air::kMaxInfo], hi[Air::kMaxInfo];
  const int nl = a.nearby(0, 0, 1400.0f, 900.0f, 3000.0f, lo, Air::kMaxInfo);
  const int nh = a.nearby(0, 0, 2200.0f, 900.0f, 4000.0f, hi, Air::kMaxInfo);
  float rl = 0, rh = 0;
  int cl = 0, ch = 0;
  for (int i = 0; i < nl; i++) { rl += lo[i].radius; cl++; }
  for (int i = 0; i < nh; i++) { rh += hi[i].radius; ch++; }
  CHECK(cl > 3 && ch > 3 && rh / ch > rl / cl);
  // The ridge lift cancels the general sink on the slope; away from the slope the sink is there.
  Ridge ridge;
  gCache.attach(&ridge);
  gCache.fill(0, 0);
  Air b(3);
  b.setField(&gCache);
  b.setWind(270.0f, 6.0f, 1000.0f);
  AirSample on, off;
  b.sample(-450.0f, 0, 1000.0f + 300.0f * expf(-(450.0f * 450.0f) / (600.0f * 600.0f)) + 60.0f, 0, &on);
  b.sample(-6000.0f, 0, 1100.0f, 0, &off);
  CHECK(on.base > off.base * 0.6f && on.ridge > 1.2f);
}

// Wing tip lift: at the edge of a thermal the tip on the stronger side rises, the glider banks away from it (a few degrees),
// a pilot feels it and turns toward the high wing. Zero in still air.
static void tip_lift() {
  Hills hills;
  gCache.attach(&hills);
  gCache.fill(0, 0);
  Air a(5);
  a.setField(&gCache);
  a.setWind(200.0f, 0.0f, 1400.0f);
  ThermalInfo th[Air::kMaxInfo];
  const int n = a.nearby(0, 0, 1800.0f, 900.0f, 3000.0f, th, Air::kMaxInfo);
  int best = 0;
  for (int i = 1; i < n; i++) if (th[i].strength > th[best].strength) best = i;
  CHECK(n > 0 && th[best].strength > 2.0f);
  const float cx = th[best].x, cy = th[best].y, R = th[best].radius;
  AirSample west, east, centre;
  a.sample(cx - 0.8f * R, cy, 1800.0f, 900.0f, &west);       // west of the centre: the lift grows toward the east
  a.sample(cx + 0.8f * R, cy, 1800.0f, 900.0f, &east);
  a.sample(cx, cy, 1800.0f, 900.0f, &centre);
  CHECK(west.gx > 0.003f && east.gx < -0.003f);
  CHECK(fabsf(centre.gx) < fabsf(west.gx) * 0.2f);
  // The gradient is the derivative of the thermal part.
  AirSample w2;
  a.sample(cx - 0.8f * R + 1.0f, cy, 1800.0f, 900.0f, &w2);
  NEAR(w2.thermal - west.thermal, west.gx, 0.15 * fabsf(west.gx) + 1e-4);
  // In the World: heading north, the east wing is the stronger one west of the centre: the glider rolls left, by a few degrees.
  static World w;
  w.beginAt(&hills, 5, 0, 0, 600.0f, 0.0f, 200.0f, 0.0f, 900.0f);
  const int nw = w.air().nearby(0, 0, 1800.0f, 900.0f, 3000.0f, th, Air::kMaxInfo);
  int bw = 0;
  for (int i = 1; i < nw; i++) if (th[i].strength > th[bw].strength) bw = i;
  CHECK(nw > 0);
  Glider& g = w.gliderMut();
  g.x = th[bw].x - 0.8f * th[bw].radius; g.y = th[bw].y; g.z = 1800.0f;
  for (int i = 0; i < 25; i++) w.step(Controls(), 0.02f);
  CHECK(w.rollKick() < -0.01f && w.rollKick() > -0.15f);
  CHECK(w.glider().bank < -0.005f);
}

// The wind blows onto the slope of the takeoff (the way it faces) when the slope is clear.
static void wind_onto_slope() {
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static terrain::FullTerrain t;
  if (!openPack("fixtures/sopot.pack", bytes, rd, t)) return;
  TerrainSource src(&t, kTakeoffs[0].lat, kTakeoffs[0].lon);
  static World w;
  w.begin(&src, 3, kTakeoffs[0]);
  float h0 = 0, a = 0, b = 0, c = 0, d = 0;
  CHECK(src.at(0, 0, &h0) && src.at(600, 0, &a) && src.at(-600, 0, &b) && src.at(0, 600, &c) && src.at(0, -600, &d));
  const float gx = (a - b) / 1200.0f, gy = (c - d) / 1200.0f;
  float wx, wy;
  w.windAt(h0 + World::kLaunchAgl, &wx, &wy);
  if (sqrtf(gx * gx + gy * gy) > 0.12f) CHECK(wx * gx + wy * gy > 0);          // blowing uphill
  NEAR(hypotf(wx, wy), 5.0f, 0.5);
  NEAR(w.agl(), World::kLaunchAgl, 0.5);
}

// The Day settings: how thermic, wind direction, wind speed.
static void day_settings() {
  Hills hills;
  gCache.attach(&hills);
  gCache.fill(0, 0);
  // Levels scale strength, density, cloud base and sink together, in order.
  float meanS[Air::kLevels], cloud[Air::kLevels], sink[Air::kLevels];
  int count[Air::kLevels];
  for (int l = 0; l < Air::kLevels; l++) {
    float sum = 0, cb = 0, sk = 0;
    int n = 0;
    for (uint32_t seed = 1; seed <= 6; seed++) {
      Air a;
      a.setLevel(l);
      a.setSeed(seed);
      a.setField(&gCache);
      a.setLevel(l);
      a.setSeed(seed);
      a.setWind(200.0f, 5.0f, 1400.0f);
      cb += a.cloudBase(); sk += a.baseSink();
      for (float tm = 300; tm < 3000; tm += 300) {
        ThermalInfo th[Air::kMaxInfo];
        const int k = a.nearby(0, 0, 1500.0f, tm, 3000.0f, th, Air::kMaxInfo);
        for (int i = 0; i < k; i++) if (th[i].strength > 1.0f) { sum += th[i].strength; n++; }
      }
    }
    meanS[l] = sum / (n ? n : 1); count[l] = n; cloud[l] = cb / 6; sink[l] = sk / 6;
  }
  for (int l = 1; l < Air::kLevels; l++) {
    CHECK(meanS[l] > meanS[l - 1] && cloud[l] > cloud[l - 1] && sink[l] > sink[l - 1] && count[l] > count[l - 1]);
  }
  printf("  levels weak..epic: mean core %.1f %.1f %.1f %.1f m/s, cloud base %.0f %.0f %.0f %.0f m, sink %.2f %.2f %.2f %.2f, usable thermals %d %d %d %d\n",
         (double)meanS[0], (double)meanS[1], (double)meanS[2], (double)meanS[3], (double)cloud[0], (double)cloud[1], (double)cloud[2],
         (double)cloud[3], (double)sink[0], (double)sink[1], (double)sink[2], (double)sink[3], count[0], count[1], count[2], count[3]);
  Air m;
  m.setSeed(1);
  CHECK(m.level() == 1);                                   // medium is the default (the tuned good day)
  // Wind direction flips the ridge lift sides; no wind, no ridge lift.
  Ridge ridge;
  gCache.attach(&ridge);
  gCache.fill(0, 0);
  Air a(3);
  a.setField(&gCache);
  const float zw = 1000.0f + 300.0f * expf(-(500.0f * 500.0f) / (600.0f * 600.0f)) + 60.0f;
  AirSample w1, e1, w2, e2, calm;
  a.setWind(270.0f, 5.0f, 1000.0f);                        // from the west
  a.sample(-500.0f, 0, zw, 0, &w1); a.sample(500.0f, 0, zw, 0, &e1);
  a.setWind(90.0f, 5.0f, 1000.0f);                         // from the east
  a.sample(-500.0f, 0, zw, 0, &w2); a.sample(500.0f, 0, zw, 0, &e2);
  CHECK(w1.ridge > 1.0f && e1.ridge < -0.5f);
  CHECK(e2.ridge > 1.0f && w2.ridge < -0.5f);
  a.setWind(90.0f, 0.0f, 1000.0f);
  a.sample(-500.0f, 0, zw, 0, &calm);
  NEAR(calm.ridge, 0, 1e-4);
  // A wind change eases in: no jump, the target after about 15 s.
  Flat flat;
  static World w;
  w.beginAt(&flat, 4, 0, 0, 900.0f, 270.0f, 270.0f, 3.0f, 0.0f);
  w.setWindTarget(90.0f, 8.3f);
  float wx0, wy0, wx1, wy1;
  w.windAt(1900.0f, &wx0, &wy0);
  w.step(Controls(), 0.02f);
  w.windAt(1900.0f, &wx1, &wy1);
  CHECK(hypotf(wx1 - wx0, wy1 - wy0) < 0.02f);
  for (int i = 0; i < 50 * 5; i++) w.step(Controls(), 0.02f);
  w.windAt(1900.0f, &wx1, &wy1);
  CHECK(fabsf(wx1) < 1.0f);                                   // 3 m/s of the 11.3 m/s change in 5 s: passing through the calm
  for (int i = 0; i < 50 * 16; i++) w.step(Controls(), 0.02f);
  w.windAt(1900.0f, &wx1, &wy1);
  NEAR(wx1, -8.3f, 0.1);                                      // from the east now: blowing west at 8.3 m/s
  // Strong days climb faster than weak ones (the scripted pilot, Sopot fixture, same seeds).
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static terrain::FullTerrain t;
  if (!openPack("fixtures/sopot.pack", bytes, rd, t)) return;
  TerrainSource src(&t, kTakeoffs[0].lat, kTakeoffs[0].lon);
  static World fw;
  float gain[Air::kLevels] = {0, 0, 0, 0};
  ForkResult lv[Air::kLevels * 6];
  forkEach(Air::kLevels * 6, [&](int i, ForkResult& r) {      // one process per level and seed
    fw.setLevel(i / 6);
    fw.begin(&src, (uint32_t)(i % 6 + 1), kTakeoffs[0], -1.0f, 1800.0f);
    r.a = simreach::fly(fw, 600.0f).gain / 6.0f;
  }, lv);
  for (int i = 0; i < Air::kLevels * 6; i++) gain[i / 6] += (float)lv[i].a;
  printf("  scripted pilot, mean gain in 10 min, weak..epic: %.0f %.0f %.0f %.0f m\n", (double)gain[0], (double)gain[1], (double)gain[2], (double)gain[3]);
  CHECK(gain[2] > gain[0] + 150.0f && gain[3] > gain[1] && gain[1] > gain[0]);
  // Medium at the usual wind of every takeoff is the playability test above; weak days are not guaranteed.
}

// The sim inside the box: started from the home grid, steady 50 fps through the real host, Home through the pause menu.
static void in_the_box() {
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static TerrainKit kit;
  FILE* f = fopen("fixtures/sopot.pack", "rb");
  CHECK(f != nullptr);
  if (!f) return;
  fseek(f, 0, SEEK_END);
  bytes.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  CHECK(fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
  fclose(f);
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  CHECK(kit.open(&rd));
  static const HeightCache* hcRefPtr = nullptr;
  // The box gives the sim a Terrain with a cache of two tiles and no relief scratch. It must read the same heights as the vario's
  // full one, and fill the whole height cache without decoding a tile again and again.
  {
    static terrain::TerrainWith<SimBox::kTiles, false> small;
    CHECK(small.open(&rd) == terrain::Terrain::OK);
    static HeightCache hcSmall, hcFull;
    hcRefPtr = &hcSmall;
    TerrainSource ssmall(&small, kTakeoffs[0].lat, kTakeoffs[0].lon), sfull(&kit.terrain, kTakeoffs[0].lat, kTakeoffs[0].lon);
    hcSmall.attach(&ssmall);
    hcFull.attach(&sfull);
    kit.terrain.flush();
    kit.terrain.resetStats();
    small.resetStats();
    CHECK(hcSmall.fill(0, 0) == HeightCache::N * HeightCache::N && hcFull.fill(0, 0) == HeightCache::N * HeightCache::N);
    CHECK(memcmp(hcSmall.heights(), hcFull.heights(), sizeof(int16_t) * HeightCache::N * HeightCache::N) == 0);
    CHECK(memcmp(hcSmall.shades(), hcFull.shades(), HeightCache::N * HeightCache::N) == 0);
    printf("  height cache fill from the pack: %u tile decodes with %d tiles, %u with %d\n", (unsigned)small.tileDecodes(), SimBox::kTiles,
           (unsigned)kit.terrain.tileDecodes(), terrain::kCacheTiles);
    // A row or column of the window crosses 5 tiles, and two tiles cannot hold them: each line decodes them again (about 5 per
    // line, 0.7 s at the start of a flight on the device). Flying then costs the same per line, at most one decode per frame.
    CHECK(small.tileDecodes() < 1000 && small.crcErrors() == 0 && small.readErrors() == 0);
    float g[3 * 240];
    terrain::Window win;
    win.lat = kTakeoffs[0].lat; win.lon = kTakeoffs[0].lon; win.w = 240; win.h = 3;
    CHECK(small.hillshade(win, terrain::ReliefStyle(), 0, 3, (uint8_t*)g) == 0);   // no relief scratch: nothing drawn, nothing overrun
  }
  // The sim in the box opens the pack by itself and starts a flight from it; with no pack it stays on its message.
  {
    sim::SimBox with(&rd), without(nullptr);
    CHECK(with.terrain().isOpen() && !without.terrain().isOpen());
    with.begin(0);
    without.begin(0);
    CHECK(with.mode() == SimApp::Mode::DAY && without.mode() == SimApp::Mode::NOMAP);
    with.showDayScreen(false);
    with.startFlight();
    // Over the pack a start shows the 48 x 48 cells around the glider at once and loads the rest of the window in the first frames.
    CHECK(with.mode() == SimApp::Mode::FLYING && !with.world().cache().complete() && with.world().agl() > 100.0f);
    CHECK(with.world().cache().x1() - with.world().cache().x0() == 2 * World::kNearCells && with.world().cache().warming());
    {
      uint32_t t = 0;
      int frames = 0;
      while (frames < 600 && (with.world().cache().warming() || !with.world().cache().complete())) { with.step(t, 20); t += 20; frames++; }
      printf("  progressive start: window complete after %d frames, %u tile decodes\n", frames, (unsigned)with.terrain().tileDecodes());
      CHECK(with.world().cache().complete() && frames < 400);
      // Same heights and shades as a window filled in one go (where the two windows overlap).
      const HeightCache& hc = with.world().cache();
      int bad = 0, n = 0;
      for (int iy = hc.y0() + 1; iy < hc.y1() - 1; iy++)
        for (int ix = hc.x0() + 1; ix < hc.x1() - 1; ix++)
          if (ix >= -63 && ix < 63 && iy >= -63 && iy < 63) { n++; if (hc.cellH(ix, iy) != hcRefPtr->cellH(ix, iy) || hc.cellShade(ix, iy) != hcRefPtr->cellShade(ix, iy)) bad++; }
      CHECK(n > 10000 && bad == 0);
    }
    without.startFlight();
    CHECK(without.mode() == SimApp::Mode::NOMAP);
    // Flying at x5 for 5 minutes of sim time over the pack: a frame loads cells only up to the first decode (a line of the window
    // takes about 5 frames), so no frame decodes more than the tiles of one cell: 4 at most, where four tiles meet.
    with.setTimeScale(5);
    with.startAt(0, 0, 1500.0f, 0.0f, 180.0f, 0.0f, 1, 0.0f);     // heading north: a new row of the window every 200 m
    with.onKey(KEY_UP, true, 0);                                  // speed bar
    const uint32_t decodes0 = with.terrain().tileDecodes();
    uint32_t worst = 0, simMs = 0;
    int incomplete = 0;
    for (int f = 0; f < 3000 && with.mode() == SimApp::Mode::FLYING; f++) {
      const uint32_t d0 = with.terrain().tileDecodes();
      with.step(simMs, 20);
      simMs += 20;
      const uint32_t d = with.terrain().tileDecodes() - d0;
      if (d > worst) worst = d;
      if (f > 100 && !with.world().cache().complete()) incomplete++;
    }
    printf("  flown over the pack at x5: %.1f s, %.0f m, worst frame %u tile decodes, window incomplete in %d frames, %u decodes flying\n",
           (double)with.world().time(), (double)with.world().distanceFlown(), (unsigned)worst, incomplete, (unsigned)(with.terrain().tileDecodes() - decodes0));
    CHECK(worst <= 4 && with.world().distanceFlown() > 2000.0f);
  }
  static Box b(1);
  b.setTerrain(&kit);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) b.tick(now); };
  auto tap = [&](Key k) { b.r.input.setKey(k, true, now); run(30); b.r.input.setKey(k, false, now); run(30); };
  b.boot(0);
  CHECK(b.registry.count() == 5 && strcmp(b.registry.at(2).title, "Sim") == 0);
  b.host.start(Box::APP_LAUNCHER, now);
  run(300);
  simshots::dumpPanel(b.r.panel, "out", "sim_launcher");
  b.host.start(Box::APP_SIM, now);
  run(100);
  CHECK(b.flightSim->mode() == SimApp::Mode::DAY);
  for (int i = 0; i < 4; i++) tap(KEY_DOWN);               // to Start
  tap(KEY_ACTION);
  run(2600);
  CHECK(b.host.active() == &b.flightSim && b.flightSim->mode() == SimApp::Mode::FLYING);
  CHECK(b.host.gameMode());
  CHECK(b.host.stats().fps >= 49 && b.host.stats().fps <= 51);
  CHECK(b.host.stats().steps >= 145 && b.host.stats().steps <= 151);   // 3 s at 50 Hz
  b.r.input.setKey(KEY_RIGHT, true, now);
  run(1000);
  CHECK(b.flightSim->controls().bank > 0.9f);
  b.r.input.setKey(KEY_RIGHT, false, now);
  // B pauses; A + B held 1.5 s opens the system sheet: the flight stands still, A on Home leaves for the home screen.
  tap(KEY_B);
  CHECK(b.flightSim->mode() == SimApp::Mode::PAUSED);
  b.r.input.setKey(KEY_ACTION, true, now); b.r.input.setKey(KEY_B, true, now);
  run(1700);
  CHECK(b.host.sheetOpen() && b.host.active() == &b.flightSim);
  b.r.input.setKey(KEY_ACTION, false, now); b.r.input.setKey(KEY_B, false, now);
  const uint32_t steps0 = b.host.stats().steps;
  run(500);
  CHECK(b.host.stats().steps == steps0);                   // frozen while the sheet is open
  CHECK(b.host.sheet().cursorRow() == SystemSheet::R_HOME);
  tap(KEY_ACTION);
  run(100);
  CHECK(b.host.active() == &b.launcher && !b.host.gameMode());
  // The way in again: the Day screen has no Home row and B does nothing there; out through the 3 s hold and the sheet.
  b.host.start(Box::APP_SIM, now);
  run(500);
  tap(KEY_B);
  CHECK(b.host.active() == &b.flightSim);
  b.r.input.press(now + 10, 3300);
  run(3600);
  CHECK(b.host.sheetOpen() && b.host.sheet().cursorRow() == SystemSheet::R_HOME);
  b.r.input.press(now + 10, 900);                          // long press: Home
  run(1200);
  CHECK(b.host.active() == &b.launcher);

  // ---- one button: the whole flow on the switch alone (short / long), tilt from the (mock) accelerometer angles.
  {
    static Box ob(1);
    ob.oneButton = true;
    ob.setTerrain(&kit);
    ob.boot(0);
    uint32_t t = 0;
    auto go = [&](uint32_t ms) { for (uint32_t e = t + ms; t < e; t += 10) ob.tick(t); };
    auto shortp = [&]() { ob.r.input.press(t + 10, 120); go(420); };
    auto longp = [&]() { ob.r.input.press(t + 10, 900); go(1200); };
    ob.tilt.on = true;
    ob.tilt.roll = 5; ob.tilt.pitch = 40;                    // how the pilot happens to hold it
    ob.host.start(Box::APP_SIM, t);
    go(300);
    SimBox& sb = *ob.flightSim.get();
    CHECK(sb.oneButton() && sb.control() == SimApp::Control::TILT && sb.mode() == SimApp::Mode::DAY);
    simshots::dumpPanel(ob.r.panel, "out", "sim_ob_day");
    shortp(); shortp();                                      // Site, Thermals, -> Wind from
    CHECK(sb.dayRow() == 2);
    longp();                                                 // act on the row: wind from usual -> N
    CHECK(sb.windDir() == 1);
    shortp(); shortp();                                      // Wind, Start (no Home row any more)
    CHECK(sb.dayRow() == 4);
    simshots::dumpPanel(ob.r.panel, "out", "sim_ob_day_home");
    shortp();                                                // wraps to Site
    CHECK(sb.dayRow() == 0);
    for (int i = 0; i < 4; i++) shortp();
    CHECK(sb.dayRow() == 4);
    longp();                                                 // Start: the calibration page comes before the first flight
    go(2500);
    CHECK(sb.mode() == SimApp::Mode::CALIB && ob.host.active() == &ob.flightSim);
    simshots::dumpPanel(ob.r.panel, "out", "sim_ob_calib");
    shortp();
    CHECK(sb.mode() == SimApp::Mode::CALIB);                 // a short press does nothing here
    longp();                                                 // zero here, fly
    CHECK(sb.mode() == SimApp::Mode::FLYING);
    go(500);
    NEAR(sb.controls().bank, 0.0f, 1e-6);                    // held at the zero: wings level
    NEAR(sb.controls().bar, 0.0f, 1e-6);
    ob.tilt.roll = 5 + 38;                                   // right side down
    go(500);
    NEAR(sb.controls().bank, 1.0f, 1e-5);
    go(3000);
    CHECK(sb.world().glider().bank > 0.3f);                  // and the glider banks right
    ob.tilt.roll = 5; ob.tilt.pitch = 40 + 20;
    go(300);
    NEAR(sb.controls().bar, 1.0f, 1e-5);
    ob.tilt.pitch = 40;
    longp();                                                 // long in flight: 3D view
    CHECK(sb.view() == SimApp::View::CHASE && sb.mode() == SimApp::Mode::FLYING);
    go(200);
    simshots::dumpPanel(ob.r.panel, "out", "sim_ob_3d");
    shortp();                                                // short in flight: the pause menu
    CHECK(sb.mode() == SimApp::Mode::PAUSED);
    const float t0 = sb.world().time();
    go(300);
    CHECK(sb.world().time() == t0);
    simshots::dumpPanel(ob.r.panel, "out", "sim_ob_pause");
    for (int i = 0; i < 5; i++) shortp();                    // Controls
    longp();                                                 // stays on tilt, re-opens the calibration page
    CHECK(sb.control() == SimApp::Control::TILT && sb.mode() == SimApp::Mode::CALIB);
    longp();
    CHECK(sb.mode() == SimApp::Mode::FLYING);
    shortp();                                                // pause again: row 0, Resume
    CHECK(sb.mode() == SimApp::Mode::PAUSED);
    longp();
    CHECK(sb.mode() == SimApp::Mode::FLYING);
    shortp();                                                // pause, then the 3 s hold: the sheet, Home first
    CHECK(sb.mode() == SimApp::Mode::PAUSED);
    ob.r.input.press(t + 10, 3300);
    go(3600);
    CHECK(ob.host.sheetOpen() && ob.host.sheet().cursorRow() == SystemSheet::R_HOME);
    longp();
    go(100);
    CHECK(ob.host.active() == &ob.launcher);
  }
}

static void run() {
  puts("sim");
  polar();
  turn_and_sink();
  stall();
  tilt_map();
  height_cache();
  air_model();
  ridge_lift();
  landing();
  voxel();
  sky_fx();
  topdown();
  game_mode();
  time_scale();
  app_logic();
  accel_tilt();
  air_playable();
  tip_lift();
  wind_onto_slope();
  playability();
  day_settings();
  in_the_box();
}

}  // namespace sim_tests
