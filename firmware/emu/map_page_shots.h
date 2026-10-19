#pragma once
// Screen renders of the terrain features, from fixtures/sopot.pack: the vario with AGL, the map page (with its
// about overlay, without a fix, outside the pack). Included by emu_main.cpp; writes out/<name>.pgm. Skipped
// silently when the fixture is missing.
#include <math.h>
#include <new>
#include <stdio.h>
#include <string>
#include <vector>
#include "apps/vario/app/terrain_kit.h"
#include "mocks.h"

namespace ms {

static std::string xline(int sec, double lat, double lon, double alt, double course, double kmh, bool fix = true, int hour = 10) {
  char body[200];
  snprintf(body, sizeof body, "XCTRC,2026,8,16,%d,%d,%d,0,%.6f,%.6f,%.2f,%.2f,%.1f,0.1,,,,967.62,85", hour, 38 + sec / 60, sec % 60,
           fix ? lat : 0.0, fix ? lon : 0.0, alt, kmh, course);
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= (uint8_t)*p;
  char buf[220];
  snprintf(buf, sizeof buf, "$%s*%02x", body, cs);
  return buf;
}

static void shots(const char* dir) {
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static TerrainKit kit;
  FILE* f = fopen("fixtures/sopot.pack", "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END);
  bytes.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  bool ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  fclose(f);
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  if (!ok || !kit.open(&rd)) return;

  struct Scenario { const char* name; int page; int mode; int hour; };   // mode 0 normal, 1 no fix, 2 outside the pack
  const Scenario sc[] = {{"live_agl", 0, 0, 10}, {"map", 4, 0, 10}, {"map_nofix", 4, 1, 10}, {"map_outside", 4, 2, 10},
                          {"map_evening", 4, 0, 16}, {"map_night", 4, 0, 21}};   // low sun from the west; after dark: the fixed light
  for (const Scenario& s : sc) {
    // Fresh app and mocks per scenario, built in static storage (big; the soak accounting replaces global new/delete).
    alignas(64) static unsigned char rigMem[sizeof(Rig)], appMem[sizeof(Driven)];
    Rig& r = *new (rigMem) Rig();
    Driven& app = *new (appMem) Driven(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    app.setTerrain(&kit);
    kit.map.clearTrack();
    kit.map.invalidate();
    app.begin(0);
    uint32_t now = 0;
    auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) app.tick(now); };
    Command pc; pc.type = Command::PAGE; pc.value = s.mode == 3 ? 0 : s.page;
    r.laptop.cmds.push_back(pc);
    // 3 minutes at 40 km/h on a heading of 40 degrees, 1 Hz; the fixture's centre is near 42.62 N, 24.85 E
    const double mpd = 111195.0, v = 40.0 / 3.6, course = 40.0 * 0.017453292519943295;
    double lat = 42.58, lon = 24.80;
    const int total = 180;
    for (int sec = 0; sec < total; sec++) {
      lat += v * cos(course) / mpd;
      lon += v * sin(course) / (mpd * cos(lat * 0.017453292519943295));
      bool outside = s.mode == 2;
      bool fix = !(s.mode == 1 && sec >= total - 5);
      r.laptop.inject(xline(sec, outside ? 30.0 : lat, outside ? -20.0 : lon, 1450.0 + 3 * sin(sec / 20.0), 40.0, 40.0, fix, s.hour));
      run(300);
      if (s.mode == 3 && sec == total - 3) { Command c; c.type = Command::PAGE; c.value = 4; r.laptop.cmds.push_back(c); }
    }
    run(400);
    r.panel.dump((std::string(dir) + "/" + s.name + ".pgm").c_str());
    app.~Driven();
    r.~Rig();
  }
}

}  // namespace ms
