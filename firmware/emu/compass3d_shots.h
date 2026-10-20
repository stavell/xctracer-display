#pragma once
// Renders of the 3D compass (the compass tool's page) through the whole box: a scripted orientation sequence (turning, tilting, a
// quick spin that shows the inertia of the card, a disturbed field, no calibration, no sensor) drives the mock compass, whose gyro
// follows the motion, so the AHRS and the card behave as on the device. Writes out/compass3d_<name>.pgm; compass3d_sheet.py puts
// them on one contact sheet (docs/screens/compass3d_sheet.png). Included by emu_main.cpp.
#include <stdio.h>
#include <string>
#include "box.h"

namespace c3s {

struct Run {
  Box& b;
  uint32_t now = 0;
  double psi = 0, pitch = 0, roll = 0;
  explicit Run(Box& box) : b(box) {}
  void set() { b.r.compass.psi = psi; b.r.compass.pitch = pitch; b.r.compass.roll = roll; }
  void feed() { if (now - thermoAt >= 2000) { thermoAt = now; b.r.thermo.set(21.4f + 0.2f * (float)((now / 2000) % 3), 48); } }
  uint32_t thermoAt = 0;
  void hold(uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) { set(); feed(); b.tick(now); } }
  // Smooth move to a new orientation over ms (linear in each angle).
  void move(double p, double t, double r, uint32_t ms) {
    const double p0 = psi, t0 = pitch, r0 = roll;
    const uint32_t start = now;
    for (uint32_t e = now + ms; now < e; now += 10) {
      const double k = (double)(now + 10 - start) / ms;
      psi = p0 + (p - p0) * k; pitch = t0 + (t - t0) * k; roll = r0 + (r - r0) * k;
      set();
      feed();
      b.tick(now);
    }
  }
  void shortp() { b.r.input.press(now + 10, 120); hold(420); }
  void dump(const char* dir, const char* name) { b.r.panel.dump((std::string(dir) + "/compass3d_" + name + ".pgm").c_str()); }
};

static void shots(const char* dir) {
  static Box b;
  Run u(b);
  b.r.compass.gyro = true;
  b.r.compass.gyroFromMotion = true;
  b.r.battery.pct = 76;
  MagCal cal;
  cal.valid = true;
  cal.radius = 44.7f;
  b.r.settings.saveMagCal(cal);
  b.r.thermo.set(21.4f, 48);
  b.host.start(Box::APP_COMPASS, u.now);
  u.hold(3000);                                   // the AHRS seeds and settles
  b.weather.clear();                              // a falling barometer over 5 h, so the tendency arrow shows
  for (int i = 0; i < 30; i++) b.weather.push(1016.0f - 0.11f * (float)i);
  u.dump(dir, "00_flat_north");
  u.move(135, 0, 0, 1500); u.hold(1200);
  u.dump(dir, "01_tilt_0");
  u.move(135, 35, 0, 1000); u.hold(1500);
  u.dump(dir, "02_pitch_up");
  u.move(250, 20, 28, 1500); u.hold(1500);
  u.dump(dir, "03_roll_right");
  u.move(250, -25, -22, 1200); u.hold(1500);
  u.dump(dir, "04_pitch_down_roll_left");
  u.move(250, 30, 0, 1200); u.hold(1500);
  u.dump(dir, "05_tilt_30");
  u.move(250, 60, 0, 1200); u.hold(1500);
  u.dump(dir, "05b_tilt_60_clamped");
  // a quick turn: the card lags and overshoots, the number is exact
  u.move(250, 25, 0, 800); u.hold(1500);
  u.move(250 + 150, 25, 0, 180);
  u.dump(dir, "06_spin_fast");
  u.hold(120);
  u.dump(dir, "07_spin_after");
  u.hold(1500);
  // the flat page (a short press): heading tape and numerals, level, field quality, weather
  u.shortp();
  u.move(135, 12, -6, 1000); u.hold(1200);
  u.dump(dir, "12_flat_se");
  u.move(358, 4, 3, 1500); u.hold(1200);
  u.dump(dir, "13_flat_north");
  u.shortp();                                     // back to the dome
  u.move(10, 110, 10, 1500); u.hold(1500);
  u.dump(dir, "08_upside_down");
  u.move(300, 15, 8, 1500); u.hold(2000);
  // disturbed field: a magnet next to the unit
  b.r.compass.hardIron[0] = 30; b.r.compass.hardIron[1] = -45; b.r.compass.hardIron[2] = 20;
  u.hold(700);
  u.dump(dir, "09_disturbed");
  b.r.compass.hardIron[0] = b.r.compass.hardIron[1] = b.r.compass.hardIron[2] = 0;
  u.hold(8000);
  // no calibration stored (a fresh box: the first one keeps the calibration it loaded)
  static Box c;
  Run v(c);
  c.r.compass.gyro = true;
  c.r.compass.gyroFromMotion = true;
  c.r.thermo.set(21.4f, 48);
  c.host.start(Box::APP_COMPASS, v.now);
  v.psi = 300; v.pitch = 25; v.roll = 8;
  v.hold(3000);
  v.dump(dir, "10_uncalibrated");
  // no gyro: no attitude
  static Box d;
  Run w(d);
  d.r.compass.gyro = false;
  d.r.thermo.set(21.4f, 48);
  d.weather.clear();                              // no pressure yet: dashes
  d.host.start(Box::APP_COMPASS, w.now);
  w.hold(1500);
  w.dump(dir, "11_no_data");
  printf("compass3d shots written to %s\n", dir);
}

}  // namespace c3s
