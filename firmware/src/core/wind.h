#pragma once
#include "core/types.h"

// Wind from a least-squares circle fit to ground-velocity vectors (Kasa fit): while turning at
// constant airspeed the vectors lie on a circle whose radius is the airspeed and whose centre
// is the wind. Accepts only believable fits (enough turn, airspeed 14-72 km/h, small residual).
struct WindFit {
  bool ok = false;
  double east = 0, north = 0;       // wind vector, m/s (the direction it blows toward)
};
WindFit fitWind(const TrackPoint* pts, int n);   // pts = the most recent n one-second points
