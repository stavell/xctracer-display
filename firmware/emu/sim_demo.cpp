// Flight sim scenario renderer and timing tool.
//   sim_demo PACK OUTDIR            writes OUTDIR/sim_*.pgm (build.sh converts them to PNG)
//   sim_demo PACK OUTDIR bench      timing of both views and work counts
#include <stdio.h>
#include <string.h>
#include <vector>
#include <chrono>
#include "sim_shots.h"

static terrain::FullTerrain gTerrain;

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: sim_demo PACK OUTDIR [bench]\n"); return 2; }
  std::vector<uint8_t> bytes;
  FILE* f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  fseek(f, 0, SEEK_END); bytes.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) return 2;
  fclose(f);
  static terrain::MemReader rd;
  rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
  if (gTerrain.open(&rd) != terrain::Terrain::OK) { fprintf(stderr, "pack rejected\n"); return 3; }
  if (argc > 3 && !strcmp(argv[3], "bench")) return 0;
  simshots::shots(argv[2], gTerrain);
  return 0;
}
