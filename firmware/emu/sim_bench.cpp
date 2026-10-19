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
#include "platform/game_probe.h"
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

static terrain::FullTerrain gTerrain;

// ---- the device model: the same AppHost + GameProbe the box runs, on a virtual clock that advances by the measured PC time x the
// calibration factor (so every part is in M4 ms) plus the loop's 1 ms delay. The panel takes a full-frame SPI/DMA transfer of
// kSendMs (measured on the device: 12.49 ms) and is not ready() until it is over, exactly the gate AppHost::gameFrame applies.
namespace devmodel {
double gFactor = 1, gVirtualUs = 0;
Clock::time_point gLast;
uint32_t clk() {
  const auto n = Clock::now();
  gVirtualUs += std::chrono::duration<double, std::micro>(n - gLast).count() * gFactor;
  gLast = n;
  return (uint32_t)gVirtualUs;
}
struct Panel : MockPanel {
  static constexpr double kSendMs = 12.5;
  double busyUntilUs = 0;
  bool ready() override { clk(); return gVirtualUs >= busyUntilUs; }
  void present() override { MockPanel::present(); clk(); gVirtualUs += 1500; /* line diff + pack + DMA start, device: refresh 1.4-1.8 ms */ busyUntilUs = gVirtualUs + kSendMs * 1000.0; }
};
}  // namespace devmodel

static void deviceModel(terrain::Terrain* terr, double factorMap, double factor3d) {
  using namespace devmodel;
  static Panel panel;
  static MockInput input;
  static sim::SimApp a;
  a.setTerrain(terr);
  a.setSite(0);
  a.showDayScreen(false);
  a.setClock(clk);
  static AppHost host(panel, input);
  static GameProbe probe(clk);
  host.add(a);
  host.setProbe(&probe);
  printf("\ndevice model (virtual M4 clock = PC time x %.0f in the map view, x %.0f in 3D, +1 ms loop delay, panel busy %.1f ms per frame after present):\n", factorMap, factor3d, Panel::kSendMs);
  const sim::SimApp::View views[2] = {sim::SimApp::View::TOP, sim::SimApp::View::CHASE};
  for (int scale : {3, 1}) {
    for (sim::SimApp::View v : views) {
      gFactor = v == sim::SimApp::View::TOP ? factorMap : factor3d;
      gLast = Clock::now();
      uint32_t nowMs = (uint32_t)(gVirtualUs / 1000);
      host.start(0, nowMs);
      a.setTimeScale(scale);
      { char flush[400]; a.gameInfo(flush, sizeof flush); }   // forget what the previous run counted
      a.setView(v);
      input.setKey(KEY_RIGHT, true, nowMs);             // a steady turn: the map picture keeps re-rendering, the cache keeps loading
      probe.enable(true, nowMs);
      const double end = gVirtualUs + 4e6;
      while (gVirtualUs < end) {
        gVirtualUs += 1000;                              // the loop's delay(1)
        gLast = Clock::now();
        nowMs = (uint32_t)(gVirtualUs / 1000);
        const uint32_t t0 = clk();
        host.tick(nowMs);
        probe.noteTick(clk() - t0);
        if (probe.due(nowMs)) {
          char info[400], line[800];
          a.gameInfo(info, sizeof info);
          probe.report(line, sizeof line, nowMs, a.name(), host.stats(), info);
          puts(line);
        }
      }
      input.setKey(KEY_RIGHT, false, nowMs);
      probe.enable(false, nowMs);
    }
  }
}

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

  {
    sim::Glider gl = app.world().glider();
    gl.bank = 0.3f; gl.airspeed = 45.0f / 3.6f;
    sim::ChaseCam cc;
    cc.reset(gl);
    const sim::Camera cam = cc.camera(gl, app.world().cache());
    const double sp = timeIt([&](int) { cc.drawGlider(bm, gl, app.world().cache(), cam); }, 2000, &w3);
    rows.push_back({"chase camera: glider sprite + shadow alone", sp, w3});
    const double cm = timeIt([&](int) { cc.update(gl, NAN, 0.06f); (void)cc.camera(gl, app.world().cache()); }, 2000, &w3);
    rows.push_back({"chase camera: smoothing + camera", cm, w3});
  }
  // Clouds and thermal views: the approach scene of the contact sheet (4 clouds, 3 thermals in front), drawn after the terrain.
  struct SkyRow { const char* name; double pc, worst; sim::SkyFx::Stats st; };
  std::vector<SkyRow> skyRows;
  double colsT = 0;
  {
    static sim::SimApp a2;
    uint32_t m2 = 0;
    if (simshots::thermalScene(a2, gTerrain, false, m2)) {
      const sim::Glider& g2 = a2.world().glider();
      const sim::Camera cam2 = a2.chase().camera(g2, a2.world().cache());
      a2.voxel().render(bm, a2.world().cache(), cam2);
      static sim::ColumnInfo ci[32];
      colsT = timeIt([&](int) { a2.world().air().columns(cam2.x, cam2.y, a2.world().time(), 9000.0f, ci, 32); }, 400, &w3);
      struct V { const char* name; bool assist; sim::SkyFx::Style st; } vs2[] = {
          {"sky: clouds only (assist off)", false, sim::SkyFx::Style::BUBBLES}, {"sky: clouds + assist, columns", true, sim::SkyFx::Style::COLUMNS},
          {"sky: clouds + assist, bubbles", true, sim::SkyFx::Style::BUBBLES}, {"sky: clouds + assist, both", true, sim::SkyFx::Style::BOTH}};
      for (const V& v : vs2) {
        const double t = timeIt([&](int) { a2.sky().draw(bm, a2.voxel(), a2.world().air(), g2, cam2, a2.world().time(), v.assist, v.st); }, 300, &w3);
        skyRows.push_back({v.name, t, w3, a2.sky().stats()});
        rows.push_back({v.name, t, w3});
      }
      rows.push_back({"Air::columns (clouds and thermals in view)", colsT, w3});
    }
  }
  app.setView(sim::SimApp::View::TOP);
  const double top =timeIt([&](int i) { memset(fb, 0xFF, sizeof fb); app.draw(bm, gfx, 20u * i); }, 300, &w3);
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
  // Clouds and thermals: the counted work (sky3d.cpp's own cycle accounting) next to the PC time scaled by the calibration factor.
  // The 3D view in total = renderer (cycle count above) + depth buffer + HUD, present and sprite (about 4 ms, NOTES.md) + the rows here.
  printf("\nclouds and thermal views, approach scene (cycle accounting inside SkyFx; Air::columns %.0f us on the PC = %.2f ms scaled)\n", colsT * 1000.0, colsT * factor);
  for (const SkyRow& r : skyRows) {
    const double cyc = (r.st.cloudCycles + r.st.assistCycles) / 64000.0;
    const double base = 16.0 + 0.3;                                  // frame so far (NOTES.md: 15 - 16 ms) + the renderer's depth buffer
    printf("  %-34s clouds %d (%d cols), shafts %d (%d cols), bubbles %d, level %d: counted %.2f ms, PC x factor %.2f ms; 3D frame about %.1f (counted) .. %.1f ms (scaled)\n",
           r.name, r.st.clouds, r.st.cloudCols, r.st.shafts, r.st.shaftCols, r.st.bubbles, r.st.level, cyc, r.pc * factor,
           base + colsT * factor + cyc, base + colsT * factor + r.pc * factor);
  }
  // ---- recalibration (2026-10-19): the vario pages are a poor ruler for this code (a single factor of 59 gave a 3D frame of 3 ms where the
  // device needs ~17: the sim's inner loops are float / byte-fill code, the vario pages are glyph blits). So the sim is calibrated against
  // ITSELF: the frame draw times measured on the device with the SIM_BENCH build (the "gm" lines, averages over 20 s), one factor per view.
  // Update kDev* when the device numbers change (NOTES.md "Sim leftovers pass").
  constexpr double kDevMapDraw = 11.0, kDev3dDraw = 17.0;
  const double fMap = kDevMapDraw / top, f3d = kDev3dDraw / fpv;
  printf("\nrecalibrated against the device: map draw %.1f ms / %.3f ms PC = x%.0f, 3D draw %.1f ms / %.3f ms PC = x%.0f (the vario-page factor was %.0f)\n",
         kDevMapDraw, top, fMap, kDev3dDraw, fpv, f3d, factor);
  printf("PC model of the parts at those factors (3D): voxel %.1f ms, sprite+shadow %.1f ms; map: picture re-render %.1f ms\n", vox * f3d, rows[2].pc * f3d, rn * fMap);
  const char* ov = getenv("SIMBENCH_FACTOR");   // one factor for both views, if set
  deviceModel(&gTerrain, ov ? atof(ov) : fMap, ov ? atof(ov) : f3d);
  return 0;
}
