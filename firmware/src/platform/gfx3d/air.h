#pragma once
// The air: wind, thermals, ridge lift, lee sink, general sink. Pure float maths, deterministic from a seed.
//
// Frame: x east, y north (m), z altitude above sea level (m), t seconds of the sim day.
//
// Wind: a direction (where it blows FROM, degrees) and a speed at the start altitude that grows with height
// (+40 % per 1000 m, kept within 0.6 .. 2.2 of the base).
//
// Thermals: the world is cut into square cells of kTile m. Each cell may host a trigger point, chosen by the
// terrain: of 9 jittered candidates the one on the best sun-facing slope or ridge top wins (the sun is in the
// south). Over the day the cell starts thermals one after another; which cells do, when, how strong (1-5 m/s),
// how wide (50-150 m core radius) and how long (10-20 min) comes from hash(seed, cell, number), so a seed is a
// day and the same seed is the same day on every run. A thermal is a tilted column: the air rises at about 0.8 of
// its strength, so the column leans with the wind (the centre at height z sits wind * dz / w downwind of the
// trigger) and the whole column slides downwind slowly as it lives. Life cycle: build up over a quarter of the
// life, mature, decay over the last third. Weak near the ground, fading out under the cloud base (a seeded
// 2000-2600 m). Gaussian core with a ring of weaker sink around it.
//
// Ridge lift: the horizontal wind runs into the slope; the air has to climb it. Vertical speed = wind component
// into the slope (wind . gradient, i.e. wind speed * slope) at the terrain upwind of the glider, times a decay
// with height above the ground (lift only reaches a few hundred metres). Behind a ridge (the glider over a slope
// that falls away in the wind direction) the same product turns negative: lee sink, with a longer reach.
//
// General sink: 0.5 - 1.0 m/s (seeded) between thermals, reduced inside a thermal's core.
#include <stdint.h>
#include "platform/gfx3d/height_cache.h"
#include "platform/gfx3d/sim_math.h"

namespace gfx3d {

struct AirSample {
  float wx = 0, wy = 0;          // wind, m/s (east, north)
  float wz = 0;                  // total vertical air speed, m/s up
  float thermal = 0;             // thermal part of wz
  float ridge = 0;               // ridge lift (+) / lee sink (-) part
  float base = 0;                // general sink part (negative)
  float gx = 0, gy = 0;          // horizontal gradient of the thermal part, (m/s) per m: the wing tips feel it
};

struct ThermalInfo {             // for the assist overlay and the tests
  float x, y;                    // centre at the given height
  float radius;                  // core radius at that height
  float strength;                // m/s now (life cycle applied, not height)
  uint32_t id;
};

// A thermal as the 3D view draws it: the column from the trigger on the ground up to the cloud base and the cumulus on top.
struct ColumnInfo {
  uint32_t id;
  float x0, y0, z0;              // trigger point on the ground
  float S;                       // peak strength, m/s (sets the bubbles' rise speed)
  float R0;                      // core radius at the trigger
  float strength;                // m/s now (peak x life cycle)
  float t0, life;                // start and length, s of the sim day
  float cloud;                   // 0..1 how developed the cumulus is: 0 before the thermal matures and after it has decayed
  float cx, cy;                  // column centre at the cloud base
  float cloudX, cloudY;          // cumulus centre (drifted downwind of the column's top)
  float width;                   // cumulus width, m
};

class Air {
 public:
  static constexpr float kTile = 1800.0f;
  static constexpr int kSlots = 64;               // cache of the cells' trigger points (probe window of 4)
  static constexpr int kHouses = 3;
  static constexpr int kMaxInfo = 32;
  static constexpr int kMaxActive = 48;

  explicit Air(uint32_t seed = 1) { setSeed(seed); }
  void setSeed(uint32_t seed);
  uint32_t seed() const { return seed_; }
  void setField(const HeightCache* f) { field_ = f; nHouse_ = 0; clearTiles(); }
  // Wind from fromDeg (meteorological), speed m/s at refAlt (m).
  void setWind(float fromDeg, float speed, float refAlt);
  // "How thermic the day is": 0 weak, 1 medium (the tuned good day), 2 strong, 3 epic. Scales the core strength, the share of
  // trigger cells that fire, the cloud base and the general sink together (more lift up means more sink in between).
  // Call before setSeed() / setHome(): they use it.
  static constexpr int kLevels = 4;
  void setLevel(int l) { level_ = l < 0 ? 0 : (l >= kLevels ? kLevels - 1 : l); nHouse_ = 0; clearTiles(); }
  int level() const { return level_; }
  static const char* levelName(int l) { static const char* n[kLevels] = {"weak", "medium", "strong", "epic"}; return n[l < 0 ? 0 : (l >= kLevels ? kLevels - 1 : l)]; }
  float cloudBase() const { return cloudBase_; }
  float baseSink() const { return baseSink_; }

  void sample(float x, float y, float z, float t, AirSample* out) const;
  void wind(float z, float* wx, float* wy) const;

  // Thermals active at time t whose columns come within `range` m of (x, y) at altitude z.
  int nearby(float x, float y, float z, float t, float range, ThermalInfo* out, int cap) const;

  // Thermals alive at time t whose column top (at the cloud base) lies within `range` m of (x, y), with their cumulus
  // (see ColumnInfo). Cumulus life cycle: none until a fifth of the thermal's life has passed, full at two fifths, thinning
  // from 65 % of the life, gone at 95 %. It forms over the column's top and drifts downwind of it as it lives.
  int columns(float x, float y, float t, float range, ColumnInfo* out, int cap) const;
  // Centre of the thermal's column at altitude z (tilt and drift as in the air model).
  void columnPoint(const ColumnInfo& c, float z, float t, float* x, float* y) const;
  static float coreRadius(const ColumnInfo& c, float z) { return c.R0 * (1.0f + 0.6f * fmaxf(z - c.z0, 0.0f) / 1000.0f); }

  // The takeoff's "house thermals": three reliable trigger spots (the best sun-facing spurs below or beside the launch,
  // 300-1100 m away) that are always working, with overlapping lives, like the known trigger spots of a real site.
  void setHome(float x, float y);
  int houseCount() const { return nHouse_; }
  bool house(int i, float* x, float* y) const { if (i < 0 || i >= nHouse_) return false; *x = house_[i].x; *y = house_[i].y; return true; }

  // Quality of a trigger point 0..1 for the given spot (sun-facing slope + ridge top); -1 where the terrain is not loaded.
  float triggerScore(float x, float y) const;

 private:
  struct Th {
    float x0, y0, z0, t0, life, S, R;
    uint32_t id;
  };
  struct Tile {
    int tx = 0x7fffffff, ty = 0;
    bool valid = false;
    float x = 0, y = 0, z = 0, score = 0;       // best trigger candidate in the cell
    bool house = false;                         // a house thermal: always fires, never weak
    uint32_t tk = 0;                            // the cell's hash key (its thermals' randomness)
    float period = 0, offset = 0;               // timing(tk), kept: hashing them again for every query was most of the air model's cost
    // The cell's thermals numbered nBase-3 .. nBase (the ones that can be alive), computed once per period instead of per query
    // (the hashes and a pow per candidate were 90 % of an air sample on the device). Bit i of mask: number nBase-3+i fires.
    int nBase = 0x7fffffff;
    uint8_t mask = 0;
    Th th[4];
  };
  struct Active {
    Th th;
    float cx, cy;                // column centre at the query height
    float env;                   // life cycle 0..1
    float R;                     // core radius at the query height
  };
  void clearTiles() { for (int i = 0; i < kSlots; i++) tiles_[i].valid = false; }
  const Tile* tile(int tx, int ty) const;
  int collect(float x, float y, float z, float t, float range, Active* out, int cap) const;
  bool thermalOf(const Tile& tl, uint32_t tk, int number, Th* out) const;
  void fillThermals(Tile& tl, int nBase) const;
  void timing(uint32_t tk, bool house, float* period, float* offset) const;
  float envelope(const Th& th, float t) const;
  static float cloudFactor(const Th& th, float t);
  void columnCentre(const Th& th, float z, float t, float* cx, float* cy) const;
  float ridgeLift(float x, float y, float z) const;

  uint32_t seed_ = 1;
  int level_ = 1;
  const HeightCache* field_ = nullptr;
  float windToX_ = 0, windToY_ = 1, windSpeed_ = 0, windRefAlt_ = 0;
  float cloudBase_ = 2400, baseSink_ = 0.7f;
  mutable Tile house_[kHouses];
  int nHouse_ = 0;
  mutable Tile tiles_[kSlots];
  mutable Active scratch_[kMaxActive];
};

}  // namespace gfx3d
