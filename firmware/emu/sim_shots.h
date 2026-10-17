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
  run(app, 100, sm);
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
