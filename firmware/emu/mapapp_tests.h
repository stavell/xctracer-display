// The Map app (apps/map) and its platform view (platform/map/map_view) over fixtures/sopot.pack.
#pragma once
#include <memory>
#include <vector>
#include "apps/map/map_app.h"
#include "platform/map/map_view.h"

static bool loadSopot(std::vector<uint8_t>& bytes) {
  FILE* f = fopen("fixtures/sopot.pack", "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  bytes.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  const bool ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  fclose(f);
  return ok;
}

static void renderAll(mapview::MapView& v) {
  for (int i = 0; i < 5000 && v.missing() > 0; i++) v.step(0);
}

// Tiles are cut from one projection and dithered on the ground's grid (north up): a view moved by whole pixels shows the very
// same pixels, shifted, wherever its tiles begin. A turned view still gets all its tiles from the pool.
static void map_view_tiles() {
  puts("map view: tiles join, the picture moves with the ground");
  static std::vector<uint8_t> bytes;
  CHECK(loadSopot(bytes));
  if (bytes.empty()) return;
  static terrain::MemReader rd;
  static terrain::FullTerrain terr;
  static terrain::MapVectors vec;
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  CHECK(terr.open(&rd) == terrain::Terrain::OK);
  vec.open(&rd, terr.info());
  static mapview::MapView a, b;
  static uint8_t fa[50 * 240], fb[50 * 240];
  const Bitmap1 A{fa, 400, 240, 50, 1}, B{fb, 400, 240, 50, 1};
  for (int scale : {25, 100}) {
    a.attach(&terr, &vec);
    b.attach(&terr, &vec);
    a.setArea(240, 368);
    b.setArea(240, 368);
    a.setScale((float)scale);
    b.setScale((float)scale);
    a.centreOn(42.69, 24.75);
    a.snap();
    b.setCentre(a.centreX() + 37, a.centreY() + 23);
    renderAll(a);
    renderAll(b);
    CHECK(a.missing() == 0 && b.missing() == 0);
    memset(fa, 0xFF, sizeof fa);
    memset(fb, 0xFF, sizeof fb);
    a.drawTiles(A, 0, 32);
    b.drawTiles(B, 0, 32);
    long diff = 0, black = 0;
    for (int y = 32 + 23; y < 400; y++)
      for (int x = 37; x < 240; x++) {
        diff += A.getPixel(x, y) != B.getPixel(x - 37, y - 23);
        black += !A.getPixel(x, y);
      }
    CHECK(diff == 0);
    CHECK(black > 1000);                                             // relief and contours, not an empty picture
    printf("  %d m/px: %u + %u tiles, %ld black px, %ld differ\n", scale, a.tilesRendered(), b.tilesRendered(), black, diff);
  }
  a.setHeading(45);
  renderAll(a);
  CHECK(a.missing() == 0);                                           // the pool holds the whole turned screen
  double lat, lon;
  a.centreLatLon(&lat, &lon);
  float gx, gy, sx, sy;
  CHECK(a.toGlobal(lat, lon, &gx, &gy));
  a.toScreen(gx, gy, &sx, &sy);
  NEAR(sx, 120, 0.01); NEAR(sy, 184, 0.01);
  a.toScreen(gx + 10, gy, &sx, &sy);                                 // 10 px east with east 45 degrees left of up: up and right
  NEAR(sx, 120 + 7.071, 0.01); NEAR(sy, 184 - 7.071, 0.01);
}

static std::string mapLine(double lat, double lon, float course, int sec) {
  char body[200];
  snprintf(body, sizeof body, "XCTRC,2026,8,16,10,%d,%d,0,%.6f,%.6f,1500.00,30.00,%.1f,1.00,,,,900.00,85", sec / 60, sec % 60, lat, lon,
           course);
  return line(body);
}

static void map_app_box() {
  puts("map app: follows the tracer, pans, zooms, compass mode, lets go of the tracer and the sensors");
  CHECK(sizeof(mapapp::MapApp) <= sizeof(VarioBox));               // the shared arena does not grow
  static std::vector<uint8_t> bytes;
  CHECK(loadSopot(bytes));
  if (bytes.empty()) return;
  static terrain::MemReader rd;
  static TerrainKit kit;
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  CHECK(kit.open(&rd));
  std::unique_ptr<Box> pb(new Box());
  Box& b = *pb;
  b.setTerrain(&kit);
  bx::BoxRun u(b);
  b.boot(0);
  u.run(300);
  b.host.openApp(Box::APP_MAP);
  u.run(300);
  CHECK(b.host.active() == &b.mapApp && b.mapApp.alive() && b.mapApp->hasPack());
  CHECK(!b.r.tracer.paused);                                         // the map listens to the tracer
  CHECK(!b.r.compass.enabled);                                       // north up: no sensors
  mapapp::MapApp& m = *b.mapApp.get();
  while (m.view().missing() > 0 && u.now < 60000) u.run(100);
  CHECK(m.view().missing() == 0);
  u.run(mapapp::MapApp::kHintMs);                                    // the hint has gone
  const int frames = b.r.panel.presents;
  u.run(3000);
  CHECK(b.r.panel.presents == frames);                               // a still map draws nothing

  b.r.tracer.lines.push_back(mapLine(42.70, 24.76, 90, 0));
  u.run(100);
  double lat, lon;
  m.view().centreLatLon(&lat, &lon);
  CHECK(m.following());
  NEAR(lat, 42.70, 0.001); NEAR(lon, 24.76, 0.002);
  CHECK(b.r.panel.presents > frames);

  const float x0 = m.view().centreX();
  b.r.input.setKey(KEY_RIGHT, true, u.now); u.run(50);
  b.r.input.setKey(KEY_RIGHT, false, u.now); u.run(50);
  CHECK(!m.following());
  NEAR(m.view().centreX() - x0, mapapp::MapApp::kStepPx, 0.01);
  b.r.tracer.lines.push_back(mapLine(42.71, 24.77, 90, 1));
  u.run(100);
  NEAR(m.view().centreX() - x0, mapapp::MapApp::kStepPx, 0.01);   // browsing: the fix does not pull the view back
  b.r.input.setKey(KEY_ACTION, true, u.now); u.run(800);           // hold A: back to the GPS
  b.r.input.setKey(KEY_ACTION, false, u.now); u.run(50);
  CHECK(m.following());
  CHECK(m.scaleIndex() == mapapp::MapApp::kStartScale);             // the hold did not zoom
  m.view().centreLatLon(&lat, &lon);
  NEAR(lat, 42.71, 0.001);

  b.r.input.setKey(KEY_B, true, u.now); u.run(50);
  b.r.input.setKey(KEY_B, false, u.now); u.run(50);
  CHECK(m.scaleIndex() == mapapp::MapApp::kStartScale + 1);         // B: out
  for (int i = 0; i < mapapp::MapApp::kStartScale + 1; i++) u.shortp();   // the switch: in, down to the closest ...
  CHECK(m.scaleIndex() == 0);
  u.shortp();
  CHECK(m.scaleIndex() == mapapp::MapApp::kScales - 1);             // ... then round to the widest
  m.view().centreLatLon(&lat, &lon);
  NEAR(lat, 42.71, 0.002);                                           // zooming keeps the centre (to half a pixel: 200 m at 400 m/px)

  u.longp();
  CHECK(m.compassMode() && b.r.compass.enabled);
  b.r.compass.psi = 90;
  u.run(3000);
  NEAR(m.view().heading(), 90, 2.0);
  u.longp();
  CHECK(!m.compassMode() && !b.r.compass.enabled && m.view().heading() == 0);

  b.r.input.setKey(KEY_ACTION, true, u.now); b.r.input.setKey(KEY_B, true, u.now); u.run(100);   // A + B: the sheet's, not a zoom
  b.r.input.setKey(KEY_ACTION, false, u.now); b.r.input.setKey(KEY_B, false, u.now); u.run(50);
  CHECK(m.scaleIndex() == mapapp::MapApp::kScales - 1);

  u.home();
  CHECK(b.host.active() == &b.launcher && !b.mapApp.alive());
  CHECK(b.r.tracer.paused && !b.r.compass.enabled);

  b.setTerrain(nullptr);                                             // no pack: says so, does not crash
  b.host.openApp(Box::APP_MAP);
  u.run(300);
  CHECK(b.mapApp.alive() && !b.mapApp->hasPack());
  u.shortp();
  u.longp();
  CHECK(b.r.panel.presents > 0);
}
