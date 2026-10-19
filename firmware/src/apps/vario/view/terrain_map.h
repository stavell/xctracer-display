#pragma once
// The map page's picture: hillshade + contours + main roads + towns of a map pack, drawn into a cached 1-bit
// layer, plus the ground track of the flight. Pure code (no hardware, no Adafruit); the screens blit the layer
// and draw the moving things (track, pilot arrow, north marker, numbers) over it every frame.
//
// Why a cache: the relief is the expensive part (every pixel is a bilinear height, a shade and four contour
// tests, and the tiles come from the QSPI), but the picture only changes when the pilot has moved or turned.
// update() therefore renders only when the cached window no longer fits the pilot (moved more than kMovePx,
// turned more than kTurnDeg), when the scale or size changed, or - if the picture is a few seconds old - when
// there is any visible change. In between, the pilot arrow and the track are drawn at their true position
// relative to the cached window (so the arrow wanders at most ~kMovePx from its home spot), and the arrow turns
// by the difference to the cached heading. Cost of the cache: one 1-bit layer, 10.2 kB.
//
// North up (headingDeg = NaN, the app's default) or heading up (headingDeg = the heading). North up needs a new
// layer only after the pilot moved ~10 px (about 1 km at 100 m/px: every minute or two in a glide, rarely in a
// thermal); heading up needs one per 5 degrees of turn as well (every second while circling). In heading-up the
// pilot sits kAhead of the height below the centre, so more of the map shows ahead of the glider.
//
//   static TerrainMap map;                               // ~25 kB: a global, never a local
//   TerrainMap::Params p; p.lat = ...; p.lon = ...; p.headingDeg = ...; p.mPerPx = 100; p.w = 218; p.h = 376;
//   map.update(terrain, vec, p, font, nowMs);         // font: Font1 of kCacheRotation, may be null (no labels)
//   map.cache()   // Bitmap1 (rotation 1: native 376 x 218, the panel's layout) to copy onto the screen
//   map.project(lat, lon, &x, &y);                    // where a position is in the cached window
#include <math.h>
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/terrain.h"

class TerrainMap {
 public:
  static constexpr int kMaxW = 218, kMaxH = 376;       // the map area of the 240 x 400 portrait screen
  static constexpr int kStride = (kMaxH + 7) / 8;      // cache rows are native rows: one logical column each
  static constexpr int kBand = 24;                     // rows of relief rendered at a time
  static constexpr int kTrackMax = 240;                // ground track points
  static constexpr uint32_t kTrackEveryMs = 5000;      // one point per 5 flight seconds: the last 20 minutes
  static constexpr float kAhead = 0.12f;               // heading up: the pilot is this fraction of the height below the centre
  static constexpr float kMovePx = 10.0f;              // re-render when the wanted window centre moved this far
  static constexpr float kTurnDeg = 5.0f;              // ... or the heading differs this much
  static constexpr uint32_t kRefreshMs = 4000;         // a picture this old follows even small changes ...
  static constexpr float kSmallMovePx = 2.0f, kSmallTurnDeg = 1.5f;   // ... of this size
  static constexpr int kCacheRotation = 1;             // matches the panel in portrait, so rows copy as bytes

  struct Params {
    double lat = 0, lon = 0;                            // the pilot
    float headingDeg = NAN;                             // NaN: north up, else the direction shown at the top
    float mPerPx = 100;
    int w = kMaxW, h = kMaxH;                           // map area
  };

  TerrainMap() { clearTrack(); }

  enum Result : uint8_t { KEPT, RENDERED };
  // Make the cached window fit p. noData() is set when the window has no terrain at all (outside the pack).
  Result update(terrain::Terrain& terr, terrain::MapVectors& vec, const Params& p, const Font1* font, uint32_t nowMs);
  void invalidate() { valid_ = false; }
  bool valid() const { return valid_; }
  bool noData() const { return noData_; }
  int dataPixels() const { return dataPx_; }
  float headingDeg() const { return hdg_; }              // the heading the cached window is turned to
  int width() const { return w_; }
  int height() const { return h_; }
  float mPerPx() const { return mPerPx_; }
  uint32_t renders() const { return renders_; }
  Bitmap1 cache() const {
    return Bitmap1{const_cast<uint8_t*>(cache_), (int16_t)h_, (int16_t)w_, (uint16_t)((h_ + 7) / 8), kCacheRotation};
  }

  // Pixel of a position in the cached window (continuous, may lie outside). False before the first render.
  bool project(double lat, double lon, float* x, float* y) const;
  bool projectE7(int32_t lat_e7, int32_t lon_e7, float* x, float* y) const;   // no double
  bool projectTrack(int i, float* x, float* y) const;                          // ground track point i, no double
  // Where the window is centred for p (the pilot shifted ahead).
  static void windowCentre(const Params& p, double mPerDeg, double* lat, double* lon);

  // Ground track, oldest first. feedTrack adds a point when kTrackEveryMs of flight clock passed since the last.
  void feedTrack(double lat, double lon, uint32_t flightClockMs);
  void clearTrack() { trackN_ = 0; trackHead_ = 0; haveTrack_ = false; }
  int trackCount() const { return trackN_; }
  bool trackAt(int i, double* lat, double* lon) const;

 private:
  void render(terrain::Terrain& terr, terrain::MapVectors& vec, const terrain::Window& win, const Font1* font);
  void drawRoads(terrain::MapVectors& vec, const terrain::Window& win, const Bitmap1& bm);
  void drawTowns(terrain::MapVectors& vec, const terrain::Window& win, const Bitmap1& bm, const Font1* font);

  uint8_t cache_[kStride * kMaxW];
  uint8_t grey_[kMaxW * kBand], cont_[kMaxW * kBand];
  terrain::RoadIter roads_;
  terrain::TownIter towns_;
  terrain::VecXform xf_;
  const terrain::MapVectors* vec_ = nullptr;
  double cLat_ = 0, cLon_ = 0;                          // the cached window's centre
  float hdg_ = 0, mPerPx_ = 0, pilotY_ = 0;
  int w_ = 0, h_ = 0, dataPx_ = 0;
  bool valid_ = false, noData_ = true;
  uint32_t renderedMs_ = 0, renders_ = 0;
  int32_t trackLat_[kTrackMax], trackLon_[kTrackMax];   // degrees * 1e7
  int trackN_ = 0, trackHead_ = 0;
  bool haveTrack_ = false;
  uint32_t trackMs_ = 0;
};
