#pragma once
// Flight sim scenarios shared by the screen renders (sim_demo, emu_main) and the tests: a SimApp over a map pack
// positioned on a ridge at Sopot with the wind blowing onto it, and inside a thermal. Needs a Terrain with a pack
// that covers Sopot (the Bulgaria pack of the study, or emu/fixtures/sopot.pack).
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "apps/games/sim/sim_app.h"
#include "mocks.h"
#include "platform/gfx/bitmap1.h"

namespace simshots {
using namespace sim;

static uint8_t gFb[50 * 240];
struct NullGfx : Adafruit_GFX {
  NullGfx() : Adafruit_GFX(240, 400) {}
  void drawPixel(int16_t, int16_t, uint16_t) override {}
};

static void dump(SimApp& app, const char* dir, const char* name, uint32_t frame = 0) {
  memset(gFb, 0xFF, sizeof gFb);
  Bitmap1 bm{gFb, 400, 240, 50, 1};
  static NullGfx gfx;
  app.draw(bm, gfx, frame);
  std::string path = std::string(dir) + "/" + name + ".pgm";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P5\n240 400\n255\n");
  for (int y = 0; y < 400; y++)
    for (int x = 0; x < 240; x++) fputc(bm.getPixel(x, y) ? 255 : 0, f);
  fclose(f);
}

static void dumpPanel(MockPanel& p, const char* dir, const char* name) {
  p.dump((std::string(dir) + "/" + name + ".pgm").c_str());
}

static void run(SimApp& app, int steps, uint32_t& simMs) {
  for (int i = 0; i < steps; i++) { simMs += 20; app.step(simMs, 20); }
}

// A ridge flight: glider in front of the Sopot slope (south of the takeoff), wind from the south at 8 m/s.
static void ridge(SimApp& app, terrain::Terrain& t) {
  app.setTerrain(&t);
  app.setSite(0);
  app.setSeed(7);
  app.begin(0);
  // Sopot origin is the takeoff; stand 1 km south of the takeoff on the steep part of the slope, 90 m above the ground, flying along it.
  app.startAt(0.0f, -1000.0f, 90.0f, 20.0f, 190.0f, 8.0f, 7, 600.0f);
}

// Put the glider in the strongest thermal within 6 km at any time of the next half hour (the day is seeded, so it is the same
// thermal on every run) and let it circle a while.
static bool thermal(SimApp& app, terrain::Terrain& t, uint32_t& simMs) {
  (void)simMs;
  app.setTerrain(&t);
  app.setSite(0);
  app.begin(0);
  World& w = app.world();
  ThermalInfo th[Air::kMaxInfo];
  float bestX = 0, bestY = 0, bestS = 0, bestT = 600;
  const float z = 1700.0f;
  for (float tm = 300.0f; tm <= 2100.0f; tm += 100.0f) {
    const int n = w.air().nearby(0, 0, z, tm, 6000.0f, th, Air::kMaxInfo);
    for (int i = 0; i < n; i++)
      if (th[i].strength > bestS) { bestS = th[i].strength; bestX = th[i].x; bestY = th[i].y; bestT = tm; }
  }
  if (bestS <= 0) return false;
  float g = 0;
  w.cache().height(bestX, bestY, &g);
  app.startAt(bestX, bestY, z - g, 0.0f, 190.0f, 8.0f, 7, bestT);
  return true;
}

// A scene for the thermal views: the strongest thermal 1.2 - 3.5 km from the Sopot launch whose cumulus is fully grown at some
// time of the first 40 minutes (the day is seeded, so it is the same one on every run).
static bool pickCloudThermal(SimApp& app, ColumnInfo* best, float* bestT) {
  ColumnInfo c[48];
  float bs = 0;
  for (float tm = 400.0f; tm <= 2400.0f; tm += 50.0f) {
    const int n = app.world().air().columns(0, 0, tm, 5000.0f, c, 48);
    for (int i = 0; i < n; i++) {
      const float d = sqrtf(c[i].cx * c[i].cx + c[i].cy * c[i].cy);
      if (c[i].cloud > 0.97f && d > 1200.0f && d < 3500.0f && c[i].strength > bs) { bs = c[i].strength; *best = c[i]; *bestT = tm; }
    }
  }
  return bs > 0;
}

// inside = false: 1.6 km from the thermal's trigger, flying toward it at 1500 m; true: circling in its core.
static bool thermalScene(SimApp& app, terrain::Terrain& t, bool inside, uint32_t& sm, float headingDeg = 90.0f) {
  app.setTerrain(&t);
  app.showDayScreen(false);
  app.setSite(0);
  app.setSeed(7);
  app.begin(0);
  app.startAt(0.0f, 0.0f, 150.0f, 0.0f, 190.0f, 5.0f, 7, 1200.0f);
  ColumnInfo c;
  float tm = 0;
  if (!pickCloudThermal(app, &c, &tm)) return false;
  app.startAt(0.0f, 0.0f, 150.0f, 0.0f, 190.0f, 5.0f, 7, tm);
  Glider& g = app.world().gliderMut();
  const float h = headingDeg * kDeg2Rad;
  float cx, cy;
  app.world().air().columnPoint(c, 1500.0f, tm, &cx, &cy);
  g.z = 1500.0f;
  g.heading = h;
  printf("thermal scene: t %.0f s, thermal %u S %.1f strength %.1f cloud %.2f, trigger (%.0f, %.0f, %.0f), column top (%.0f, %.0f), cloud (%.0f, %.0f) width %.0f, column at 1500 m (%.0f, %.0f)\n",
         (double)tm, c.id, (double)c.S, (double)c.strength, (double)c.cloud, (double)c.x0, (double)c.y0, (double)c.z0, (double)c.cx, (double)c.cy,
         (double)c.cloudX, (double)c.cloudY, (double)c.width, (double)cx, (double)cy);
  if (inside) { g.x = cx; g.y = cy; }
  else {                                                  // 2.3 km from the middle of the column (its trigger and its top), looking across the lean
    const float mx = 0.5f * (c.x0 + c.cx), my = 0.5f * (c.y0 + c.cy);
    g.x = mx - 2300.0f * sinf(h);
    g.y = my - 2300.0f * cosf(h);
    float gh = 0;
    app.world().cache().height(g.x, g.y, &gh);
    if (g.z < gh + 250.0f) g.z = gh + 250.0f;
  }
  app.setView(SimApp::View::CHASE);
  sm = 0;
  if (inside) {
    app.onKey(KEY_RIGHT, true, 0);
    run(app, 60, sm);
  } else {
    run(app, 2, sm);
  }
  return true;
}

// The three thermal styles, approaching and inside, plus the clouds alone (assist off): sim_thermal_<scene>_<style>.pgm.
static void thermalStyleShots(const char* dir, terrain::Terrain& t) {
  const char* sn[3] = {"columns", "bubbles", "both"};
  for (int inside = 0; inside < 2; inside++) {
    for (int st = -1; st < 3; st++) {
      SimApp app;
      uint32_t sm = 0;
      if (!thermalScene(app, t, inside != 0, sm)) return;
      app.setAssist(st >= 0);
      if (st >= 0) app.setThermalStyle((SkyFx::Style)st);
      char name[64];
      snprintf(name, sizeof name, "sim_thermal_%s_%s", inside ? "inside" : "approach", st < 0 ? "clouds" : sn[st]);
      dump(app, dir, name, 3);
      const SkyFx::Stats& s = app.sky().stats();
      printf("%-34s thermals %d, clouds %d (%d cols), shafts %d (%d cols), bubbles %d, level %d, est. %.0f + %.0f cycles = %.2f ms\n", name, s.found, s.clouds,
             s.cloudCols, s.shafts, s.shaftCols, s.bubbles, s.level, (double)s.cloudCycles, (double)s.assistCycles,
             (double)(s.cloudCycles + s.assistCycles) / 64000.0);
    }
  }
}

static void shots(const char* dir, terrain::Terrain& t) {
  SimApp app;
  uint32_t sm = 0;
  ridge(app, t);
  app.setView(SimApp::View::TOP);
  run(app, 200, sm);
  dump(app, dir, "sim_top_ridge", 3);
  app.setView(SimApp::View::CHASE);
  run(app, 5, sm);
  dump(app, dir, "sim_chase_ridge", 3);
  // The chase camera in a right turn: the glider swings to the left of the screen, the horizon tilts a fraction of the bank.
  app.onKey(KEY_RIGHT, true, 0);
  run(app, 120, sm);
  dump(app, dir, "sim_chase_turn", 3);
  app.onKey(KEY_RIGHT, false, 0);
  app.onKey(KEY_UP, true, 0);                                // speed bar: the canopy shows more of its top
  run(app, 45, sm);
  dump(app, dir, "sim_chase_bar", 3);

  SimApp app2;
  sm = 0;
  if (thermal(app2, t, sm)) {
    app2.setAssist(true);
    app2.setView(SimApp::View::TOP);
    app2.onKey(KEY_RIGHT, true, 0);                          // circle right: bank held
    run(app2, 750, sm);
    dump(app2, dir, "sim_top_thermal", 3);
    app2.setView(SimApp::View::CHASE);
    run(app2, 10, sm);
    dump(app2, dir, "sim_chase_thermal", 3);
  }

  // At launch with the assist on: the thermals a pilot can reach.
  SimApp app3;
  app3.setTerrain(&t);
  app3.showDayScreen(false);
  app3.setSite(0);
  app3.setSeed(7);
  app3.begin(0);
  app3.setAssist(true);
  app3.setView(SimApp::View::TOP);
  uint32_t sm3 = 0;
  run(app3, 30, sm3);
  dump(app3, dir, "sim_top_launch_assist", 3);
  app3.setView(SimApp::View::CHASE);
  run(app3, 5, sm3);
  dump(app3, dir, "sim_chase_launch", 3);
  app3.onKey(KEY_B, true, 0);                                // the pause menu with the Day rows
  dump(app3, dir, "sim_menu", 3);

  SimApp app4;                                               // the Day screen
  app4.setTerrain(&t);
  app4.setSite(5);
  app4.begin(0);
  app4.onKey(KEY_DOWN, true, 0);
  app4.onKey(KEY_RIGHT, true, 0);
  dump(app4, dir, "sim_day", 3);
  thermalStyleShots(dir, t);
}

// The standard renders from the Sopot fixture pack (run by emu_main shots).
static void fixtureShots(const char* dir) {
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static terrain::Terrain t;
  FILE* f = fopen("fixtures/sopot.pack", "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END);
  bytes.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  const bool ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  fclose(f);
  if (!ok) return;
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  if (t.open(&rd) != terrain::Terrain::OK) return;
  shots(dir, t);
}

}  // namespace simshots
