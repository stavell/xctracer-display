#pragma once
#include "apps/vario/core/types.h"

// The best recent climb found in a wind-compensated trail: where its lift was centred, relative to
// the current position, so the pilot can centre it or fly back to it.
struct Thermal {
  bool valid = false;
  float x = 0, y = 0;        // metres east / north of the current position (air-mass frame)
  float avgClimb = 0;        // m/s over the episode
  float gain = 0;            // metres gained in the episode
  uint32_t ageS = 0;         // seconds since the episode ended
};

// tr = one point per second, oldest first. An episode is a run of points climbing at 0.5 m/s or more
// (gaps up to 6 s allowed) that lasts at least 8 s and gains at least 15 m. Of the episodes with at
// least 60% of the best gain, the most recent wins. The centre is the lift-weighted centroid.
bool findThermal(const AirPoint* tr, int n, Thermal& out);

// The memory of where thermals were: each is stored as a ground position plus the time it was valid and
// drifts with the wind afterwards, so it stays useful for minutes after you leave it.
struct ThermalMark {
  bool used = false;
  double lat = 0, lon = 0;
  float avgClimb = 0, gain = 0;
  uint32_t atMs = 0;        // flight clock when lat/lon were valid (they drift with the wind after that)
  uint32_t endMs = 0;       // flight clock when the climb ended
};

class ThermalMemory {
 public:
  static const int MAX = 6;
  static const uint32_t LIFETIME_MS = 15u * 60u * 1000u;
  void reset();
  void expire(uint32_t clockMs);
  // `found` is the best episode in the current trail, `lat/lon` the pilot's position now (the episode's
  // centre is relative to it). The same thermal re-detected within 50 m updates its entry.
  void update(double lat, double lon, const Thermal& found, uint32_t clockMs, double windE, double windN);
  // Marks relative to the pilot, newest first. Returns the count.
  int relative(ThermalRel* out, int maxN, double lat, double lon, uint32_t clockMs, double windE, double windN) const;
  int count() const;

 private:
  ThermalMark m_[MAX];
};
