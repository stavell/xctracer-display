#pragma once
#include "apps/vario/core/types.h"

// Wind and temperature by altitude: 100 m bands from 0 to 6000 m, each holding a smoothed wind vector
// and a smoothed temperature. Fed with the wind estimate and the thermometer while flying, so a climb
// through different air layers fills the table. A band can have either one without the other. Pure,
// fixed storage, no allocation.
struct ProfileBand {
  float altM;        // centre of the band
  float speedKmh;    // NaN when the band has no (fresh) wind
  float toDeg;       // direction the wind blows TOWARD, 0 = north; NaN without wind
  uint32_t ageS;     // seconds since the wind was last updated (the temperature's age without wind)
  int samples;       // wind samples
  float tempC;       // NaN when the band has no (fresh) temperature
  int tempSamples;
};

class WindProfile {
 public:
  static const int BANDS = 60;
  static const int BAND_M = 100;
  static const uint32_t EXPIRY_MS = 60u * 60u * 1000u;   // a band older than this is stale air

  void reset();
  // Altitudes below 0 go to band 0, above the top to the top band. Non-finite input is ignored.
  void add(float altM, float windEastMs, float windNorthMs, uint32_t clockMs);
  // Temperature in the band at this altitude; non-finite input is ignored.
  void addTemp(float altM, float tempC, uint32_t clockMs);
  // Bands with wind or temperature updated within the last hour, lowest first. Returns how many were written (<= maxN).
  int query(ProfileBand* out, int maxN, uint32_t nowMs) const;
  static int bandOf(float altM);

 private:
  struct Band {
    float e = 0, n = 0;
    uint16_t count = 0;
    uint32_t last = 0;
    float t = 0;
    uint16_t tCount = 0;
    uint32_t tLast = 0;
  };
  Band b_[BANDS];
};

// Average lapse rate in degC per 100 m: least-squares slope of temperature against altitude over the
// bands that have a temperature. NaN with fewer than 3 such bands or less than 200 m between the
// lowest and the highest.
float lapseRate(const ProfileBand* b, int n);
// True when band i has a temperature that is more than 0.2 degC above the band 100 m below it
// (that band must be in the list and have a temperature): warmer air above cooler air.
bool inversionAt(const ProfileBand* b, int n, int i);
