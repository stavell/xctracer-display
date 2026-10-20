#pragma once
// Screen renders of the box: the home screen in its states, the vario's device list, the system sheet, the compass
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
  auto toSleep = [&]() {                          // the 3 s hold opens the sheet; short presses to Sleep, a long press does it
    press(3300);
    for (int i = 0; i < 5 && b.host.sheet().cursorRow() != SystemSheet::R_SLEEP; i++) press(120);
    press(900);
  };

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
  b.r.input.press(now + 10, 2500);                // held 1.8 s: the hold bar along the bottom
  run(1800);
  dump(b, dir, "launcher_holdbar");
  run(1500);
  b.r.input.press(now + 10, 3400);                // the sheet over the launcher: Sleep first, no Home row
  run(3500);
  dump(b, dir, "sheet_launcher");
  press(120); press(120); press(900);             // Sleep -> Power off -> Back, then its 3 -> 6 s power-off bar
  b.r.input.press(now + 10, 5000);
  run(4300);
  dump(b, dir, "sheet_poweroff");
  run(1500);

  b.host.start(Box::APP_VARIO, now);              // the vario's device list: back, then the tracers found
  b.r.tracer.addItem("XC-Tracer", -52);
  run(300);
  press(900);
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
  // A synthetic day: steady 1018 hPa, a slow dip, then a fall of 4.5 hPa in the last 3 h (the forecast turns to rain).
  auto history = [&](float last, auto shape) {
    b.weather.clear();
    for (int i = 0; i < WeatherLog::kN; i++) b.weather.push(shape(i - (WeatherLog::kN - 1)));   // i - 143 = minus samples ago
    b.r.baro.hPa = last;
  };
  history(1013.3f, [](int k) { return k > -18 ? 1017.8f + (k + 18) * 0.25f : 1017.8f + 2.0f * sinf(k * 0.05f) ; });
  b.r.thermo.set(21.4f, 58);
  press(3300);
  dump(b, dir, "sheet_vario");                    // the system sheet over the vario
  for (int i = 0; i < 5 && b.host.sheet().cursorRow() != SystemSheet::R_SLEEP; i++) press(120);
  press(900);
  run(3600);
  dump(b, dir, "sleep_clock");
  b.r.battery.pct = 8;
  b.clock.set(7 * 3600 + 5 * 60 + 30, now);       // 10:05 local; the next minute change redraws
  run(40000);
  dump(b, dir, "sleep_clock_low");
  b.clock = WallClock();                          // never set: "--:--"
  b.r.baro.present = false;
  b.r.thermo.present = false;
  b.weather.clear();                              // no sensors, no history: every value reads "--"
  press(150);                                     // a tap redraws
  dump(b, dir, "sleep_clock_unknown");
  b.r.baro.present = true;
  b.r.thermo.present = true;
  b.r.input.press(now + 10, 1500);                // wake
  run(3000);
  // The other forecasts: rising (sun), steady (sun + cloud), a deep fall (storm), a sub-zero temperature.
  const struct { const char* name; float d3h, t, rh; Forecast f; } fc[] = {
    {"rising", 4.0f, 24.0f, 41, FC_SUN}, {"steady", 0.2f, 12.0f, 77, FC_SUN_CLOUD},
    {"cloudy", -2.0f, 6.0f, 88, FC_CLOUD}, {"rain", -4.5f, 9.0f, 93, FC_RAIN}, {"storm", -7.0f, -3.0f, 100, FC_STORM}};
  for (const auto& c : fc) {
    b.sleep = Box::SS_AWAKE;
    b.clock.set(15 * 3600 + 12 * 60, now);
    b.host.start(Box::APP_VARIO, now);
    run(300);
    const float end = 1004.6f;
    history(end, [&](int k) { return k > -18 ? end - c.d3h + (k + 18) * c.d3h / 18.0f : end - c.d3h + 3.0f * sinf(k * 0.04f); });
    b.r.thermo.set(c.t, c.rh);
    toSleep();
    run(3600);
    char nm[48];
    snprintf(nm, sizeof nm, "sleep_station_%s", c.name);
    dump(b, dir, nm);
    if (b.weather.forecast() != c.f) fprintf(stderr, "forecast mismatch for %s\n", c.name);
  }
  // Asleep after the loop: the power-off hold fills the countdown bar; the date variant is drawn directly (the box has no date yet).
  b.r.panel.setRotation(1);
  b.r.panel.clear();
  {
    Bitmap1 bm = b.r.panel.bitmap();
    clockview::draw(bm, 23 * 60 + 47, 12, &b.weather, 6, 12);
  }
  dump(b, dir, "sleep_station_countdown");
  b.r.panel.setRotation(1);
  b.r.panel.clear();
  {
    Bitmap1 bm = b.r.panel.bitmap();
    clockview::draw(bm, 8 * 60 + 5, 100, &b.weather, -1, 12, "Mon 19 Oct");
  }
  dump(b, dir, "sleep_station_date");

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
