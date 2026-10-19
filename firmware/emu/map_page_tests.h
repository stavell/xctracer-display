#pragma once
// Tests for the vario's terrain features: AGL in the view model, the map page in the page cycle, the cached
// map layer (when it re-renders, where things project), determinism, and that an App without a pack behaves
// exactly as before. Uses fixtures/sopot.pack. Included by tests.cpp after terrain_tests.h.
#include <chrono>
#include <string>
#include <vector>
#include "apps/vario/app/terrain_kit.h"
#include "platform/util/numfmt.h"
#include "platform/sensors/sun.h"
#include "apps/vario/core/xctrc.h"
#include <random>

namespace map_page_tests {
using namespace terrain;

static std::vector<uint8_t> gBytes;
static MemReader gReader;
static TerrainKit gKit;                                   // ~65 kB: static, like on the device
static TerrainKit gClosed;                                // never opened

static bool setup() {
  if (!gKit.ok) {
    if (!terrain_tests::loadFile("fixtures/sopot.pack", gBytes)) return false;
    gReader = MemReader(gBytes.data(), (uint32_t)gBytes.size());
    if (!gKit.open(&gReader)) return false;
  }
  return true;
}

// A tracer line at second `sec` with the given position, GNSS altitude and course.
static std::string xc(int sec, double lat, double lon, double alt, double course = 90, double kmh = 30) {
  char b[200];
  snprintf(b, sizeof b, "XCTRC,2026,8,16,10,38,%d,0,%.6f,%.6f,%.2f,%.2f,%.1f,0.1,,,,967.62,85", sec % 60, lat, lon, alt, kmh, course);
  return line(b);
}

struct Session {
  Rig r;
  Driven app;
  uint32_t now = 0;
  int sec = 0;
  explicit Session(TerrainKit* kit, AppConfig cfg = AppConfig())
      : app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power, cfg) {
    if (kit) app.setTerrain(kit);
    app.begin(0);
  }
  void run(uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); }
  void fly(double lat, double lon, double alt, double course = 90, int seconds = 1) {
    for (int i = 0; i < seconds; i++) { r.laptop.inject(xc(sec++, lat, lon, alt, course)); run(300); }
  }
  void page(int p) { Command c; c.type = Command::PAGE; c.value = p; r.laptop.cmds.push_back(c); run(100); }
  void press() { r.input.press(now + 10, 120); run(450); }
  int cur() { return app.buildViewModel(now).page; }
};

static void test_agl() {
  puts("terrain: AGL and clearance in the view model");
  CHECK(setup());
  // the reference point of the pack fixture: 42.5034827 N, 25.0832816 E, mean height 920.04 m, highest terrain around 1050 m
  const double lat = 42.503483, lon = 25.083282;
  float g = 0, c = 0;
  CHECK(gKit.terrain.elevation(lat, lon, &g) && gKit.terrain.ceiling(lat, lon, &c));
  NEAR(g, 920.04, 0.5);
  {
    Session s(&gKit);
    s.fly(lat, lon, 1000.0, 90, 3);
    const ViewModel& vm = s.app.buildViewModel(s.now);
    CHECK(vm.terrainOn && vm.fresh && vm.s.fix);
    NEAR(vm.agl, 1000.0 - g, 1e-3);
    NEAR(vm.agl, 79.96, 0.6);                              // GNSS altitude minus the model, by hand
    NEAR(vm.clearance, 1000.0 - c, 1e-3);
    CHECK(vm.clearance < vm.agl);                          // the ceiling is never below the mean height
    // no fix: the tracer sends 0, 0 and the old position must not be used
    s.r.laptop.inject(line("XCTRC,2026,8,16,10,38,5,0,0.000000,0.000000,1000.00,0.00,0.0,0.1,,,,967.62,85"));
    s.run(300);
    const ViewModel& nf = s.app.buildViewModel(s.now);
    CHECK(nf.terrainOn && !nf.s.fix && std::isnan(nf.agl) && std::isnan(nf.clearance));
    // fix again, then the data goes stale
    s.fly(lat, lon, 1000.0, 90, 2);
    CHECK(!std::isnan(s.app.buildViewModel(s.now).agl));
    s.run(3500);
    const ViewModel& old = s.app.buildViewModel(s.now);
    CHECK(!old.fresh && std::isnan(old.agl) && std::isnan(old.clearance));
  }
  {
    Session s(&gKit);                                      // outside the stored area (Atlantic): lookups fail, no number
    s.fly(30.0, -20.0, 500.0, 90, 3);
    const ViewModel& vm = s.app.buildViewModel(s.now);
    CHECK(vm.terrainOn && vm.fresh && vm.s.fix && std::isnan(vm.agl) && std::isnan(vm.clearance));
  }
  {
    Session s(&gKit);                                      // the barometric/GNSS altitude is the one that counts, not a made-up one
    s.fly(lat, lon, 1500.0, 90, 3);
    NEAR(s.app.buildViewModel(s.now).agl, 1500.0 - g, 1e-3);
  }
  {
    Session s(nullptr);                                    // no pack: nothing at all
    s.fly(lat, lon, 1000.0, 90, 3);
    const ViewModel& vm = s.app.buildViewModel(s.now);
    CHECK(!vm.terrainOn && std::isnan(vm.agl) && std::isnan(vm.clearance) && vm.map == nullptr);
  }
  {
    Session s(&gClosed);                                   // a kit whose pack was rejected counts as no pack
    s.fly(lat, lon, 1000.0, 90, 3);
    CHECK(!s.app.buildViewModel(s.now).terrainOn && !s.app.hasTerrain());
  }
}

static void test_page_cycle() {
  puts("terrain: the map page is in the page cycle only with a pack");
  CHECK(setup());
  {
    Session s(nullptr);
    CHECK(s.cur() == ViewModel::PAGE_COMPASS);
    int seen[8], n = 0;
    for (int i = 0; i < 6; i++) { s.press(); seen[n++] = s.cur(); }
    CHECK(seen[0] == ViewModel::PAGE_THERMAL && seen[1] == ViewModel::PAGE_CENTER && seen[2] == ViewModel::PAGE_PROFILE &&
          seen[3] == ViewModel::PAGE_HOME && seen[4] == ViewModel::PAGE_COMPASS && seen[5] == ViewModel::PAGE_THERMAL);
    s.page(ViewModel::PAGE_MAP);                           // the laptop cannot open it either
    CHECK(s.cur() != ViewModel::PAGE_MAP);
  }
  {
    Session s(&gKit);
    int seen[8], n = 0;
    for (int i = 0; i < 7; i++) { s.press(); seen[n++] = s.cur(); }
    CHECK(seen[0] == ViewModel::PAGE_THERMAL && seen[1] == ViewModel::PAGE_CENTER && seen[2] == ViewModel::PAGE_MAP &&
          seen[3] == ViewModel::PAGE_PROFILE && seen[4] == ViewModel::PAGE_HOME && seen[5] == ViewModel::PAGE_COMPASS &&
          seen[6] == ViewModel::PAGE_THERMAL);
    s.page(ViewModel::PAGE_MAP);
    CHECK(s.cur() == ViewModel::PAGE_MAP);
    s.app.setTerrain(nullptr);                             // detaching while on the map page falls back to the compass
    s.run(100);
    CHECK(s.cur() == ViewModel::PAGE_COMPASS);
    s.app.setTerrain(&gKit);
  }
}

static uint32_t rawHash(Rig& r) {
  uint32_t h = 2166136261u;
  const uint8_t* p = r.panel.raw();
  for (int i = 0; i < 400 * 240 / 8; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static int blackPixels(Rig& r) {
  int n = 0;
  for (int i = 0; i < 400 * 240; i++) n += !r.panel.rawPixel(i % 400, i / 400);
  return n;
}

static void test_screens() {
  puts("terrain: map page render (deterministic) and screens without a pack");
  CHECK(setup());
  const double lat = 42.62, lon = 24.85;
  auto render = [&](TerrainKit* kit, int page, uint32_t* hash, int* black) {
    Session s(kit);
    s.fly(lat, lon, 1300.0, 40, 4);
    s.page(page);
    s.fly(lat, lon, 1300.0, 40, 2);
    *hash = rawHash(s.r);
    *black = blackPixels(s.r);
  };
  uint32_t h1, h2, h3, hc, hn;
  int b1, b2, b3, bc, bn;
  gKit.map.invalidate();
  render(&gKit, ViewModel::PAGE_MAP, &h1, &b1);
  gKit.map.invalidate();
  render(&gKit, ViewModel::PAGE_MAP, &h2, &b2);
  CHECK(h1 == h2 && b1 == b2);                             // the same input draws the same pixels
  CHECK(b1 > 3000 && b1 < 400 * 240 * 0.8);                // something is there: not blank, not black
  // the same page without a pack is the compass (the map page does not exist), and an unopened kit changes nothing
  render(nullptr, ViewModel::PAGE_COMPASS, &hn, &bn);
  render(&gClosed, ViewModel::PAGE_COMPASS, &hc, &bc);
  CHECK(hn == hc && bn == bc);
  render(&gKit, ViewModel::PAGE_COMPASS, &h3, &b3);
  CHECK(h3 != hn);                                         // with a pack the AGL tile shows
  // outside the pack: the page says so instead of showing a made-up picture
  {
    Session s(&gKit);
    s.fly(30.0, -20.0, 500.0, 40, 4);
    s.page(ViewModel::PAGE_MAP);
    s.fly(30.0, -20.0, 500.0, 40, 2);
    CHECK(gKit.map.valid() && gKit.map.noData());
  }
  // north up is the default; heading up (AppConfig::mapHeadingUp) turns the cached window to the heading
  {
    Session s(&gKit);
    s.page(ViewModel::PAGE_MAP);
    s.fly(lat, lon, 1300.0, 40, 8);
    CHECK(gKit.map.valid() && gKit.map.headingDeg() == 0);
    AppConfig cfg; cfg.mapHeadingUp = true;
    Session u(&gKit, cfg);
    u.page(ViewModel::PAGE_MAP);
    u.fly(lat, lon, 1300.0, 40, 12);
    NEAR(gKit.map.headingDeg(), 40, 8);
  }
  // the vario cells themselves are untouched by terrain except the AGL tile: top part identical
  {
    Session a(nullptr), b(&gKit);
    a.fly(lat, lon, 1300.0, 40, 4);
    b.fly(lat, lon, 1300.0, 40, 4);
    a.run(400); b.run(400);
    int diffTop = 0;
    for (int y = 0; y < 330; y++)
      for (int x = 0; x < 240; x++) diffTop += a.r.panel.rawPixel(399 - y, x) != b.r.panel.rawPixel(399 - y, x);
    CHECK(diffTop == 0);                                   // header, cells and compass: same pixels
  }
}

static void test_map_cache() {
  puts("terrain: cached map layer (when it renders, where things project, track)");
  CHECK(setup());
  TerrainMap& m = gKit.map;
  m.invalidate();
  TerrainMap::Params p;
  p.lat = 42.62; p.lon = 24.85; p.headingDeg = 0; p.mPerPx = 100; p.w = 218; p.h = 376;
  const double mpd = gKit.terrain.info().mPerDeg;
  const double cosLat = cos(p.lat * D2R);
  const Font1* font = nullptr;
  uint32_t t = 1000;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, font, t) == TerrainMap::RENDERED);
  CHECK(m.valid() && !m.noData() && m.dataPixels() == 218 * 376);
  uint32_t n0 = m.renders();
  float x, y;
  CHECK(m.project(p.lat, p.lon, &x, &y));
  NEAR(x, 109, 0.6); NEAR(y, 188 + 0.12 * 376, 0.6);       // the pilot sits below the centre
  CHECK(m.project(p.lat + 5000 / mpd, p.lon, &x, &y));     // 5 km north is 50 px up (north up)
  NEAR(x, 109, 0.6); NEAR(y, 188 + 0.12 * 376 - 50, 0.8);
  CHECK(m.project(p.lat, p.lon + 5000 / (mpd * cosLat), &x, &y));   // 5 km east is 50 px right
  NEAR(x, 109 + 50, 0.8);

  // moving less than 10 px keeps the layer; more re-renders
  TerrainMap::Params q = p;
  q.lat = p.lat + 800 / mpd;                               // 8 px
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 100) == TerrainMap::KEPT && m.renders() == n0);
  q.lat = p.lat + 1100 / mpd;                              // 11 px
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 200) == TerrainMap::RENDERED && m.renders() == n0 + 1);
  // after the re-render the pilot is back at its place
  CHECK(m.project(q.lat, q.lon, &x, &y));
  NEAR(x, 109, 0.6); NEAR(y, 188 + 0.12 * 376, 0.6);
  // turning: 4 deg keeps, 6 deg renders; the cached heading follows
  q = p;
  q.lat = p.lat + 1100 / mpd; q.headingDeg = 4;
  uint32_t n1 = m.renders();
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 300) == TerrainMap::KEPT);
  q.headingDeg = 6;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 400) == TerrainMap::RENDERED && m.renders() == n1 + 1);
  NEAR(m.headingDeg(), 6, 1e-4);
  // heading east: east is up. 5 km east of the pilot is 50 px above it
  q.headingDeg = 90;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 500) == TerrainMap::RENDERED);
  CHECK(m.project(q.lat, q.lon + 5000 / (mpd * cos(q.lat * D2R)), &x, &y));
  NEAR(x, 109, 1.0); NEAR(y, 188 + 0.12 * 376 - 50, 1.0);
  // a small change shows only once the picture is old enough
  uint32_t n2 = m.renders();
  TerrainMap::Params s2 = q;
  s2.lat += 300 / mpd;                                     // 3 px: below the 10 px rule
  s2.headingDeg = 90;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, s2, font, t + 600) == TerrainMap::KEPT && m.renders() == n2);
  CHECK(m.updateSync(gKit.terrain, gKit.vec, s2, font, t + 500 + TerrainMap::kRefreshMs + 10) == TerrainMap::RENDERED);
  // a new scale re-renders and changes the projection
  q = s2; q.mPerPx = 50;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 20000) == TerrainMap::RENDERED);
  CHECK(m.project(q.lat + 0.0, q.lon + 2500 / (mpd * cos(q.lat * D2R)), &x, &y));
  NEAR(y, 188 + 0.12 * 376 - 50, 1.5);
  // no heading: north up, the pilot in the centre
  q.headingDeg = NAN; q.mPerPx = 100;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 30000) == TerrainMap::RENDERED && m.headingDeg() == 0);
  CHECK(m.project(q.lat, q.lon, &x, &y));
  NEAR(x, 109, 0.6); NEAR(y, 188, 0.6);
  // north up does not re-render for a turn, only for movement
  const uint32_t n3 = m.renders();
  q.headingDeg = NAN;
  q.lat += 500 / mpd;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, q, font, t + 30100) == TerrainMap::KEPT && m.renders() == n3);

  // identical params into two layers: identical bytes
  static TerrainMap other;
  other.updateSync(gKit.terrain, gKit.vec, q, font, t + 40000);
  m.invalidate();
  m.updateSync(gKit.terrain, gKit.vec, q, font, t + 40000);
  CHECK(memcmp(m.cache().buf, other.cache().buf, TerrainMap::kStride * 218) == 0);

  // ground track: one point per 5 flight seconds, oldest first, a ring
  m.clearTrack();
  for (int i = 0; i < 20; i++) m.feedTrack(42.0 + i * 0.001, 25.0, 1000u * i);
  CHECK(m.trackCount() == 4);                              // seconds 0, 5, 10, 15
  double la, lo;
  CHECK(m.trackAt(0, &la, &lo)); NEAR(la, 42.0, 1e-6);
  CHECK(m.trackAt(3, &la, &lo)); NEAR(la, 42.015, 1e-6);
  CHECK(!m.trackAt(4, &la, &lo) && !m.trackAt(-1, &la, &lo));
  for (int i = 0; i < 1000; i++) m.feedTrack(43.0 + i * 1e-4, 25.0, 100000u + 5000u * i);
  CHECK(m.trackCount() == TerrainMap::kTrackMax);
  CHECK(m.trackAt(TerrainMap::kTrackMax - 1, &la, &lo)); NEAR(la, 43.0 + 999 * 1e-4, 1e-6);
  CHECK(m.trackAt(0, &la, &lo)); NEAR(la, 43.0 + (1000 - TerrainMap::kTrackMax) * 1e-4, 1e-6);
  m.feedTrack(NAN, 25.0, 1u << 30);
  CHECK(m.trackCount() == TerrainMap::kTrackMax);          // garbage is not stored
  m.clearTrack();
  CHECK(m.trackCount() == 0);
}

// Sun position against values worked out by hand (equinox / solstice noon) and the shading direction.
static void test_credits() {
  puts("map credits banner: once per boot, closes at a press or a fix");
  CHECK(setup());
  App::forgetCreditsShown();
  {
    Session s(&gKit);
    s.run(500);
    CHECK(s.app.buildViewModel(s.now).credits);
    s.run(3000);
    CHECK(!s.app.buildViewModel(s.now).credits);          // timed out
  }
  {
    Session s(&gKit);                                      // a later entry in the same boot
    s.run(500);
    CHECK(!s.app.buildViewModel(s.now).credits);
  }
  App::forgetCreditsShown();
  {
    Session s(&gKit);
    s.run(500);
    CHECK(s.app.buildViewModel(s.now).credits);
    s.fly(42.62, 24.85, 1300.0, 40, 1);
    CHECK(!s.app.buildViewModel(s.now).credits);          // a fix closes it at once
  }
  App::forgetCreditsShown();
  {
    Session s(&gKit);
    s.run(500);
    s.press();
    CHECK(!s.app.buildViewModel(s.now).credits);
  }
  Session n(nullptr);                                      // no pack: nothing to credit
  App::forgetCreditsShown();
  n.run(500);
  CHECK(!n.app.buildViewModel(n.now).credits);
}

static void test_sun() {
  puts("sun: position formula, light vector, shading by the sun");
  float az, el;
  // equator, prime meridian, 2026-06-21 12:00 UTC: sun 66.5 degrees high, in the north (equation of time ~ -1.7 min, 0.4 deg)
  CHECK(sunPosition(2026, 6, 21, 12 * 3600, 0.0, 0.0, &az, &el));
  NEAR(el, 66.5, 0.6); CHECK(az < 8.0f || az > 352.0f);
  // Sofia (42.7 N, 23.3 E), 2026-12-21: noon sun ~ 23.9 degrees, due south; 06:00 UTC is a good night-time-ish low sun
  CHECK(sunPosition(2026, 12, 21, (int)((12 - 23.3 / 15.0 - 0.03) * 3600), 42.7, 23.3, &az, &el));
  NEAR(el, 90 - 42.7 - 23.44, 0.6); NEAR(az, 180, 3.0);
  CHECK(sunPosition(2026, 9, 22, (int)((6 - 23.3 / 15.0) * 3600), 42.7, 23.3, &az, &el));   // equinox sunrise: due east, at the horizon
  NEAR(el, 0, 2.5); NEAR(az, 90, 5.0);
  CHECK(sunPosition(2026, 8, 16, 0, 42.6, 24.8, &az, &el) && el < -20);                        // local night
  CHECK(!sunPosition(2026, 13, 1, 0, 0, 0, &az, &el) && !sunPosition(2026, 1, 1, 90000, 0, 0, &az, &el));
  CHECK(!sunPosition(2026, 1, 1, 0, 95, 0, &az, &el) && !sunPosition(2026, 1, 1, 0, NAN, 0, &az, &el));
  // the light vector in screen axes: sun in the south, window north up: the light comes from the bottom of the screen
  ReliefStyle st;
  st.setSun(180, 30, 0);
  NEAR(st.lightX, 0, 1e-4); NEAR(st.lightY, -0.8660, 1e-3); NEAR(st.lightZ, 0.5, 1e-4);
  st.setSun(90, 30, 0);                                    // east: from the right
  NEAR(st.lightX, 0.866, 1e-3); NEAR(st.lightY, 0, 1e-4);
  st.setSun(90, 30, 90);                                   // east is up when heading east: the light comes from the top
  NEAR(st.lightX, 0, 1e-3); NEAR(st.lightY, 0.866, 1e-3);
  // the date of a tracer line reaches the sample; a bad date is "unknown", not an error
  Sample smp;
  CHECK(xctrcParse(xc(5, 42.0, 25.0, 1000).c_str(), smp) && smp.year == 2026 && smp.month == 8 && smp.day == 16);
  CHECK(xctrcParse(line("XCTRC,xx,8,16,10,38,5,0,42.0,25.0,1000,30,90,0.1,,,,967.62,85").c_str(), smp) && smp.year == 0);

  // the shaded map differs from the fixed light, and a sun below kSunMinEl gives the fixed-light picture exactly
  CHECK(setup());
  TerrainMap& m = gKit.map;
  static TerrainMap fixedMap;
  TerrainMap::Params p;
  p.lat = 42.62; p.lon = 24.85; p.headingDeg = NAN; p.mPerPx = 100;
  m.invalidate(); fixedMap.invalidate();
  CHECK(fixedMap.updateSync(gKit.terrain, gKit.vec, p, nullptr, 0) == TerrainMap::RENDERED && !fixedMap.sunLit());
  p.sunAz = 260; p.sunEl = 15;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 0) == TerrainMap::RENDERED && m.sunLit());
  CHECK(memcmp(m.cache().buf, fixedMap.cache().buf, TerrainMap::kStride * 218) != 0);
  p.sunEl = 1.0f;                                          // too low: twilight
  m.invalidate();
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 0) == TerrainMap::RENDERED && !m.sunLit());
  CHECK(memcmp(m.cache().buf, fixedMap.cache().buf, TerrainMap::kStride * 218) == 0);
  // the sun moving a little keeps the picture; a lot (or setting) re-shades
  p.sunAz = 200; p.sunEl = 40;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 10) == TerrainMap::RENDERED && m.sunLit());
  p.sunAz = 203; p.sunEl = 38;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 20) == TerrainMap::KEPT);
  p.sunAz = 212;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 30) == TerrainMap::RENDERED);
  p.sunEl = NAN;
  CHECK(m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 40) == TerrainMap::RENDERED && !m.sunLit());
}

// reliefStrip in pieces gives exactly the bytes of relief(); the map job in small steps gives exactly the bytes of the
// synchronous render, the shown picture does not change while it runs, and no step does more than its unit budget.
static void test_map_incremental() {
  puts("terrain: relief strips and the incremental map job");
  CHECK(setup());
  Window win;
  win.lat = 42.62; win.lon = 24.85; win.mPerPx = 100; win.w = 218; win.h = 376; win.headingDeg = 37;
  static uint8_t g1[218 * 376], c1[218 * 376], g2[64 * 7], c2[64 * 7];
  const ReliefStyle st = ReliefStyle::light();
  gKit.terrain.relief(win, st, 0, 0, 376, g1, c1);
  const int sw = gKit.terrain.stripWidth(win);
  CHECK(sw >= 16 && sw <= kMaxStrip);
  int bad = 0, total = 0;
  for (int xs = 0; xs < 218; xs += sw) {
    const int xe = xs + sw < 218 ? xs + sw : 218;
    for (int y = 0; y < 376; y += 7) {                     // 7 rows at a time, resuming the strip
      const int y1 = y + 7 < 376 ? y + 7 : 376;
      total += gKit.terrain.reliefStrip(win, st, 0, xs, xe, y, y1, g2, c2) >= 0;
      for (int yy = y; yy < y1; yy++)
        for (int x = xs; x < xe; x++) {
          bad += g2[(yy - y) * (xe - xs) + (x - xs)] != g1[yy * 218 + x];
          bad += c2[(yy - y) * (xe - xs) + (x - xs)] != c1[yy * 218 + x];
        }
    }
  }
  CHECK(total > 0 && bad == 0);
  // a strip interrupted by another render restarts cleanly (no stale rows)
  gKit.terrain.reliefStrip(win, st, 0, 0, sw, 0, 7, g2, c2);
  gKit.terrain.relief(win, st, 0, 100, 110, g1, c1);
  gKit.terrain.reliefStrip(win, st, 0, 0, sw, 7, 14, g2, c2);
  gKit.terrain.relief(win, st, 0, 0, 376, g1, c1);
  bad = 0;
  for (int yy = 7; yy < 14; yy++) for (int x = 0; x < sw; x++) bad += g2[(yy - 7) * sw + x] != g1[yy * 218 + x];
  CHECK(bad == 0);

  TerrainMap& m = gKit.map;
  static TerrainMap ref;
  TerrainMap::Params p;
  p.lat = 42.62; p.lon = 24.85; p.headingDeg = NAN; p.mPerPx = 100; p.sunAz = 230; p.sunEl = 35;
  m.invalidate(); ref.invalidate();
  CHECK(ref.updateSync(gKit.terrain, gKit.vec, p, nullptr, 0) == TerrainMap::RENDERED);
  CHECK(m.update(gKit.terrain, gKit.vec, p, nullptr, 0) == TerrainMap::STARTED && !m.valid() && m.building());
  int calls = 0;
  while (m.building() && calls < 100000) { m.step(gKit.terrain, gKit.vec, nullptr, 1000); calls++; }
  CHECK(!m.building() && m.valid() && calls > 5);
  CHECK(memcmp(m.cache().buf, ref.cache().buf, TerrainMap::kStride * 218) == 0);
  printf("  map job: %d steps of %d units to build a picture\n", calls, TerrainMap::kUnitsNoClock);
  // the pilot moves on: the old picture stays byte for byte while the new one is built, then it is swapped
  static uint8_t shown[TerrainMap::kStride * TerrainMap::kMaxW];
  memcpy(shown, m.cache().buf, sizeof shown);
  const double mpd = gKit.terrain.info().mPerDeg;
  p.lat += 1200 / mpd;                                    // 12 px north
  CHECK(m.update(gKit.terrain, gKit.vec, p, nullptr, 100) == TerrainMap::STARTED);
  float x, y;
  CHECK(m.project(p.lat, p.lon, &x, &y) && fabsf(y - 188) > 11 && fabsf(y - 188) < 13);   // still the old window: the pilot is 12 px off its centre
  bool swapped = false;
  int n = 0;
  while (m.building()) {
    CHECK(memcmp(m.cache().buf, shown, sizeof shown) == 0);
    swapped = m.step(gKit.terrain, gKit.vec, nullptr, 1000);
    CHECK(m.update(gKit.terrain, gKit.vec, p, nullptr, 100 + n) == (swapped ? TerrainMap::KEPT : TerrainMap::STARTED));
    n++;
    if (n > 100000) break;
  }
  CHECK(swapped && memcmp(m.cache().buf, shown, sizeof shown) != 0);
  CHECK(m.project(p.lat, p.lon, &x, &y)); NEAR(y, 188, 0.6);
  // a pilot far from the picture's window (a long pause on another page): the picture is dropped, not shown wrong
  p.lat += 12000 / mpd;
  CHECK(m.update(gKit.terrain, gKit.vec, p, nullptr, 500) == TerrainMap::STARTED && !m.valid());
  m.invalidate();
  CHECK(!m.building() && !m.step(gKit.terrain, gKit.vec, nullptr, 1000));
}

static void test_map_cost() {
  puts("terrain: map render cost on the PC (informational)");
  CHECK(setup());
  printf("  RAM: TerrainKit %zu B (Terrain %zu, MapVectors %zu, TerrainMap %zu: cache %d + bands %d + track %zu)\n", sizeof(TerrainKit),
         sizeof(Terrain), sizeof(MapVectors), sizeof(TerrainMap), TerrainMap::kStride * TerrainMap::kMaxW,
         2 * terrain::kMaxStrip * TerrainMap::kRows, 8 * (size_t)TerrainMap::kTrackMax);
  TerrainMap& m = gKit.map;
  TerrainMap::Params p;
  p.lat = 42.62; p.lon = 24.85; p.mPerPx = 100;
  for (float hd : {0.0f, 45.0f}) {
    for (float sc : {50.0f, 100.0f, 200.0f}) {
      p.headingDeg = hd; p.mPerPx = sc;
      gKit.terrain.flush();
      m.invalidate();
      auto t0 = std::chrono::steady_clock::now();
      m.updateSync(gKit.terrain, gKit.vec, p, nullptr, 0);
      double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e3;
      printf("  map render 218x376 @%.0f m/px heading %.0f (cold tile cache, no labels): %.1f ms\n", sc, hd, ms);
    }
  }
}

// The same with the real Bulgaria pack (when the study's file exists) and the town labels: what a full page
// render costs here. Informational; the device is much slower (see NOTES.md for the estimate).
static void test_map_cost_real() {
  static TerrainKit real;
  static std::vector<uint8_t> bytes;
  static MemReader rd;
  const char* path = "/private/tmp/claude-501/-Users-stavel-dev-display/da365ad1-40f2-499d-917a-3a6befca5ade/scratchpad/terrain/bulgaria.pack";
  if (!terrain_tests::loadFile(path, bytes)) { puts("  (no Bulgaria pack: real-pack timing skipped)"); return; }
  rd = MemReader(bytes.data(), (uint32_t)bytes.size());
  CHECK(real.open(&rd));
  const Font1* font = viewFont(viewGfxFont(1), TerrainMap::kCacheRotation);
  CHECK(font != nullptr);
  struct Spot { const char* name; double lat, lon; } spots[] = {{"Veliki Preslav flight", 43.25, 26.93}, {"Rila", 42.18, 23.55}};
  for (const Spot& sp : spots)
    for (float hd : {0.0f, 135.0f}) {
      TerrainMap::Params p;
      p.lat = sp.lat; p.lon = sp.lon; p.headingDeg = hd; p.mPerPx = 100;
      real.terrain.flush();
      real.map.invalidate();
      auto t0 = std::chrono::steady_clock::now();
      real.map.updateSync(real.terrain, real.vec, p, font, 0);
      double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e3;
      printf("  real pack, %s, heading %.0f, 100 m/px, roads + towns + labels: %.1f ms, %d px with data, %u tile decodes\n", sp.name, hd, ms,
             real.map.dataPixels(), (unsigned)real.terrain.tileDecodes());
      CHECK(real.map.valid() && !real.map.noData());
    }
}

// fmtFixed must give the very string snprintf gives, for every format the views use.
static void test_numfmt() {
  const char* fmts[] = {"%.0f", "%.1f", "%+.0f", "%+.1f", "%.2f", "%.3f", "%+.3f"};
  long bad = 0, n = 0;
  auto one = [&](float v) {
    for (const char* f : fmts) {
      int dec; bool plus;
      CHECK(fmtFixedSpec(f, &dec, &plus));
      char a[32], b[32];
      snprintf(a, sizeof a, f, v);
      const bool ok = fmtFixed(b, sizeof b, v, dec, plus);
      n++;
      if (!ok) { if (!(fabsf(v) >= 1e9f)) { bad++; printf("  numfmt refused %g\n", v); } continue; }
      if (strcmp(a, b) != 0) { if (bad++ < 10) printf("  numfmt %s of %.9g: snprintf '%s', fmtFixed '%s'\n", f, v, a, b); }
    }
  };
  // ties, rounding edges, signs, zero, tiny, large
  for (int i = -4000; i <= 4000; i++) {
    one(i * 0.5f); one(i * 0.25f); one(i * 0.125f); one(i * 0.05f); one(i * 0.1f); one(i * 0.01f); one(i * 0.001f);
    one(i * 0.05f + 0.0001f); one(i * 0.05f - 0.0001f);
  }
  one(0.0f); one(-0.0f); one(-0.04f); one(-0.4f); one(-0.5f); one(0.5f); one(1.5f); one(2.5f); one(0.95f); one(0.05f);
  one(9.95f); one(99.95f); one(999999.5f); one(123456789.0f); one(999999999.0f); one(1e-30f); one(-1e-30f);
  one(1e-45f); one(1.17549435e-38f); one(8388607.5f); one(16777216.0f); one(0.99999994f); one(-0.99999994f);
  // every float of a few binades by consecutive bit patterns, and random bit patterns
  for (uint32_t bitsBase : {0x3F7FFF00u, 0x3F000000u, 0x41200000u, 0x4B7FFF00u, 0x00000000u, 0x80000000u, 0xBF7FFF00u}) {
    for (uint32_t k = 0; k < 4096; k++) {
      uint32_t bits = bitsBase + k;
      float v; memcpy(&v, &bits, 4);
      if (isfinite(v)) one(v);
    }
  }
  std::mt19937 rng(12345);
  for (int i = 0; i < 40000; i++) {
    uint32_t bits = rng();
    float v; memcpy(&v, &bits, 4);
    if (isfinite(v)) one(v);
    one((float)((int)(rng() % 2000001) - 1000000) / 1000.0f);        // plausible readings: +-1000 with 3 decimals
    one((float)((int)(rng() % 20000001) - 10000000) / 100.0f);
  }
  printf("  numfmt: %ld strings compared with snprintf, %ld differences\n", n, bad);
  CHECK(bad == 0);
  char s[4];
  CHECK(fmtFixed(s, sizeof s, 12345.0f, 1, false) && strcmp(s, "123") == 0);   // truncates like snprintf
  CHECK(!fmtFixed(s, sizeof s, NAN, 1, false) && !fmtFixed(s, sizeof s, 2e9f, 0, false));
  { int dd; bool pp; CHECK(!fmtFixedSpec("%d", &dd, &pp) && !fmtFixedSpec("%.4f", &dd, &pp)); }
}

// The float map maths against the double formulas they replaced.
static void test_map_float_maths() {
  if (!setup()) { CHECK(false); return; }
  const MapVectors& vec = gKit.vec;
  const PackInfo& I = gKit.terrain.info();
  double worstUnit = 0, worstXf = 0;
  std::mt19937 rng(7);
  for (int i = 0; i < 2000; i++) {
    const double lat = 41.3 + (rng() % 29000) * 1e-4, lon = 22.4 + (rng() % 60000) * 1e-4;
    const int32_t la = (int32_t)llround(lat * 1e7), lo = (int32_t)llround(lon * 1e7);
    double ux, uy;
    vec.latLonToUnit(la * 1e-7, lo * 1e-7, &ux, &uy);
    float fx, fy;
    vec.latLonToUnitE7(la, lo, &fx, &fy);
    worstUnit = fmax(worstUnit, fmax(fabs(fx - ux), fabs(fy - uy)));
    // transform against the double formulas (the old code) for a random window
    Window w; w.lat = lat; w.lon = lon; w.mPerPx = 25.0f + (rng() % 200); w.headingDeg = (rng() % 3600) / 10.0f;
    const VecXform t = vec.makeXform(w);
    const double mpd = I.mPerDeg, u = I.unitM, cosLat = cos(w.lat * 0.017453292519943295);
    const double th = w.headingDeg * 0.017453292519943295, ct = cos(th), sn = sin(th);
    const double ex = u * cosLat / I.cos0, e0 = (I.originLon_e7 * 1e-7 - w.lon) * mpd * cosLat, ny = -u,
                 n0 = (I.originLat_e7 * 1e-7 - w.lat) * mpd, s = w.mPerPx;
    const double a = ex * ct / s, b = -ny * sn / s, c = w.w * 0.5 + (e0 * ct - n0 * sn) / s;
    const double d = -ex * sn / s, e = -ny * ct / s, f = w.h * 0.5 - (e0 * sn + n0 * ct) / s;
    // pixel error over the whole window, from units up to +-3000 away from the centre
    for (int k = 0; k < 4; k++) {
      const double x = ux + (k & 1 ? 3000 : -3000), y = uy + (k & 2 ? 3000 : -3000);
      worstXf = fmax(worstXf, fmax(fabs(t.px((float)x, (float)y) - (a * x + b * y + c)),
                                   fabs(t.py((float)x, (float)y) - (d * x + e * y + f))));
    }
    (void)I;
  }
  printf("  float map maths: worst unit error %.4f units (10 m), worst pixel error %.4f px\n", worstUnit, worstXf);
  CHECK(worstUnit < 0.01);
  CHECK(worstXf < 0.05);
}

static void run() {
  test_agl();
  test_page_cycle();
  test_screens();
  test_map_cache();
  test_credits();
  test_sun();
  test_map_incremental();
  test_map_cost();
  test_map_cost_real();
  test_numfmt();
  test_map_float_maths();
}

}  // namespace map_page_tests
