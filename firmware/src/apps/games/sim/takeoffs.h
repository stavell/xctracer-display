#pragma once
// Takeoffs of the flight simulator: a few well-known Bulgarian paragliding sites. The coordinates and heights are
// APPROXIMATE (from memory and corrected against the terrain model, good to a few hundred metres, not surveyed): the sim starts the glider a few metres
// above the terrain model at that spot whatever the table says, and a launch that lands in a gully is a
// reason to correct the table. windFrom is a typical flying wind for the site (the way the slope faces).
#include <stdint.h>

namespace sim {

struct Takeoff {
  const char* name;
  double lat, lon;          // degrees, approximate
  float altNominal;         // m, approximate; the terrain model decides
  float windFromDeg;        // flying wind direction (where it blows from)
};

constexpr Takeoff kTakeoffs[] = {
    {"Sopot", 42.6920, 24.7531, 1390, 190},           // south slope of the Stara planina above Sopot (the town itself is at 42.65 N, 530 m)
    {"Kalofer", 42.6650, 24.9310, 1280, 90},          // Balkan foothills west of Kalofer, east-facing
    {"Vratsa", 43.1710, 23.5500, 980, 350},           // Vrachanski Balkan above Vratsa, north-facing
    {"Sliven", 42.7350, 26.3000, 850, 180},           // Sliven Balkan north of the town, south-facing
    {"Bansko", 41.8080, 23.4800, 1190, 10},           // under Pirin above Bansko, north-facing
    // The north-east plateau sites: the launch is the steepest convex plateau-edge cell the terrain model has within 3 km of
    // the rough position (slope >= 0.15 at 400 m, 200 m or more of drop within 1.5 km downhill, the plateau behind it), the
    // wind the way that edge faces. Approximate like the others.
    {"Shumen", 43.2529, 26.9272, 460, 120},           // Shumen plateau edge above the town, south-east-facing
    {"Madara", 43.2709, 27.1188, 405, 305},           // Madara plateau cliffs near the Rider, north-west-facing
    {"Nevsha", 43.2256, 27.4744, 330, 325},           // plateau edge east of Nevsha (Vetrino), north-west-facing
};
constexpr int kTakeoffCount = sizeof kTakeoffs / sizeof kTakeoffs[0];

}  // namespace sim
