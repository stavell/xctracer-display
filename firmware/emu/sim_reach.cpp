// Playability of the flight sim's air from every takeoff.
//   build.sh simreach [PACK] [seeds] [t0 ...]
// For each takeoff and seed: the thermals working at launch (strength >= 1.5 m/s), the nearest, how many a straight trim
// glide reaches (with >= 30 m left above the ground, and with 0.5 m/s extra sink), and the climb of the scripted pilot in 10 minutes.
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>
#include "sim_reach.h"
#include "apps/games/sim/terrain_source.h"
#include "platform/terrain/terrain.h"

static terrain::Terrain gTerrain;
static sim::World gWorld;
#ifdef OLD_AIR
#define HOUSES 0
#else
#define HOUSES gWorld.air().houseCount()
#endif

int main(int argc, char** argv) {
  const char* packPath = argc > 1 ? argv[1] : "fixtures/sopot.pack";
  const int seeds = argc > 2 ? atoi(argv[2]) : 12;
  std::vector<float> times;
  for (int i = 3; i < argc; i++) times.push_back((float)atof(argv[i]));
  if (times.empty()) times = {180.0f, 1800.0f, 3600.0f};
  std::vector<uint8_t> bytes;
  FILE* f = fopen(packPath, "rb");
  if (!f) { perror(packPath); return 2; }
  fseek(f, 0, SEEK_END); bytes.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) return 2;
  fclose(f);
  terrain::MemReader rd(bytes.data(), (uint32_t)bytes.size());
  if (gTerrain.open(&rd) != terrain::Terrain::OK) { fprintf(stderr, "pack rejected\n"); return 3; }
  const bool all = bytes.size() > 1000000;
  printf("pack %s, %d seeds, start times", packPath, seeds);
  for (float t : times) printf(" %.0f", t);
  printf(" s\n");
  printf("%-8s | working nearest(median) reach-trim reach+sink nearestReach | ap gain>=500 in 10 min (median gain, mean thermals used)\n", "site");
  int totalRuns = 0, totalOk = 0;
  for (int s = 0; s < sim::kTakeoffCount; s++) {
    if (!all && s != 0) continue;
    sim::TerrainSource src(&gTerrain, sim::kTakeoffs[s].lat, sim::kTakeoffs[s].lon);
    float sumW = 0, sumR = 0, sumM = 0;
    std::vector<float> nearest, nearestR, gains;
    int runs = 0, ok = 0, lessThanTwo = 0, thUsed = 0;
    std::string perTime;
    for (float t0 : times) {
      int okT = 0;
      for (int seed = 1; seed <= seeds; seed++) {
#ifdef OLD_AIR     // the air as it was before the playability work (same launch height and wind as now, table wind direction)
        gWorld.beginAt(&src, (uint32_t)seed, 0, 0, 150.0f, sim::kTakeoffs[s].windFromDeg, sim::kTakeoffs[s].windFromDeg, 5.0f, t0);
#else
        gWorld.begin(&src, (uint32_t)seed, sim::kTakeoffs[s], 0.0f, t0);
#endif
        const simreach::Reach r = simreach::analyse(gWorld);
        sumW += r.working; sumR += r.reachable; sumM += r.withMargin;
        if (r.withMargin < 2) lessThanTwo++;
        nearest.push_back(r.nearest);
        nearestR.push_back(r.nearestReach);
        const simreach::Flight fl = simreach::fly(gWorld, 600.0f);
        gains.push_back(fl.gain);
        if (getenv("VERBOSE"))
          printf("  %s t0=%.0f seed=%d: houses %d working %d reach %d/%d nearest %.0f gain %.0f m%s thermals %d cloudbase %.0f\n", sim::kTakeoffs[s].name, t0, seed,
                 HOUSES, r.working, r.reachable, r.withMargin, r.nearest, fl.gain, fl.landed ? " LANDED" : "", fl.thermals, gWorld.air().cloudBase());
        thUsed += fl.thermals;
        runs++;
        if (fl.gain >= 500.0f) { ok++; okT++; }
      }
      perTime += " t0=" + std::to_string((int)t0) + ":" + std::to_string(okT) + "/" + std::to_string(seeds);
    }
    auto med = [](std::vector<float> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    printf("%-8s | %5.1f %9.0f m %9.1f %9.1f %10.0f m | %3d/%d  median %4.0f m, %.1f thermals; <2 reachable with margin: %d\n", sim::kTakeoffs[s].name, sumW / runs,
           med(nearest), sumR / runs, sumM / runs, med(nearestR), ok, runs, med(gains), (double)thUsed / runs, lessThanTwo);
    printf("           by start time:%s\n", perTime.c_str());
    fflush(stdout);
    totalRuns += runs; totalOk += ok;
  }
  printf("TOTAL gain>=500 m in 10 min: %d/%d = %.0f %%\n", totalOk, totalRuns, 100.0 * totalOk / totalRuns);
  return 0;
}
