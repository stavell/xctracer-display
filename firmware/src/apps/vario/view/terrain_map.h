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
// by the difference to the cached heading. Cost of the cache: two 1-bit layers (shown and being built), 20.4 kB.
//
// Never in one go: a full picture costs ~1.9 s on the device, far more than a loop pass may take. update() only decides
// and starts a job; step() does a few milliseconds of it per loop pass (rows of one relief strip, then one road polyline,
// then one town at a time) into the second layer while the first one keeps being shown, and swaps them when the job is
// done. The new picture is centred where the pilot was when the job started: at 100 m/px even 100 km/h is 0.3 px/s,
// so the pilot is a few px off centre when it appears, far inside the margin of kMovePx..window edge. The sun
// (Params::sunAz/sunEl) shades the relief; a change of more than kSunDeg starts a job too (every 20 minutes or so).
//
// North up (headingDeg = NaN, the app's default) or heading up (headingDeg = the heading). North up needs a new
// layer only after the pilot moved ~10 px (about 1 km at 100 m/px: every minute or two in a glide, rarely in a
// thermal); heading up needs one per 5 degrees of turn as well (every second while circling). In heading-up the
// pilot sits kAhead of the height below the centre, so more of the map shows ahead of the glider.
//
//   static TerrainMap map;                               // ~25 kB: a global, never a local
//   TerrainMap::Params p; p.lat = ...; p.lon = ...; p.headingDeg = ...; p.mPerPx = 100; p.w = 218; p.h = 376;
//   map.update(terrain, vec, p, font, nowMs);         // decides, may start a job; font: Font1 of kCacheRotation, may be null
//   map.step(terrain, vec, font, 8000);               // every loop pass: up to ~8 ms of the running job
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
  static constexpr int kRows = 1;                      // rows of one relief strip rendered per work unit (a strip is <= 64 px wide)
  static constexpr int kUnitsNoClock = 40;             // work units per step() when no clock is set
  static constexpr float kLostPx = 95.0f;              // the pilot is this far from the picture's centre: it no longer shows him
  static constexpr float kSunDeg = 5.0f;               // re-shade when the sun moved this far (az, el)
  static constexpr float kSunMinEl = 2.0f;             // the sun lower than this: the fixed light (night, twilight)
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
    float sunAz = NAN, sunEl = NAN;                     // the sun (degrees); NaN or low: the fixed light from the upper left
  };

  TerrainMap() { clearTrack(); }

  enum Result : uint8_t { KEPT, STARTED, RENDERED };
  // Decide whether the cached window still fits p; if not, start a job (STARTED; also while one runs for these
  // params) - the picture itself does not change until step() has finished it. A picture whose window the pilot
  // has left far behind is dropped at once (valid() false: the screen says so instead of showing a wrong map).
  Result update(terrain::Terrain& terr, terrain::MapVectors& vec, const Params& p, const Font1* font, uint32_t nowMs);
  // Work on the running job for about budgetUs (with a clock set; else kUnitsNoClock units). True when the job
  // finished in this call and its picture is now the shown one. noData() is set when the window has no terrain at all.
  bool step(terrain::Terrain& terr, terrain::MapVectors& vec, const Font1* font, uint32_t budgetUs);
  // update() and run the job to its end: KEPT or RENDERED. For tests, screen renders and the PC.
  Result updateSync(terrain::Terrain& terr, terrain::MapVectors& vec, const Params& p, const Font1* font, uint32_t nowMs);
  // A microsecond clock for the step budget (the device's cycle counter); none: fixed work units.
  static void setClock(uint32_t (*micros)()) { clock_ = micros; }
  bool building() const { return job_ != IDLE; }
  uint32_t steps() const { return steps_; }                // step() calls that did work
  uint32_t maxStepUs() const { return maxStepUs_; }        // the longest of them (needs a clock); resets with resetStepStats()
  void resetStepStats() { steps_ = 0; maxStepUs_ = 0; }
  bool sunLit() const { return sunOn_; }                   // the shown picture is shaded by the sun
  float sunAz() const { return sunAz_; }
  float sunEl() const { return sunEl_; }
  void invalidate() { valid_ = false; job_ = IDLE; }
  bool valid() const { return valid_; }
  bool noData() const { return noData_; }
  int dataPixels() const { return dataPx_; }
  float headingDeg() const { return hdg_; }              // the heading the cached window is turned to
  int width() const { return w_; }
  int height() const { return h_; }
  float mPerPx() const { return mPerPx_; }
  uint32_t renders() const { return renders_; }
  Bitmap1 cache() const {
    return Bitmap1{const_cast<uint8_t*>(buf_[front_]), (int16_t)h_, (int16_t)w_, (uint16_t)((h_ + 7) / 8), kCacheRotation};
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
  enum Job : uint8_t { IDLE, RELIEF, ROADS, TOWNS };
  void startJob(const terrain::Window& win, const Params& q, uint32_t nowMs);
  bool unit(terrain::Terrain& terr, terrain::MapVectors& vec, const Font1* font);   // one piece of work; true when the job is done
  void finishJob();
  Bitmap1 buildBitmap() const {
    return Bitmap1{const_cast<uint8_t*>(buf_[1 - front_]), (int16_t)jh_, (int16_t)jw_, (uint16_t)((jh_ + 7) / 8), kCacheRotation};
  }
  void reliefUnit(terrain::Terrain& terr);
  bool roadUnit(terrain::MapVectors& vec);
  bool townUnit(terrain::MapVectors& vec, const Font1* font);

  uint8_t buf_[2][kStride * kMaxW];                      // the shown layer and the one being built
  int front_ = 0;
  uint8_t grey_[terrain::kMaxStrip * kRows], cont_[terrain::kMaxStrip * kRows];
  terrain::RoadIter roads_;
  terrain::TownIter towns_;
  terrain::VecXform xf_;                                // of the shown picture
  const terrain::MapVectors* vec_ = nullptr;
  double cLat_ = 0, cLon_ = 0;                          // the shown window's centre
  float hdg_ = 0, mPerPx_ = 0, pilotY_ = 0;
  int w_ = 0, h_ = 0, dataPx_ = 0;
  bool valid_ = false, noData_ = true;
  uint32_t renderedMs_ = 0, renders_ = 0;
  float sunAz_ = 0, sunEl_ = 0;                         // the sun of the shown picture
  bool sunOn_ = false;
  // The running job: the picture for window jwin_, built into the other layer.
  Job job_ = IDLE;
  terrain::Window jwin_;
  terrain::ReliefStyle jstyle_;
  terrain::VecXform jxf_;
  terrain::UnitBox jbox_;
  const terrain::MapVectors* jvec_ = nullptr;
  int jw_ = 0, jh_ = 0, jsw_ = 0, jxs_ = 0, jy_ = 0, jdata_ = 0, jpass_ = 0;
  bool jActive_ = false;                                // road / town iterator started
  float jhdg_ = 0, jmPerPx_ = 0, jpilotY_ = 0, jsunAz_ = 0, jsunEl_ = 0;
  bool jsunOn_ = false;
  uint32_t jStartMs_ = 0, steps_ = 0, maxStepUs_ = 0;
  static uint32_t (*clock_)();
  int32_t trackLat_[kTrackMax], trackLon_[kTrackMax];   // degrees * 1e7
  int trackN_ = 0, trackHead_ = 0;
  bool haveTrack_ = false;
  uint32_t trackMs_ = 0;
};
