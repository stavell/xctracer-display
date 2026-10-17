// Timing of the flight sim's frames on the PC, calibrated against vario pages whose time on the device is known.
//   build.sh simbench [PACK]
// Method: the Cortex-M4F at 64 MHz without data cache is 30-60 times slower than this PC on pixel-pushing code; rather than guess,
// the same binary times two existing routines whose cost on the device was measured (the vario's thermal page 5-6 ms, its
// compass page 7.6 ms, NOTES.md) and scales the sim's PC times by the same ratio. A second estimate counts the work of a frame
// (samples, spans, bytes, pixels) and multiplies by cycle costs of the inner loops (instruction counts of the compiled loops).
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <vector>
#include <Fonts/FreeSansBold12pt7b.h>
#include "apps/games/sim/sim_app.h"
#include "apps/vario/view/screens.h"
#include "mocks.h"
#include "sim_shots.h"

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

template <class F>
static double timeIt(F f, int reps, double* worst = nullptr) {
  double sum = 0, w = 0;
  for (int i = 0; i < reps; i++) {
    const auto t0 = Clock::now();
    f(i);
    const double d = ms(t0, Clock::now());
    sum += d;
    w = std::max(w, d);
  }
  if (worst) *worst = w;
  return sum / reps;
}

static terrain::Terrain gTerrain;

int main(int argc, char** argv) {
  const char* packPath = argc > 1 ? argv[1] : "fixtures/sopot.pack";
  std::vector<uint8_t> bytes;
  FILE* f = fopen(packPath, "rb");
  if (!f) { perror(packPath); return 2; }
  fseek(f, 0, SEEK_END); bytes.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) return 2;
  fclose(f);
  terrain::MemReader rd(bytes.data(), (uint32_t)bytes.size());
  if (gTerrain.open(&rd) != terrain::Terrain::OK) { fprintf(stderr, "pack rejected\n"); return 3; }

  // ---- calibration routines: the vario's pages
  static ViewModel vm;
  vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED;
  vm.s.fix = true; vm.s.alt = 1400; vm.s.vario = 1.5f; vm.s.speedKmh = 30; vm.s.course = 90;
  vm.heading = 90; vm.headingSrc = HDG_GPS;
  vm.wind.valid = true; vm.wind.speedKmh = 12; vm.wind.toDeg = 90;
  vm.localMinutes = 600; vm.localBattPct = 80;
  vm.trailCount = 60;
  for (int i = 0; i < vm.trailCount; i++) { vm.trail[i].x = 40 * sinf(i * 0.3f); vm.trail[i].y = 40 * cosf(i * 0.3f); vm.trail[i].vario = 1.5f; }
  vm.markCount = 1; vm.marks[0].x = 30; vm.marks[0].y = 20; vm.marks[0].avgClimb = 2.1f;
  static MockPanel p;
  p.setRotation(1);
  vm.page = ViewModel::PAGE_THERMAL;
  const int reps = 400;
  double w;
  const double pcThermal = timeIt([&](int) { p.clear(); drawLive(p.gfx(), vm, 0); }, reps, &w);
  vm.page = ViewModel::PAGE_COMPASS;
  const double pcCompass = timeIt([&](int) { p.clear(); drawLive(p.gfx(), vm, 0); }, reps, &w);
  const double devThermal = 5.5, devCompass = 7.6;       // ms on the device (NOTES.md)
  const double fT = devThermal / pcThermal, fC = devCompass / pcCompass;
  const double factor = 0.5 * (fT + fC);
  printf("calibration: thermal page %.3f ms on the PC (device 5-6 ms, factor %.0f), compass page %.3f ms (device 7.6 ms, factor %.0f), used %.0f\n",
         pcThermal, fT, pcCompass, fC, factor);

  // ---- the sim
  static sim::SimApp app;
  app.setTerrain(&gTerrain);
  app.setSite(0);
  app.begin(0);
  app.startAt(0.0f, -1000.0f, 150.0f, 20.0f, 190.0f, 8.0f, 7, 600.0f);
  static uint8_t fb[50 * 240];
  Bitmap1 bm{fb, 400, 240, 50, 1};
  static simshots::NullGfx gfx;
  uint32_t simMs = 0;
  simshots::run(app, 100, simMs);

  // Cache loading: all cells and per-frame refresh when moving.
  {
    static sim::HeightCache hc;
    static sim::TerrainSource src(&gTerrain, sim::kTakeoffs[0].lat, sim::kTakeoffs[0].lon);
    hc.attach(&src);
    const auto t0 = Clock::now();
    const int n = hc.fill(0, 0);
    const double t = ms(t0, Clock::now());
    printf("height cache fill: %d cells in %.1f ms (%.2f us per cell incl. tile decodes)\n", n, t, t * 1000.0 / n);
  }

  struct Row { const char* name; double pc, worst; };
  std::vector<Row> rows;
  app.setView(sim::SimApp::View::CHASE);
  for (int i = 0; i < 20; i++) { memset(fb, 0xFF, sizeof fb); app.draw(bm, gfx, 20 * i); }
  double w3;
  const double fpv = timeIt([&](int i) { memset(fb, 0xFF, sizeof fb); app.draw(bm, gfx, 20u * i); }, 300, &w3);
  rows.push_back({"3D view frame (render + HUD)", fpv, w3});
  const sim::VoxelView::Stats vs = app.voxel().stats();
  const double vox = timeIt([&](int) { sim::Camera c; c.x = 0; c.y = -1000; c.z = 1400; c.heading = 0.35f; c.roll = 0.0f; c.pitch = 0.05f; app.voxel().render(bm, app.world().cache(), c); }, 300, &w3);
  rows.push_back({"3D renderer alone", vox, w3});

  app.setView(sim::SimApp::View::TOP);
  const double top = timeIt([&](int i) { memset(fb, 0xFF, sizeof fb); app.draw(bm, gfx, 20u * i); }, 300, &w3);
  rows.push_back({"top-down frame (blit + trail + glider + HUD)", top, w3});
  // Picture re-render, 4500 px per step and whole.
  static sim::TopDownView tv;
  tv.reset();
  const double rn = timeIt([&](int i) { tv.reset(); tv.renderNow(app.world().cache(), 0, -1000, 0.1f * i, 40.0f); }, 20, &w3);
  rows.push_back({"top-down picture, full render (spread over ~20 frames)", rn, w3});
  const double step = timeIt([&](int) { simMs += 20; app.step(simMs, 20); }, 500, &w3);
  rows.push_back({"sim step (physics + air + cache refresh + top-down budget)", step, w3});
  app.setView(sim::SimApp::View::CHASE);
  const double step3 = timeIt([&](int) { simMs += 20; app.step(simMs, 20); }, 500, &w3);
  rows.push_back({"sim step in the 3D view (physics + air + cache refresh)", step3, w3});
  // Air alone.
  sim::AirSample as;
  const double airT = timeIt([&](int i) { for (int k = 0; k < 50; k++) app.world().air().sample(10.0f * k, -1000.0f + i, 1500, 700, &as); }, 200, &w3) / 50.0;
  rows.push_back({"Air::sample (one call)", airT, w3});

  printf("\nwork of one 3D frame: %d columns, %d steps walked, %d samples (%d bilinear), %d spans, ~%d bytes filled, %d columns ended at the screen top; %d steps per ray at most\n",
         vs.columns, vs.steps, vs.samples, vs.bilinear, vs.spans, vs.bytes, vs.early, app.voxel().stepCount());
  printf("%-62s %9s %9s %14s\n", "routine", "PC ms", "PC worst", "M4 estimate ms");
  for (const Row& r : rows) printf("%-62s %9.3f %9.3f %14.1f\n", r.name, r.pc, r.worst, r.pc * factor);

  // ---- second estimate: count the work, multiply by cycle costs
  // Inner loop of the voxel renderer per bilinear sample: ~85 instructions (16 loads, ~30 float ops, int/float conversions, 6 compares),
  // per nearest sample ~45; a Cortex-M4F executes about 1.3 cycles per instruction from flash with wait states (-O2): 110 / 60 cycles.
  const double cyc = 110.0 * vs.bilinear + 60.0 * (vs.samples - vs.bilinear) + 40.0 * vs.spans + 1.0 * vs.bytes + 15.0 * vs.columns + 150.0 * vs.columns;
  printf("\ncycle-count estimate of the 3D renderer: %.2f M cycles = %.1f ms at 64 MHz (110 cycles per bilinear sample, 60 per nearest sample, 40 per span, 1 per byte)\n",
         cyc / 1e6, cyc / 64000.0);
  return 0;
}
