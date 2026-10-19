#pragma once
// First-person terrain renderer in the style of Comanche's voxel space, for the portrait panel (240 x 400,
// rotation 1) and the HeightCache.
//
// Method: for each screen column a ray leaves the camera; the ray is walked front to back in steps that grow with
// distance (the same step table for every column). At each step the terrain height is looked up in the cache
// (bilinear for the near steps, nearest beyond kNearSteps), projected to a screen row, and if it rises above the
// highest row drawn so far in this column (the y-buffer) the span up to it is filled with the terrain's shade. What
// is left above the last span is sky. Roll is a vertical shear of the horizon across the columns (right bank: the
// horizon climbs to the right), pitch a vertical shift.
//
// Why it is fast on the panel: a screen column in portrait is one contiguous NATIVE row (rotation 1: native row = x,
// bit (399 - y) of the row). The 8 x 8 Bayer dither makes the pattern of one column the same byte at every byte of the
// row (400 rows = 50 bytes, period 8), so a span is a masked byte at each end and a memset between: the renderer writes
// whole bytes, never single pixels. Distance fog lightens the shade (toward white = the haze at the horizon).
#include <stdint.h>
#include "apps/games/sim/height_cache.h"
#include "platform/gfx/bitmap1.h"

namespace sim {

struct Camera {
  float x = 0, y = 0, z = 0;       // m
  float heading = 0;               // rad, 0 = north, clockwise
  float roll = 0;                  // rad, right wing down positive
  float pitch = 0;                 // rad, nose down positive
};

class VoxelView {
 public:
  static constexpr int kW = 240, kH = 400;
  static constexpr float kFovDeg = 70.0f;               // horizontal
  static constexpr int kHorizonY = 175;                 // screen row of the horizon at zero pitch and roll
  static constexpr float kNear = 25.0f;                 // first step distance, m
  static constexpr float kMaxDist = 9000.0f;
  static constexpr int kMaxSteps = 96;
  static constexpr int kBilinearSteps = 24;             // steps (from the camera) that interpolate (about the first 1.2 km)
  static constexpr int kColStep = 3;                    // screen columns per ray
  static constexpr int kYBins = 26;                     // depth buffer: bins of two steps (48 steps at present)

  struct Stats {                                        // work of the last render
    int columns = 0, rays = 0, steps = 0, samples = 0, bilinear = 0, spans = 0, bytes = 0, early = 0;
  };

  VoxelView();
  // Draw the scene into bm (rotation 1, native 400 x 240). Nothing is drawn for any other layout.
  void render(const Bitmap1& bm, const HeightCache& hc, const Camera& cam);
  const Stats& stats() const { return stats_; }
  int stepCount() const { return nSteps_; }
  float stepDistance(int i) const { return s_[i]; }
  // Depth buffer for things drawn after the terrain (clouds, thermal shafts): the highest screen row covered by terrain
  // NEARER than distance s along the view axis in screen column x (kH when none). Whatever is at distance s must not be
  // drawn at rows >= this value. Exact for the bin edges (every second step), conservative in between.
  int stepFor(float s) const { int i = 0; while (i < nSteps_ && s_[i] < s) i++; return i; }   // first step at or beyond s
  int nearerRow(int x, int step) const { int b = (step >> 1) - 1; if (b < 0) return kH; if (b >= kYBins) b = kYBins - 1; return yb_[x / kColStep][b]; }
  uint8_t pattern(int level, int xm) const { return pat_[level][xm]; }
  // Focal length in pixels.
  static float focal();

 private:
  void fillSpan(uint8_t* row, int y0, int y1, uint8_t pat) const;
  void sky(uint8_t* row, int x, int hy, int ymin);

  Stats stats_;
  int nSteps_ = 0;
  float s_[kMaxSteps];         // distance along the camera axis of each step
  float ds_[kMaxSteps];        // step length
  float fs_[kMaxSteps];        // focal / s: projects a height difference to pixels
  uint8_t fog_[kMaxSteps];     // 0..16
  float ka_[kMaxSteps], kb_[kMaxSteps];   // grey level = shade * ka + kb
  struct Span { int16_t y0, y1; uint8_t lvl; };
  Span spans_[kMaxSteps + 1];  // the spans of the ray being drawn
  int16_t yb_[kW / kColStep][kYBins];   // highest terrain row after steps 0..2b+1 of each ray
  uint8_t pat_[17][8];         // dither byte per level and column phase
};

}  // namespace sim
