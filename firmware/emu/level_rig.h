#pragma once
// The example app (apps/examples/level) in a bare AppHost with the system sheet over mock ports: what a new app's test starts from.
// Shared by level_tests.h and level_shots.h.
#include "apps/examples/level/level_app.h"
#include "mocks.h"
#include "platform/ui/system_sheet.h"

struct LevelRig {
  Rig r;
  examples::LevelApp app{r.battery, r.compass, r.settings};
  SystemSheet sheet;
  AppHost host{r.panel, r.input};
  uint32_t now = 0;
  LevelRig() {
    host.add(app);
    host.setSystemSheet(&r.power, &sheet);
    host.start(0, now);
  }
  void run(uint32_t ms) {                       // the main loop: one host iteration every 1 ms (game mode), as the device does
    for (uint32_t e = now + ms; now < e; now++) { r.compass.nowMs = now; host.tick(now); }
  }
  void tilt(double rollDeg, double pitchDeg) { r.compass.roll = rollDeg; r.compass.pitch = pitchDeg; }
};
