#pragma once
// Screen renders of the box: the launcher in its states, the device list with its "Apps..." row, the compass
// tool's menu. Included by emu_main.cpp; writes out/<name>.pgm like the other scenarios.
#include <string>
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
  b.host.start(Box::APP_LAUNCHER, now);
  run(300);
  dump(b, dir, "launcher");
  press(120);
  dump(b, dir, "launcher_tetris");
  press(120); press(120);                         // the "Start-up" row
  dump(b, dir, "launcher_startup");
  b.r.input.press(now + 10, 2500);                // held past 1.5 s: the footer warns about power-off
  run(1800);
  dump(b, dir, "launcher_held");
  run(1500);

  b.host.start(Box::APP_VARIO, now);              // the vario's device list with the box's extra row
  run(300);
  press(900);
  press(120); press(120);
  dump(b, dir, "list_apps");
  b.host.start(Box::APP_COMPASS, now);            // the compass tool's menu
  run(1000);
  press(900);
  dump(b, dir, "list_tool");
}

}  // namespace bs
