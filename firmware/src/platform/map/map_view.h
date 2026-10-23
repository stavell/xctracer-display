#pragma once
// A browsable map of the pack: hillshade + contours from a cache of rendered tiles, roads and towns drawn live on top, panned
// and zoomed freely, north up or turned to any heading. Pure code (no hardware); used by the Map app (apps/map).
//
// Projection: "global pixels" of the current scale, the pack's vector units (PackInfo: 10 m on the fixed cos0 projection)
// divided by mPerPx / unitM: x east, y south of the pack's origin. Every tile's relief window uses that projection
// (Window::cosLat = cos0), so tiles rendered one at a time join exactly and roads (in vector units) land on the relief.
//
// Tiles: 64 x 64 pixels of the north-up picture, rendered on demand (nearest the screen first, then a margin ahead
// for panning) into a pool of kPool slots of 1 kB each: contours as 1-bit ink, hillshade as 4-bit grey at half resolution
// (32 x 32). The grey is dithered at draw time: north up on the ground's pixel grid (the picture does not crawl while it
// pans), turned on the screen's grid (a turned dither pattern would beat against itself). A tile costs about 1/30 of the
// whole screen, so a pan only waits for the strip it uncovers; step() does a few ms of the current tile per call.
// A zoom drops all tiles (they are of another scale).
//
//   static MapView view;                    // ~55 kB: a global or an arena member, never a local
//   view.attach(&terrain, &vec);            // vec may have no towns / roads
//   view.setArea(240, 368);                 // the map's rectangle on the screen; the view centre is its middle
//   view.setScale(100); view.centreOn(lat, lon); view.setHeading(0);
//   view.step(8000);                        // every loop pass: true when a tile finished (redraw)
//   view.draw(bm, 0, 32, font, reserved, n); // tiles + roads + towns into the area at (0, 32)
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"
#include "platform/map/map_draw.h"
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/terrain.h"

namespace mapview {

class MapView {
 public:
  static constexpr int kTile = 64;
  static constexpr int kPool = 52;                     // slots: the screen needs about 40 at worst (turned 45 degrees)
  static constexpr int kPrefetchPx = 64;               // tiles this far outside the screen are rendered when nothing nearer waits
  static constexpr int kRows = 8;                      // tile rows per work unit
  static constexpr int kUnitsNoClock = 6;              // work units per step() without a clock (tests, the PC)
  static constexpr int kTab = 16;                      // tiles across the bounding box of what is shown or wanted, at most

  MapView() { reset(); }
  void attach(terrain::Terrain* t, const terrain::MapVectors* v) { terr_ = t; vec_ = v; reset(); }
  void reset();                                        // forget all tiles
  void setArea(int w, int h) { aw_ = w; ah_ = h; }
  void setScale(float mPerPx);                         // drops the tiles when the scale changes; keeps the centre's ground point
  void setHeading(float deg);                          // direction shown at the top; 0 = north up
  void centreOn(double lat, double lon);
  void setCentre(float gx, float gy) { cx_ = gx; cy_ = gy; }
  // Move the view by (dx, dy) screen pixels (right, down), whatever the heading.
  void pan(float dx, float dy);
  // Snap the centre to the pixel grid (north up: the picture then moves in whole pixels and the dither stays on the ground).
  void snap();

  float scale() const { return m_; }
  float heading() const { return hdg_; }
  float centreX() const { return cx_; }
  float centreY() const { return cy_; }
  void centreLatLon(double* lat, double* lon) const { toLatLon(cx_, cy_, lat, lon); }
  bool toGlobal(double lat, double lon, float* gx, float* gy) const;
  void toLatLon(float gx, float gy, double* lat, double* lon) const;
  // Global pixel -> pixel of the map area (continuous), and back.
  void toScreen(float gx, float gy, float* sx, float* sy) const;
  void toGlobalFromScreen(float sx, float sy, float* gx, float* gy) const;

  // Render tiles for about budgetUs (with a clock) or kUnitsNoClock units. True when a tile on the screen was finished.
  bool step(uint32_t budgetUs);
  static void setClock(uint32_t (*micros)()) { clock_ = micros; }
  int missing() const;                                 // tiles the screen shows that are not rendered yet
  bool hasData() const { return terr_ && terr_->isOpen(); }
  uint32_t tilesRendered() const { return tiles_; }

  // The picture into the w x h area (setArea) at (ax, ay) of a white canvas: tiles, then roads and towns. Names stay clear
  // of the reserved rectangles (area pixels). font: the town names (null: none).
  void draw(const Bitmap1& bm, int ax, int ay, const Font1* font, const mapdraw::Rect* reserved = nullptr, int nReserved = 0);
  // The tiles only (the picture under the vectors), for tests.
  void drawTiles(const Bitmap1& bm, int ax, int ay);

 private:
  enum State : uint8_t { EMPTY, BUILDING, READY };
  struct Slot {
    int32_t tx, ty;
    uint16_t gen;
    uint8_t state;
    uint8_t ink[kTile * kTile / 8];                    // 1 = black (contour, no-data checker), LSB = leftmost
    uint8_t grey[(kTile / 2) * (kTile / 4)];           // 32 x 32 nibbles, low nibble = the even column; 15 = white
  };
  struct Box { int tx0, ty0, tw, th; };
  float tileEdge(int tx, int ty) const;                // how far the tile lies outside the screen (px, 0 = it shows)
  Box boxFor(float marginPx) const;
  void table(const Box& b, const Slot** tab) const;
  bool pickTile(int* tx, int* ty, float* e) const;
  int pickSlot(float e) const;
  void startTile(int slot, int tx, int ty);
  bool unit();                                         // one work unit; true when a tile on the screen was finished
  terrain::Window tileWindow(int tx, int ty, int px, float mPerPx) const;
  terrain::Window viewWindow() const;

  terrain::Terrain* terr_ = nullptr;
  const terrain::MapVectors* vec_ = nullptr;
  int aw_ = 240, ah_ = 368;
  float m_ = 100, hdg_ = 0, ct_ = 1, sn_ = 0;
  float cx_ = 0, cy_ = 0;
  uint16_t gen_ = 1;
  Slot slot_[kPool];
  mutable const Slot* tab_[kTab * kTab];             // tile lookup over the bounding box (draw, pick)
  int job_ = -1, phase_ = 0, jy_ = 0;
  uint32_t tiles_ = 0;
  uint8_t greyRows_[(kTile / 2) * kRows], contRows_[kTile * kRows];
  terrain::RoadIter roads_;
  terrain::TownIter towns_;
  mapdraw::Labels labels_;
  static uint32_t (*clock_)();
};

}  // namespace mapview
