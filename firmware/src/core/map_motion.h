#pragma once
#include <stdint.h>
#include "core/types.h"

// Continuous motion for the thermal map between tracer samples (about 3 per second).
//
// The map is drawn as of a display time that trails the newest sample by one sample interval (plus a
// small jitter margin). The view centre is interpolated linearly between the two samples around that
// time, so it never shows a position that was not measured, and holds at the newest sample when the
// data stops. Pure maths, no clock of its own: times are the caller's arrival times (wall milliseconds).
// Flight time still comes from the data: the air-mass drift between two samples uses the measured
// ratio of data time to wall time (1 live, 3 in an `igc_replay --speed 3`).
//
// Frame: metres east/north of the trail origin, the sample that started the newest data second (the
// newest point FlightModel::airTrail returns), in the air-mass frame (wind drift removed).
struct MapView {
  bool valid = false;
  float cx = 0, cy = 0;       // view centre (the pilot) relative to the trail origin, air-mass frame
  float ax = 0, ay = 0;       // the same point in a frame fixed since reset(): compare frames with this, cx/cy jump with the origin
  float hdgRad = NAN;         // direction of travel on the same interpolated path, NaN if unknown
  int drop = 0;               // trail points newer than the display time (draw the line up to the centre)
};

class MapMotion {
 public:
  static constexpr int N = 24;                       // ring of recent samples (8 s live)
  static constexpr uint32_t GAP_MS = 2000;           // a longer pause between samples snaps
  static constexpr double TELEPORT_M = 150.0;        // so does a jump this far between two samples
  static constexpr uint32_t MARGIN_MS = 50;          // delay = sample interval + margin (arrival jitter)
  static constexpr uint32_t HDG_WINDOW_MS = 3000;    // data time the heading looks back over
  static constexpr float HDG_MIN_M = 3.0f;           // shorter than this: use the GPS course

  void reset();
  // One tracer sample. `newSecond`: FlightModel::feed() started a new data second with it (it is then the
  // trail origin); `flightClockMs` is FlightModel::clockMs() after the feed.
  void feed(uint32_t arrMs, const Sample& s, bool newSecond, uint32_t flightClockMs);
  // The view as of wall time nowMs; windE/windN in m/s (the wind FlightModel::airTrail uses, 0 if none).
  // Advances the display time by the time since the previous call, so calling it twice with the same
  // nowMs gives the same answer. maxDrop caps view.drop.
  MapView view(uint32_t nowMs, float windE, float windN, int maxDrop);

  uint32_t delayMs() const;                  // current display delay
  float dataRate() const { return rate_; }   // data seconds per wall second

 private:
  struct P { uint32_t t; double lat, lon; float course; };
  void ground(uint32_t t, double& lat, double& lon, float* course) const;   // interpolated
  void clearRing();

  P ring_[N];
  int n_ = 0;                      // oldest first
  bool originValid_ = false;
  P origin_ = {0, 0, 0, NAN};
  double kx_ = 111320.0;           // metres per degree of longitude at the origin
  bool haveRef_ = false;
  P ref_ = {0, 0, 0, NAN};         // first sample since reset: the fixed frame of MapView::ax/ay
  double kxRef_ = 111320.0;
  uint32_t interval_ = 333;
  bool tdValid_ = false;
  uint32_t td_ = 0, lastNow_ = 0;
  bool haveNow_ = false;
  // data clock at the last seconds boundaries, for the data/wall time ratio
  struct A { uint32_t arr, clock; };
  A anchors_[8];
  int na_ = 0;
  float rate_ = 1.0f;
};
