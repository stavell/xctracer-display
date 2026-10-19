#pragma once
// The sim's local metres over the map pack: x east, y north of a takeoff (lat0, lon0).
#include <math.h>
#include "apps/games/sim/height_cache.h"
#include "platform/terrain/terrain.h"

namespace sim {

class TerrainSource : public IHeightSource {
 public:
  TerrainSource() {}
  TerrainSource(terrain::Terrain* t, double lat0, double lon0) { set(t, lat0, lon0); }
  void set(terrain::Terrain* t, double lat0, double lon0) {
    t_ = t; lat0_ = lat0; lon0_ = lon0;
    mPerLon_ = 111195.0 * cos(lat0 * 0.017453292519943295);
    lat0E7_ = (int32_t)llround(lat0 * 1e7);
    lon0E7_ = (int32_t)llround(lon0 * 1e7);
    latE7PerM_ = (float)(1e7 / 111195.0);
    lonE7PerM_ = (float)(1e7 / mPerLon_);
  }
  bool at(float x, float y, float* h) override {
    if (!t_ || !t_->isOpen()) return false;
    // Whole 1e-7 degrees from the takeoff's: no double maths per cell (the 16384 cells of a window were 95 us each on the device).
    return t_->elevationE7(lat0E7_ + (int32_t)lroundf(y * latE7PerM_), lon0E7_ + (int32_t)lroundf(x * lonE7PerM_), h);
  }
  uint32_t cost() const override { return t_ ? t_->tileDecodes() : 0; }
  double lat0() const { return lat0_; }
  double lon0() const { return lon0_; }
  terrain::Terrain* terrain() const { return t_; }

 private:
  terrain::Terrain* t_ = nullptr;
  double lat0_ = 0, lon0_ = 0, mPerLon_ = 1;
  int32_t lat0E7_ = 0, lon0E7_ = 0;
  float latE7PerM_ = 0, lonE7PerM_ = 0;
};

}  // namespace sim
