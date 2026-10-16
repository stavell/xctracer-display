#pragma once
#include <stdint.h>
#include "platform/util/types.h"

// The climb column: the last minutes of circling as a stack of circles in the air-mass frame, to show how the
// lift leans and where it is strong. Pure maths, no clock of its own: times are the flight clock the caller
// passes in. Static buffers only (about 19 kB); nothing here belongs on the 4 KB loop stack.
//
// Per data second the caller pushes one point (ground position, altitude, climb).
// analyze() then
//   1. moves every point into the air-mass frame (wind drift removed, as FlightModel::airTrail does),
//   2. finds the circling run(s) of the latest climb (turn rate, cumulative heading change),
//   3. cuts them into 360 degree circles and gives each a centre, mean climb, best-lift sector and a core
//      point (lift-weighted centroid) at the circle's mean altitude,
//   4. fits the core line through the newest circles' core points (lean = slope against altitude),
//   5. averages the climb by altitude band and judges whether the lift is building, steady or dying.
// Without circling in the last two minutes valid stays false and the page prints "--". docs/MATH.md section 6b.

struct CoreCircle {
  float x = 0, y = 0, z = 0;      // core point: metres east / north of the newest point (air frame), altitude (m)
  float cx = 0, cy = 0;           // plain centre of the circle (same frame)
  float climb = 0;                // mean vario over the circle, m/s
  float bestClimb = 0;            // strongest 3-point-smoothed vario on the circle, m/s
  float bestDeg = 0;              // bearing from the circle centre to that point, degrees clockwise from north
  float bestX = 0, bestY = 0;     // that point
  int a = 0, b = 0;               // first and last point of the circle, numbered like ThermalColumn::px(i)
  int8_t turn = 1;                // +1 clockwise, -1 counter-clockwise
};

struct ClimbBand {
  float z0 = 0;                   // lowest altitude of the band (multiple of the band height)
  float climb = 0;                // mean vario of the points in it, m/s
  int n = 0;                      // points (seconds) in it
};

// Where the lift is, for centring: the lift-weighted core of the newest full turn (the two before it only give the
// spread), and the circle centre of the newest turn. All in the air
// frame, metres east / north of the newest point. Only while circling, from the first full turn on.
struct CoreCue {
  bool valid = false;
  float x = 0, y = 0;              // best estimate of the core now
  float cx = 0, cy = 0;            // centre of the newest turn
  float shiftX = 0, shiftY = 0;    // core - centre: move the circle this far
  float shiftM = 0;                // its length, metres
  float shiftDeg = 0;              // its bearing, degrees clockwise from north
  float spread = 0;                // how far the older turns' estimates are from the newest one's (rms, m)
  float rawX = 0, rawY = 0;        // lift-weighted centroid of the newest turn alone (no gain, no lean)
  float turnS = 0;                 // duration of the newest turn, seconds
  float meanClimb = 0;             // mean vario over the newest turn
  float bestClimb = 0;             // strongest smoothed vario on it
  float bestX = 0, bestY = 0;      // where that was
  int turns = 0;                   // turns used (1..3)
  int a0 = 0;                      // first point of the newest turn (numbered like ThermalColumn::px)
  int a1 = 0, a2 = 0;              // first points of the turns before it (a1 = a0 if there is none)
};

enum ColumnTrend : uint8_t { TREND_NONE, TREND_BUILDING, TREND_STEADY, TREND_WEAKENING, TREND_TOP };

class ThermalColumn {
 public:
  static const int MAXP = 600;          // ten minutes of track
  static const int MAXC = 32;           // circles kept (the newest)
  static const int MAXB = 24;           // altitude bands
  static const int LEAN_CIRCLES = 10;   // the lean is fitted through the newest this many core points
  // Cue tuning (static so the evaluation tool can sweep them): the lift-weighted centroid of a circle sits only part
  // of the way to the core, so it is pushed away from the circle centre by cueGain; cueLean extrapolates older
  // turns along the lean; cueW are the weights of the newest, second and third turn.
  static float cueGain;
  static bool cueLean;
  static float cueW[3];
  static int cueMinTurns;       // full turns before a cue is shown
  static float cueMaxM;         // a shift longer than this is not believed (no cue)
  static const int MAX_AGE_S = 120;     // a climb that ended longer ago than this is not shown

  void reset();
  // One point per data second. Points without a fix or an altitude are dropped.
  void push(uint32_t clockMs, bool fix, double lat, double lon, float alt, float vario);
  // Analyse with the wind of now (the air-frame drift of every point). Cheap to call again with no new point.
  void analyze(float windE, float windN);
  // The newest sample, up to a second newer than the newest point (the tracer sends about 3 per second).
  void setHead(bool valid, double lat, double lon, float alt, float vario);

  // ---- results of analyze()
  bool valid = false;            // a climb with at least one full circle that ended at most MAX_AGE_S ago
  bool circling = false;         // the climb is going on (its last circle is the newest data)
  uint32_t ageS = 0;             // seconds since the climb ended (0 while circling)
  int nCircles = 0;
  CoreCircle circles[MAXC];      // oldest first
  int nBands = 0;
  ClimbBand bands[MAXB];         // lowest first
  float bandM = 50;              // band height, metres
  bool leanValid = false;        // at least 3 circles over at least 30 m
  float leanDeg = 0;             // direction the core moves in as it rises, degrees clockwise from north
  float leanPer100 = 0;          // metres of drift per 100 m of height
  float leanRms = 0;             // rms distance of the core points from the line, metres
  float coreX0 = 0, coreY0 = 0, coreZ0 = 0;     // the core line: (x, y) at altitude z0 ...
  float coreSx = 0, coreSy = 0;                 // ... and its slope in metres per metre of height
  CoreCue cue;
  ColumnTrend trend = TREND_NONE;
  float trendRatio = 0;          // climb of the newest two circles over the best earlier pair
  float zMin = 0, zMax = 0;      // altitude span of the climb's points

  // ---- the climb's track, oldest first: the points from the first circle to the newest, in the air frame
  int count() const { return nP_; }
  float px(int i) const { return ax_[p0_ + i]; }
  float py(int i) const { return ay_[p0_ + i]; }
  float pz(int i) const { return pts_[p0_ + i].z; }
  float pv(int i) const { return pts_[p0_ + i].v; }
  bool headValid() const { return headOk_; }
  float headX() const { return hx_; }
  float headY() const { return hy_; }
  float headZ() const { return hz_; }
  float headV() const { return hv_; }
  // Point (numbered like px(i)) where the last circling run ended: the track after it is the way out of the climb.
  int endPoint() const { return endPt_; }
  float newestGx() const { return n_ ? pts_[n_ - 1].gx : 0; }       // ground position of the newest point (column origin)
  float newestGy() const { return n_ ? pts_[n_ - 1].gy : 0; }
  uint32_t newestMs() const { return n_ ? pts_[n_ - 1].tMs : 0; }
  uint32_t revision() const { return rev_; }       // counts pushed points: a view redraws when it changes
  int pushed() const { return n_; }                // points held

 private:
  struct Pt {
    float gx, gy;      // ground metres east / north of the column origin
    float z, v;        // altitude, climb
    float hdg;         // ground heading of the step from the previous point, NaN if unknown
    float rate;        // turn rate at this point, deg/s, NaN if unknown
    uint32_t tMs;
  };
  void rebase(double dx, double dy);
  void clearResults();
  void circleMetrics(CoreCircle& c) const;
  void computeCue(int sg, int lowest);

  Pt pts_[MAXP];
  int n_ = 0;
  float ax_[MAXP], ay_[MAXP];      // air-frame positions relative to the newest point
  bool haveOrigin_ = false;
  double lat0_ = 0, lon0_ = 0, kx_ = 111320.0;
  uint32_t rev_ = 0, analyzedRev_ = 0xffffffffu;
  float analyzedWE_ = 0, analyzedWN_ = 0;
  int p0_ = 0, nP_ = 0, endPt_ = 0;            // the climb's points: [p0_, p0_ + nP_) of pts_
  bool headOk_ = false;
  float hx_ = 0, hy_ = 0, hz_ = 0, hv_ = 0;
  double headLat_ = 0, headLon_ = 0;
  float headAlt_ = 0, headVario_ = 0;
  bool headSet_ = false;
  void placeHead();
};
