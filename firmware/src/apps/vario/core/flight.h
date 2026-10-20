#pragma once
#include "apps/vario/core/home.h"
#include "apps/vario/core/thermal.h"
#include "apps/vario/core/thermal3d.h"
#include "apps/vario/core/windprofile.h"
#include "apps/vario/core/types.h"
#include "apps/vario/core/windekf.h"

// Everything derived from the stream of samples: 1 Hz history, averages, glide ratio, takeoff /
// landing, circling and wind. Pure and deterministic: time comes from the samples' own time stamps
// (so a replay at any speed behaves like real flight time), never from a wall clock.
class FlightModel {
 public:
  static const int HIST = 180;     // seconds of dense track kept (3 min); thermals are remembered separately
  static const int AVG_S = 20;     // vario averaging window
  static const int WIND_S = 30;    // seconds of track used for each wind fit
  static constexpr double DISAGREE_MS = 3.0;   // EKF vs circle fit (m/s) that counts as disagreement
  static const int DISAGREE_N = 8;             // accepted fits in a row before the EKF is restarted from the fit

  void reset();
  bool feed(const Sample& s);
  void feedTemperature(float tempC);   // latest thermometer reading, stamped with the flight clock      // true when a new data second started

  bool hasData() const { return have_; }
  const Sample& last() const { return cur_; }
  float avgVario() const;          // m/s over AVG_S seconds
  float glide() const;             // NAN unless sinking with a GPS fix
  bool flying() const { return flying_; }
  uint32_t flightSec() const;
  bool circling() const { return circling_; }
  Wind wind() const;               // EKF estimate when confident, else the circle fit; ageS = time since the
                                   // estimate was last informed by a turn (EKF) or a circle (fit)
  Wind circleWind() const;         // circle-fit estimate only (diagnostics)
  bool ekfConfident() const { return ekf_.confident(); }
  float ekfSigmaW() const { return ekf_.sigmaW(); }
  const WindEkf& ekf() const { return ekf_; }
  // Last maxN seconds of track in the air-mass frame (wind-compensated when a wind estimate exists).
  int airTrail(AirPoint* out, int maxN) const;
  // Remembered thermals relative to the current position, newest first (positions drift with the wind).
  int thermals(ThermalRel* out, int maxN) const;
  // The climb column (thermal3d.h): the last minutes of circling. column() analyses with the wind of now and the
  // newest sample as the head of the track; call it only when a page needs it.
  const ThermalColumn& column();
  // Wind by altitude, filled from fresh wind estimates while flying.
  const WindProfile& profile() const { return profile_; }
  // Takeoff position (provisional first fix until takeoff is detected, then the spot where speed first passed the threshold; kept for the rest of the session).
  const HomePoint& home() const { return home_; }
  uint32_t clockMs() const { return clock_; }
  int histCount() const { return n_; }
  const TrackPoint& hist(int i) const { return h_[i]; }

 private:
  void tick();
  void updateWind();
  static constexpr uint32_t WIND_FIT_FRESH_MS = 30000;   // a circle fit this young beats the EKF
  bool windSource(double& wx, double& wy, uint32_t& ageS) const;
  bool windXY(double& wx, double& wy) const;   // the one wind vector wind(), airTrail and thermals use
  void updateProfile();
  void updateTemperature();
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
  int disagree_ = 0;               // accepted fits in a row that contradict a confident EKF
  uint32_t ekfInformedAt_ = 0;     // flight clock of the last update made while the heading was changing
  bool turnedRecently() const;
  WindProfile profile_;
  float tempC_ = NAN;
  uint32_t tempAt_ = 0;
  bool haveTemp_ = false;
  HomePoint home_;
  bool homeFixed_ = false;         // takeoff seen: home is final
  bool candValid_ = false;         // position where speed first crossed the takeoff threshold
  double candLat_ = 0, candLon_ = 0;
  float candAlt_ = NAN;
  ThermalMemory mem_;
  ThermalColumn col_;
  AirPoint scratch_[HIST];         // working space for the thermal finder (too big for the stack)
};
