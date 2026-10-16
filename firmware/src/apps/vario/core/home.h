#pragma once
#include "apps/vario/core/types.h"

// Way back to takeoff: distance, bearing, required glide and the height left on arrival.
struct HomePoint {
  bool valid = false;
  bool provisional = false;    // first fix before takeoff; replaced by the real takeoff position
  double lat = 0, lon = 0;
  float alt = NAN;
};

// Typical paraglider at trim: about 36 km/h through the air, sinking about 1.1 m/s. Used for the
// arrival estimate; the real glider and loading differ, so the figure is an estimate only.
static const float HOME_AIRSPEED_MS = 10.0f;
static const float HOME_SINK_MS = 1.1f;

struct HomeInfo {
  bool valid = false;
  float distM = NAN;
  float bearingDeg = NAN;      // true bearing to home, 0..360
  float relBearingDeg = NAN;   // relative to the heading, -180..180 (NaN without a heading)
  float glideReq = NAN;        // distance / height above home; NaN when not above home
  float arrivalM = NAN;        // height above home on arrival, wind and sink included
  bool reachable = false;      // false when the wind leaves no progress toward home
};

// All inputs may be NaN; nothing is derived from what is missing. `wind` is used when valid.
HomeInfo computeHome(double lat, double lon, float altM, float headingDeg, const HomePoint& home,
                     const Wind& wind, float airspeedMs = HOME_AIRSPEED_MS, float sinkMs = HOME_SINK_MS);
