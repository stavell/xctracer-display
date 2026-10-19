#pragma once
// Towns and main roads of the map pack: spatial queries without heap allocation.
//
// Coordinates are the pack's vector units (10 m) on the DEM projection: x east of 22 E, y south of 45 N,
// x = (lon - 22) * cos(42.7 deg) * 111195 / 10, so one unit is the same on both axes near the middle of
// Bulgaria. unitToLatLon()/latLonToUnit() convert; makeXform() builds the affine map from units to the
// pixels of a relief Window (using the true cos(latitude) of the window centre, like the relief does, so
// roads and shading register).
//
//   static MapVectors vec;                       // holds no big buffers itself
//   vec.open(&flash, terrain.info());
//   static RoadIter roads;                       // 2.7 kB: a global, never a local
//   roads.begin(vec, vec.boxForWindow(win));
//   while (const RoadPoly* p = roads.next()) { ... p->x[i], p->y[i], p->cls ... }
//   TownIter towns; Town t;                      // ~170 B + 256 B read chunk
//   towns.begin(vec, box);
//   while (towns.next(&t)) { ... t.latin, t.cyr, t.popClass ... }   // most populous first
//
// The section CRCs were checked by Terrain::open(); the decoders below still bound-check every read and drop
// a block that does not parse instead of drawing it.
#include <stddef.h>
#include <stdint.h>
#include "platform/terrain/block_reader.h"
#include "platform/terrain/pack_format.h"
#include "platform/terrain/terrain.h"

namespace terrain {

constexpr int kMaxRoadPts = 120;               // vertices per polyline record
constexpr uint32_t kMaxRoadBlockBytes = 2048;  // coded bytes of one 10.24 km block (largest in Bulgaria: 1055)
constexpr int kMaxLatin = 48, kMaxCyr = 96;    // name buffers including the NUL

enum RoadClass : uint8_t { ROAD_MOTORWAY = 0, ROAD_TRUNK = 1, ROAD_PRIMARY = 2, ROAD_SECONDARY = 3 };

struct UnitBox {                               // inclusive, vector units
  int32_t x0 = 0, y0 = 0, x1 = -1, y1 = -1;
  bool contains(int32_t x, int32_t y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
  bool intersects(const UnitBox& o) const { return x0 <= o.x1 && o.x0 <= x1 && y0 <= o.y1 && o.y0 <= y1; }
};

struct Town {
  int32_t x = 0, y = 0;
  uint8_t popClass = 0;                        // 0 unknown, 1 <5k, 2 <10k, 3 <20k, 4 <50k, 5 <100k, 6 <250k, 7 >=250k
  bool city = false;                           // place=city (else town)
  char latin[kMaxLatin] = {0};                 // transliterated name
  char cyr[kMaxCyr] = {0};                     // UTF-8 Cyrillic name
};

struct RoadPoly {
  uint8_t cls = 0;                             // RoadClass
  uint8_t n = 0;                               // vertices
  UnitBox bbox;
  uint16_t x[kMaxRoadPts], y[kMaxRoadPts];     // units
};

// Affine map from vector units to continuous pixel coordinates of a Window: px = a x + b y + c, py = d x + e y + f.
struct VecXform {
  float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
  float px(float x, float y) const { return a * x + b * y + c; }
  float py(float x, float y) const { return d * x + e * y + f; }
};

class MapVectors {
 public:
  bool open(IBlockReader* reader, const PackInfo& info);
  bool hasTowns() const { return townCount_ > 0; }
  bool hasRoads() const { return roadsOk_; }
  int townCount() const { return townCount_; }

  void unitToLatLon(double x, double y, double* lat, double* lon) const;
  void latLonToUnit(double lat, double lon, double* x, double* y) const;
  // Float version for per-point work (ground track): degrees * 1e7 in, units out, no double.
  void latLonToUnitE7(int32_t lat_e7, int32_t lon_e7, float* x, float* y) const;
  UnitBox boxFromLatLon(double latS, double latN, double lonW, double lonE) const;
  UnitBox boxForWindow(const Window& win) const;       // bounding box of the (rotated) window
  VecXform makeXform(const Window& win) const;

  // Attribution string i (0 Copernicus, 1 OpenStreetMap with the (c) sign in UTF-8, 2 the same in ASCII), copied
  // and NUL-terminated. False if there is no such string.
  bool attribution(int i, char* dst, size_t cap) const;

 private:
  friend class TownIter;
  friend class RoadIter;
  IBlockReader* rd_ = nullptr;
  PackInfo info_;
  Section towns_, roads_, strings_;
  uint16_t townCount_ = 0, townNames_ = 0;
  bool roadsOk_ = false;
  uint16_t blk_ = 0, bx0_ = 0, by0_ = 0, nbx_ = 0, nby_ = 0, maxPts_ = 0;
  uint32_t roadIdx_ = 0, roadData_ = 0;                // offsets inside the roads section
};

class TownIter {
 public:
  void begin(const MapVectors& v, const UnitBox& box);
  bool next(Town* out);                                // false when done
 private:
  const MapVectors* v_ = nullptr;
  UnitBox box_;
  int pos_ = 0, have_ = 0, base_ = 0;
  uint8_t chunk_[32 * 8];
};

class RoadIter {
 public:
  void begin(const MapVectors& v, const UnitBox& box);
  // Next polyline whose bounding box meets the query box (valid until the next call), or null.
  const RoadPoly* next();
 private:
  bool loadBlock();
  const MapVectors* v_ = nullptr;
  UnitBox box_;
  int bxA_ = 0, bxB_ = 0, byA_ = 0, byB_ = -1, bx_ = 0, by_ = 0, curBx_ = 0, curBy_ = 0;
  bool inBlock_ = false;
  uint32_t pos_ = 0, len_ = 0, left_ = 0;
  RoadPoly poly_;
  uint8_t buf_[kMaxRoadBlockBytes];
};

}  // namespace terrain
