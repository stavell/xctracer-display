#pragma once
// Render of the example app "Level": out/example_level.pgm (and .png from build.sh). Included by emu_main.cpp.
#include <string>
#include "level_rig.h"

namespace levelshots {

static void shots(const char* dir) {
  static LevelRig t;
  t.tilt(7, 4);
  t.run(1500);
  t.r.panel.dump((std::string(dir) + "/example_level.pgm").c_str());
}

}  // namespace levelshots
