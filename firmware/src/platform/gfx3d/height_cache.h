#pragma once
// HeightCache: a square window of the terrain around the glider, held as plain arrays so that physics, the
// top-down view and the 3D renderer read heights without touching the map pack (tile decodes cost far more
// than a frame can spare).
//
//   128 x 128 cells of 200 m (the pack's own resolution), int16 metres + a precomputed shade 0..16 per cell
//   (a lambert level from a fixed sun, so lit slopes face the same way whatever the camera does): 48 kB.
//   Cell (ix, iy) is centred on (ix * 200 m, iy * 200 m) of the sim's local frame (x east, y north).
//   The array is toroidal: a cell lives at slot (ix & 127, iy & 127), so moving the window loads only the cells
//   that came into view. The cells known to be valid always form a rectangle that is grown one row or column at
//   a time inside the wanted window (setCentre), a bounded number of cells per refresh() call, so a frame never
//   pays for more than a few hundred elevation lookups.
//
//   A cell the source cannot give (outside the pack) reads as 0 m: this is a game, nothing is flown on it.
#include <stdint.h>
#include "platform/gfx3d/sim_math.h"

namespace gfx3d {

// Terrain elevation in the sim's local metres. Implemented over terrain::Terrain on the box and by analytic
// surfaces in the tests.
struct IHeightSource {
  virtual ~IHeightSource() {}
  virtual bool at(float x, float y, float* h) = 0;
  // A running count of the expensive things at() did (tile decodes of the map pack). refresh() stops after the call that
  // advanced it, so one frame never pays for a run of them. Analytic surfaces have no such cost.
  virtual uint32_t cost() const { return 0; }
};

class HeightCache {
 public:
  static constexpr int N = 128;                  // power of two
  static constexpr int kMask = N - 1;
  static constexpr float kCell = 200.0f;
  static constexpr float kInv = 1.0f / kCell;
  static constexpr float kExag = 1.6f;           // vertical exaggeration of the shade

  void attach(IHeightSource* src) { src_ = src; invalidateAll(); }
  void invalidateAll();
  // Where the window should be (cell index of the middle). Cheap; call every step.
  void setCentre(float x, float y);
  // Load at most budget cells toward the wanted window; returns the number of cells loaded.
  int refresh(int budget);                       // (times itself when a clock is set)
  int refreshWork(int budget);
  // Centre on (x, y) and load the whole window now (level start, tests). Returns cells loaded.
  int fill(float x, float y);
  int fillNear(float x, float y, int r);         // just the 2r x 2r cells around (x, y), then warmStep() loads the rest of the window
  // After fillNear: load the rest of that window in tile-friendly block order, `budget` cells per call (the valid rectangle stays the
  // near one meanwhile; refresh() must not run), then give it the whole window and shade it a few rows per call. Returns cells done.
  int warmStep(int budget);
  bool warming() const { return warming_ || shading_; }
  bool complete() const;

  // Bilinear height (m) at a local position; false outside the valid rectangle.
  bool height(float x, float y, float* h) const;
  // Bilinear shade level 0..16 (fixed point /16 -> returned as 0..256 = level * 16); false outside.
  bool shade16(float x, float y, int* s16) const;
  // Slope of the surface: (dh/dx, dh/dy) from central differences d metres apart. False outside.
  bool gradient(float x, float y, float d, float* gx, float* gy) const;

  // Direct cell access (the renderers). The index range [x0(), x1()) x [y0(), y1()) is valid.
  int x0() const { return vx0_; }
  int x1() const { return vx1_; }
  int y0() const { return vy0_; }
  int y1() const { return vy1_; }
  int16_t cellH(int ix, int iy) const { return h_[slot(ix, iy)]; }
  uint8_t cellShade(int ix, int iy) const { return s_[slot(ix, iy)]; }
  static int slot(int ix, int iy) { return ((iy & kMask) << 7) | (ix & kMask); }
  const int16_t* heights() const { return h_; }
  const uint8_t* shades() const { return s_; }
  uint32_t loaded() const { return loaded_; }     // cells loaded since attach
  // Optional profiling (status line / bench): with a clock (microseconds) set, refresh() times itself.
  typedef uint32_t (*ClockUs)();
  void setClock(ClockUs c) { clock_ = c; }
  struct RefreshStats { uint32_t calls = 0, sumUs = 0, maxUs = 0, cells = 0; };
  RefreshStats takeRefreshStats() { RefreshStats r = rs_; rs_ = RefreshStats(); return r; }

 private:
  enum Kind : uint8_t { NONE, COL, ROW };
  void loadCell(int ix, int iy);
  void loadRect(int x0, int y0, int x1, int y1);
  void shadeCell(int ix, int iy);
  void shadeLine(Kind k, int idx);
  bool inside(int ix, int iy) const { return ix >= vx0_ && ix < vx1_ && iy >= vy0_ && iy < vy1_; }

  IHeightSource* src_ = nullptr;
  int16_t h_[N * N];
  uint8_t s_[N * N];
  int vx0_ = 0, vx1_ = 0, vy0_ = 0, vy1_ = 0;    // valid rectangle, half open, in cell indices
  int tx0_ = 0, ty0_ = 0;                        // wanted window (N x N)
  bool haveTarget_ = false;
  bool warming_ = false, shading_ = false;
  int wx0_ = 0, wy0_ = 0, wpos_ = 0, shadeRow_ = 0;   // warm-up: the window it fills, the next cell in block order, the next row to shade
  Kind curKind_ = NONE;
  int curIdx_ = 0, curPos_ = 0;
  uint32_t loaded_ = 0;
  ClockUs clock_ = nullptr;
  RefreshStats rs_;
};

}  // namespace gfx3d
