#pragma once
// Tests for the flight simulator (apps/games/sim) and the platform's game mode (fixed-timestep host). Included by
// tests.cpp after CHECK / NEAR; run() is called from main.
#include <math.h>
#include <string.h>
#include <vector>
#include "apps/games/sim/air.h"
#include "apps/games/sim/glider.h"
#include "apps/games/sim/height_cache.h"
#include "apps/games/sim/sim_app.h"
#include "apps/games/sim/terrain_source.h"
#include "apps/games/sim/tilt.h"
#include "apps/games/sim/topdown.h"
#include "apps/games/sim/voxel.h"
#include "apps/games/sim/world.h"
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

static HeightCache gCache;          // 48 kB: static, the tests are not the loop task but keep the habit
static VoxelView gVoxel;

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
  // Thermals follow the rules: 1-5 m/s, 50-150 m cores, a life of 10-20 minutes (checked through the strength envelope).
  for (int i = 0; i < na; i++) {
    CHECK(ta[i].strength >= 0 && ta[i].strength <= 5.0f);
    CHECK(ta[i].radius >= 50.0f && ta[i].radius <= 150.0f * 1.7f);
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
  CHECK(a.cloudBase() >= 2000.0f && a.cloudBase() <= 2600.0f);
  // Between thermals the air sinks 0.5 - 1.0 m/s.
  CHECK(a.baseSink() >= 0.5f && a.baseSink() <= 1.0f);
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
  for (int i = 0; i < 6; i++) c.onKey(KEY_DOWN, true, 0);
  c.onKey(KEY_ACTION, true, 0);
  CHECK(c.timeScale() == 5);
  c.onKey(KEY_B, true, 0);
}

static void app_logic() {
  Slope rise;                                              // a mountain side to fly in front of
  SimApp* app = new SimApp;
  StaticTilt tilt;
  app->setSource(&rise);
  app->setTilt(&tilt);
  uint32_t sm = 0;
  app->begin(0);
  CHECK(app->mode() == SimApp::Mode::FLYING && app->view() == SimApp::View::TOP);
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
  CHECK(app->view() == SimApp::View::FPV);
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
  app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0); app->onKey(KEY_DOWN, true, 0);
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
  static Box b(1, true);
  b.setTerrain(&kit);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) b.tick(now); };
  auto tap = [&](Key k) { b.r.input.setKey(k, true, now); run(30); b.r.input.setKey(k, false, now); run(30); };
  b.boot(0);
  CHECK(b.registry.count() == 5 && strcmp(b.registry.at(4).title, "Flight sim") == 0);
  b.host.start(Box::APP_LAUNCHER, now);
  run(300);
  simshots::dumpPanel(b.r.panel, "out", "sim_launcher");
  b.host.start(Box::APP_SIM, now);
  run(3000);
  CHECK(b.host.active() == b.flightSim.get() && b.flightSim->mode() == SimApp::Mode::FLYING);
  CHECK(b.host.gameMode());
  CHECK(b.host.stats().fps >= 49 && b.host.stats().fps <= 51);
  CHECK(b.host.stats().steps >= 145 && b.host.stats().steps <= 151);   // 3 s at 50 Hz
  b.r.input.setKey(KEY_RIGHT, true, now);
  run(1000);
  CHECK(b.flightSim->controls().bank > 0.9f);
  b.r.input.setKey(KEY_RIGHT, false, now);
  // B pauses, UP wraps to the last row (Home), A leaves for the home screen.
  tap(KEY_B);
  CHECK(b.flightSim->mode() == SimApp::Mode::PAUSED);
  tap(KEY_UP);
  tap(KEY_ACTION);
  run(100);
  CHECK(b.host.active() == &b.launcher && !b.host.gameMode());
  // The way in again, and out through the 3 s hold of the switch like every other app.
  b.host.start(Box::APP_SIM, now);
  run(500);
  b.r.input.press(now + 10, 3300);
  run(3600);
  CHECK(b.host.active() == &b.launcher);
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
  topdown();
  game_mode();
  time_scale();
  app_logic();
  in_the_box();
}

}  // namespace sim_tests
