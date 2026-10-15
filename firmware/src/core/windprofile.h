#pragma once
#include "core/types.h"

// Wind by altitude: 100 m bands from 0 to 6000 m, each holding a smoothed wind vector. Fed with the
// wind estimate while flying, so a climb through different air layers fills the table. Pure, fixed
// storage, no allocation.
struct ProfileBand {
  float altM;        // centre of the band
  float speedKmh;
  float toDeg;       // direction the wind blows TOWARD, 0 = north
  uint32_t ageS;     // seconds since the band was last updated
  int samples;
};

class WindProfile {
 public:
  static const int BANDS = 60;
  static const int BAND_M = 100;
  static const uint32_t EXPIRY_MS = 60u * 60u * 1000u;   // a band older than this is stale air

  void reset();
  // Altitudes below 0 go to band 0, above the top to the top band. Non-finite input is ignored.
  void add(float altM, float windEastMs, float windNorthMs, uint32_t clockMs);
  // Bands updated within the last hour, lowest first. Returns how many were written (<= maxN).
  int query(ProfileBand* out, int maxN, uint32_t nowMs) const;
  static int bandOf(float altM);

 private:
  struct Band {
    float e = 0, n = 0;
    uint16_t count = 0;
    uint32_t last = 0;
  };
  Band b_[BANDS];
};
