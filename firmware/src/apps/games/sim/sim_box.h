#pragma once
// The flight sim as the box image builds it: SimApp plus the least terrain code it needs to fly over the map pack. The sim
// reads heights only to fill its own height cache (one cell at a time, bilinear on the 200 m layer), so a Terrain with a
// cache of two 2 kB tiles is enough (the vario's TerrainKit carries the map page's caches, 47 kB, which the sim never uses).
// Built in the shared app arena by main_box.cpp and emu/box.h alike; without a pack (reader null, or it is rejected) the app
// stays on its "needs the map pack" screen.
#include "apps/games/sim/accel_tilt.h"
#include "apps/games/sim/sim_app.h"
#include "platform/terrain/terrain.h"

namespace sim {

class SimBox : public SimApp {
 public:
  static constexpr int kTiles = 2;                  // a bilinear sample touches 1 to 4 tiles, mostly 1 or 2; a corner sample decodes two again (measured in sim_tests)

  explicit SimBox(terrain::IBlockReader* pack) {
    if (pack && terrain_.open(pack) == terrain::Terrain::OK) setTerrain(&terrain_);
  }
  const terrain::Terrain& terrain() const { return terrain_; }
  // The box without front buttons: tilt from the board's accelerometer, one-button menus (SimApp::setOneButton).
  void useAccel(ICompass& compass, const BoxSettings& settings) {
    accel_.attach(&compass, &settings);
    setTilt(&accel_);
    setOneButton(true);
  }

 private:
  AccelTilt accel_;
  terrain::TerrainWith<kTiles, false> terrain_;     // no relief scratch
};

}  // namespace sim
