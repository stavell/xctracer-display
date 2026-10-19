#pragma once
// Screen renders of the box: the home screen in its states, the vario's menu with its "Home" row, the compass
// tool's menu, Settings (list, calibration, About with and without a map pack). Included by emu_main.cpp; writes
// out/<name>.pgm like the other scenarios.
#include <stdio.h>
#include <string>
#include <vector>
#include "box.h"

namespace bs {

static void dump(Box& b, const char* dir, const char* name) {
  b.r.panel.dump((std::string(dir) + "/" + name + ".pgm").c_str());
}

static void shots(const char* dir) {
  static Box b;                                   // big: static, like the App objects on the device
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) b.tick(now); };
  auto press = [&](uint32_t dur) { b.r.input.press(now + 10, dur); run(dur + 300); };

  b.r.battery.pct = 76;
  b.r.settings.setTzMinutes(180);
  b.clock.set(10 * 3600 + 38 * 60, 0);            // 13:38 local
  b.host.start(Box::APP_LAUNCHER, now);
  run(300);
  dump(b, dir, "launcher");
  press(120);
  dump(b, dir, "launcher_tetris");
  press(120); press(120);                         // Settings
  dump(b, dir, "launcher_settings");
  b.r.input.press(now + 10, 2500);                // held past 1.5 s: the footer warns about power-off
  run(1800);
  dump(b, dir, "launcher_held");
  run(1500);

  b.host.start(Box::APP_VARIO, now);              // the vario's menu: back, Home, then the tracers found
  b.r.tracer.addItem("XC-Tracer", -52);
  run(300);
  press(900);
  press(120);
  dump(b, dir, "list_home");
  b.host.start(Box::APP_COMPASS, now);            // the compass tool's menu
  run(1000);
  press(900);
  dump(b, dir, "list_tool");

  // Settings: the list, a mounting change, the calibration while turning the unit, About.
  b.host.start(Box::APP_SETTINGS, now);
  run(300);
  dump(b, dir, "settings");
  press(120); press(900);                         // Board mounting +1
  press(120); press(900);                         // Start-up app -> Tetris
  press(120); press(900);                         // Time zone +1 h
  dump(b, dir, "settings_changed");
  b.r.settings.setMountIndex(0);
  b.host.start(Box::APP_SETTINGS, now);
  run(300);
  b.r.compass.hardIron[0] = 12; b.r.compass.hardIron[1] = -18; b.r.compass.hardIron[2] = 7;
  b.r.input.press(now + 10, 900); run(1500);      // Compass calibration
  for (int psi = 0; psi < 360; psi += 30)
    for (int pitch = -90; pitch <= 90; pitch += 45) {
      b.r.compass.psi = psi; b.r.compass.pitch = pitch; b.r.compass.roll = psi % 120;
      run(40);
    }
  run(300);
  dump(b, dir, "settings_cal");
  press(150);                                     // cancel
  b.r.compass.psi = b.r.compass.pitch = b.r.compass.roll = 0;
  press(120); press(120); press(120); press(120); press(120);  // down to About
  press(900);
  dump(b, dir, "settings_about_nopack");

  // Settings list with the new row, then the clock sleep: known time, a low battery, unknown time.
  b.host.start(Box::APP_SETTINGS, now);
  run(300);
  dump(b, dir, "settings_list");
  b.r.settings.setTzMinutes(180);
  b.clock.set(10 * 3600 + 38 * 60, now);
  b.r.battery.pct = 76;
  b.host.start(Box::APP_VARIO, now);
  run(300);
  b.r.input.press(now + 10, 3300);
  run(4500);
  dump(b, dir, "sleep_clock");
  b.r.battery.pct = 8;
  b.clock.set(7 * 3600 + 5 * 60 + 30, now);       // 10:05 local; the next minute change redraws
  run(40000);
  dump(b, dir, "sleep_clock_low");
  b.clock = WallClock();                          // never set: "--:--"
  press(150);                                     // a tap redraws
  dump(b, dir, "sleep_clock_unknown");
  b.r.input.press(now + 10, 1500);                // wake
  run(3000);

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
  b.setTerrain(&kit);
  b.host.start(Box::APP_LAUNCHER, now);
  run(300);
  dump(b, dir, "launcher_pack");
  b.host.start(Box::APP_SETTINGS, now);
  run(300);
  for (int i = 0; i < 5; i++) press(120);
  press(900);
  dump(b, dir, "settings_about");
  b.setTerrain(nullptr);
}

}  // namespace bs
