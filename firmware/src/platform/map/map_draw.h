#pragma once
// Drawing of the map pack's vector layers, shared by the vario's map page (apps/vario/view/terrain_map) and the platform map
// view (platform/map/map_view): roads as black lines in white casings, towns as a black dot in a white ring with the Latin name
// in a white halo, placed clear of the names already drawn. Pure code; coordinates are pixels of the target bitmap.
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"
#include "platform/terrain/map_vectors.h"

namespace mapdraw {

void thickLine(const Bitmap1& bm, int x0, int y0, int x1, int y1, int w, DrawMode m);
// Skip a segment whose bounding box misses a w x h bitmap by a margin: the polylines come from 10 km blocks and
// mostly lie off the window, and line() clips pixel by pixel.
bool segVisible(int x0, int y0, int x1, int y1, int w, int h, int m);
// Float to int for pixel coordinates: a polyline vertex far outside the window must not overflow the int.
int toPx(float v);

// One pass of a road (motorway .. secondary; anything else is skipped): pass 0 the white casings, pass 1 the black cores.
// All casings go first, so crossings stay clean. (ox, oy) is added to every vertex (a clip window inside the bitmap).
void roadPoly(const Bitmap1& bm, const terrain::VecXform& xf, const terrain::RoadPoly& r, int pass, int w, int h, int ox = 0,
              int oy = 0);

struct Rect { int x0, y0, x1, y1; };

// The label boxes placed so far (plus reserved corners), so names never overlap.
class Labels {
 public:
  static const int kMax = 45;
  void clear() { n_ = 0; }
  void reserve(const Rect& r) { if (n_ < kMax) r_[n_++] = r; }
  bool full() const { return n_ >= kMax; }
  // Box for a name tw wide beside the dot at (x, y) in a window W wide: right of the dot, or left of it when there is no room.
  // False when it would leave the window or overlap a placed box; else the box is placed.
  bool place(int x, int y, int tw, int W, Rect* out);

 private:
  Rect r_[kMax];
  int n_ = 0;
};

void townDot(const Bitmap1& bm, int x, int y, bool city);
void haloText(const Bitmap1& bm, int x, int baseline, const char* s, const Font1& f);   // black text, 1 px white halo

}  // namespace mapdraw
