#pragma once
// What the 3D view shows of the air: cumulus clouds above mature thermals (always, they are the pilot's real cue) and, with the
// assist on, the thermals themselves (translucent shafts and/or rising bubbles). Drawn after the terrain, with the voxel
// renderer's depth buffer, so a cloud or a shaft behind a ridge is hidden by it.
//
// Clouds: one per thermal, over the column's top at the cloud base, drifting downwind of it (Air::columns). Size grows with the
// thermal's strength and its cloud factor (none before the thermal matures, none after it decayed). A cumulus is a union of
// discs on a flat base, drawn column by column (two or four screen columns per evaluation): black outline (thicker where the
// edge is steep), a light dithered body in three bands, a darker flat base. Whole bytes are written, like the terrain.
//
// Thermals (assist): style COLUMNS = a shaft from the trigger up to the cloud base, following the column's lean and drift, as
// XOR dots (translucent: they invert whatever is behind, sky or hillside), denser for stronger thermals and at the edges.
// BUBBLES = small rings that rise through the column at a speed proportional to the thermal's strength (a visual speed, not the
// real climb, which would not be seen to move), more of them and nearer the core for stronger thermals. BOTH = a faint shaft
// plus the bubbles. The thermal the glider is in always gets a few larger bubbles around the glider's altitude.
//
// Budget: the cost is estimated from the geometry before drawing (cycles on the M4F); over the budget the detail drops (coarser
// columns, fewer segments and bubbles), then the farthest elements are left out. Deterministic (hash of the thermal id).
#include "apps/games/sim/air.h"
#include "apps/games/sim/glider.h"
#include "apps/games/sim/voxel.h"
#include "platform/gfx/bitmap1.h"

namespace sim {

class SkyFx {
 public:
  enum class Style : uint8_t { COLUMNS, BUBBLES, BOTH };
  static constexpr int kStyles = 3;
  static const char* styleName(Style s) { return s == Style::COLUMNS ? "columns" : (s == Style::BUBBLES ? "bubbles" : "both"); }
  static constexpr int kMaxCols = 32, kMaxClouds = 4, kMaxShafts = 3, kMaxBubbles = 28;
  static constexpr float kCloudBudget = 64000.0f;      // cycles (1.0 ms at 64 MHz)
  static constexpr float kAssistBudget = 96000.0f;     // cycles (1.5 ms)
  static constexpr float kViewRange = 9000.0f;
  static constexpr float kRiseVisual = 8.0f;           // bubbles rise at 8 x the thermal's peak strength m per sim second

  struct Stats {
    int found = 0, clouds = 0, cloudCols = 0, shafts = 0, shaftCols = 0, bubbles = 0, level = 0;
    float cloudCycles = 0, assistCycles = 0;           // estimated from the work done, M4F cycles
  };

  // Draw into bm after VoxelView::render() of the same camera. t = sim time. Clouds always; assist adds the thermals.
  void draw(const Bitmap1& bm, const VoxelView& vv, const Air& air, const Glider& g, const Camera& cam, float t, bool assist,
            Style style, bool clouds = true);
  const Stats& stats() const { return st_; }
  // Tests: draw every cloud in view, ignoring the cap of four and the budget (to look at one particular cloud).
  void setUnlimited(bool u) { unlimited_ = u; }
  // The cumulus of the last draw: centre screen x, base row, width px (for the tests); n <= kMaxClouds.
  struct CloudShot { uint32_t id; float sx, sy, wpx, dist; bool hidden; };
  int cloudShots(CloudShot* out, int cap) const;

 private:
  struct Pj;
  void drawClouds(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, int n, float cb);
  void drawCloud(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, const ColumnInfo& c, float sx, float sy, float s, int cs);
  void drawShaft(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, const Air& air, const ColumnInfo& c, float t, float cb, int nseg,
                 int cs, int level);
  void drawBubbles(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, const Air& air, const ColumnInfo& c, float t, float cb, int n,
                   const Glider* core);

  Stats st_;
  ColumnInfo col_[kMaxCols];
  CloudShot shots_[kMaxCols];
  bool unlimited_ = false;
  int nShots_ = 0;
  int16_t top_[VoxelView::kW / 2 + 2];
};

}  // namespace sim
