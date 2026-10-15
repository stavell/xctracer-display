#pragma once
#include "core/thermal.h"
#include "core/types.h"
#include "core/windekf.h"

// Everything derived from the stream of samples: 1 Hz history, averages, glide ratio, takeoff /
// landing, circling and wind. Pure and deterministic: time comes from the samples' own time stamps
// (so a replay at any speed behaves like real flight time), never from a wall clock.
class FlightModel {
 public:
  static const int HIST = 180;     // seconds of dense track kept (3 min); thermals are remembered separately
  static const int AVG_S = 20;     // vario averaging window
  static const int WIND_S = 30;    // seconds of track used for each wind fit

  void reset();
  bool feed(const Sample& s);      // true when a new data second started

  bool hasData() const { return have_; }
  const Sample& last() const { return cur_; }
  float avgVario() const;          // m/s over AVG_S seconds
  float glide() const;             // NAN unless sinking with a GPS fix
  bool flying() const { return flying_; }
  uint32_t flightSec() const;
  bool circling() const { return circling_; }
  Wind wind() const;               // EKF estimate when confident, else the circle fit (with its age)
  Wind circleWind() const;         // circle-fit estimate only (diagnostics)
  bool ekfConfident() const { return ekf_.confident(); }
  float ekfSigmaW() const { return ekf_.sigmaW(); }
  const WindEkf& ekf() const { return ekf_; }
  // Last maxN seconds of track in the air-mass frame (wind-compensated when a wind estimate exists).
  int airTrail(AirPoint* out, int maxN) const;
  // Remembered thermals relative to the current position, newest first (positions drift with the wind).
  int thermals(ThermalRel* out, int maxN) const;
  int histCount() const { return n_; }
  const TrackPoint& hist(int i) const { return h_[i]; }

 private:
  void tick();
  void updateWind();
  bool windXY(double& wx, double& wy) const;   // the one wind vector wind(), airTrail and thermals use
  void updateThermals();

  Sample cur_;
  bool have_ = false;
  TrackPoint h_[HIST];
  int n_ = 0;
  uint32_t clock_ = 1000;          // flight clock, ms, advanced by data time stamps
  int lastKey_ = -1;               // seconds of day of the last sample
  bool flying_ = false;
  uint32_t flightStart_ = 0, fastSince_ = 0, slowSince_ = 0;
  bool circling_ = false;
  uint32_t lastTurn_ = 0;
  bool windValid_ = false;
  double wx_ = 0, wy_ = 0;
  uint32_t windAt_ = 0;
  WindEkf ekf_;
  bool ekfStarted_ = false;
  uint32_t ekfAt_ = 0;
  ThermalMemory mem_;
  AirPoint scratch_[HIST];         // working space for the thermal finder (too big for the stack)
};
