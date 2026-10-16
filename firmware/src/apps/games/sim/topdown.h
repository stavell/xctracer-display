#pragma once
// Top-down, heading-up map of the terrain around the glider, drawn from the HeightCache (not the map pack:
// the pack's relief renderer takes 0.2-0.4 s per window on the device, far over a 20 ms frame).
//
// The picture is a 1-bit CACHE (240 x 376, one native row per screen column, like the vario's map page) that is
// re-rendered, a few thousand pixels per frame, into a back buffer while the front buffer keeps being shown; the
// buffers swap when the back one is finished. A render starts when the shown one is stale (the glider moved more
// than 12 px or the heading differs by more than 7 degrees from the one the picture was made for). Between
// renders the picture is shifted so the glider stays where it is on the screen (a byte-and-shift copy of native
// rows: 240 rows x 47 bytes), and the glider symbol turns by the difference between its heading and the map's.
// All moving things (glider, trail, thermals) are drawn over the picture every frame by the caller through
// worldToScreen().
//
// Pixels: hillshade from the cache's precomputed shade (bilinear), contour lines every 200 m (index line every 1000 m,
// drawn 2 px wide) from the interpolated height. North is up in the picture when the heading is 0.
#include <stdint.h>
#include "apps/games/sim/height_cache.h"
#include "platform/gfx/bitmap1.h"

namespace sim {

class TopDownView {
 public:
  static constexpr int kW = 240, kH = 376, kStride = 47;
  static constexpr int kTopY = 24;                       // screen row of the cache's first row
  static constexpr int kGliderX = 120, kGliderY = 236;   // glider position inside the map area
  static constexpr float kStalePx = 12.0f, kStaleDeg = 7.0f;

  TopDownView() { reset(); }
  void reset();
  // Advance the picture: start a render when the shown one is stale, spend up to budgetPx pixels on a running
  // one. Returns the pixels rendered. (x, y) is the glider in local metres, heading in radians.
  int update(const HeightCache& hc, float x, float y, float heading, float mPerPx, int budgetPx);
  // Render a complete picture right away (tests, the first frame).
  void renderNow(const HeightCache& hc, float x, float y, float heading, float mPerPx);

  bool ready() const { return shownValid_; }
  bool rendering() const { return busy_; }
  float mapHeading() const { return shown_.heading; }
  float mPerPx() const { return shown_.m; }
  uint32_t renders() const { return renders_; }
  uint32_t pixelsRendered() const { return pixels_; }

  // Copy the picture to the screen with the glider at (kGliderX, kTopY + kGliderY); (gx, gy) is where the glider is now.
  void blit(const Bitmap1& screen, float gx, float gy);
  // World position -> screen pixel with the shown picture's orientation (valid after blit()).
  void worldToScreen(float wx, float wy, float* sx, float* sy) const;
  // Pixel (0 black .. 1 white) of the shown picture, for tests.
  int shownPixel(int px, int py) const;

 private:
  struct Params { float x, y, heading, m; };
  void renderColumn(const HeightCache& hc, const Params& p, uint8_t* buf, int px);
  uint8_t* back() { return buf_[1 - front_]; }
  uint8_t* frontBuf() { return buf_[front_]; }

  uint8_t buf_[2][kStride * kW];
  int front_ = 0;
  Params shown_ = {}, making_ = {};
  bool shownValid_ = false, busy_ = false;
  int col_ = 0;
  int ox_ = 0, oy_ = 0;                                  // screen offset of the shown picture's origin
  int16_t prevBand_[kH];
  int16_t band_[kH];
  uint8_t pix_[kH];
  uint32_t renders_ = 0, pixels_ = 0;
};

}  // namespace sim
