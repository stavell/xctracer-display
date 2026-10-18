#pragma once
// Everything the vario needs for terrain, in one object that its owner (main_<image>.cpp, or the PC player)
// declares as a static: about 65 kB (tile cache 20 kB + the map page's cache and bands 24 kB + vector reader
// buffers 3 kB + the ground track 2 kB). The App only holds a pointer, so an image without a pack pays nothing.
//
//   static TerrainKit kit;                        // a global, never a local
//   if (kit.open(&reader)) app.setTerrain(&kit);  // no pack, or a damaged one: the app shows no terrain at all
#include "apps/vario/view/terrain_map.h"
#include <stdio.h>
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/pack_status.h"
#include "platform/terrain/terrain.h"

struct TerrainKit : terrain::IPackStatus {
  terrain::FullTerrain terrain;
  terrain::MapVectors vec;
  TerrainMap map;
  bool ok = false;

  bool open(terrain::IBlockReader* reader) {
    ok = false;
    map.invalidate();
    map.clearTrack();
    if (terrain.open(reader) != terrain::Terrain::OK) return false;
    vec.open(reader, terrain.info());            // towns and roads are optional: a pack without them still gives AGL and relief
    ok = true;
    return true;
  }

  // IPackStatus: what the home screen and Settings > About show.
  bool packOk() const override { return ok; }
  uint32_t packBytes() const override { return ok ? terrain.info().fileSize : 0; }
  void packArea(char* dst, size_t cap) const override {
    if (!ok) { if (cap) dst[0] = 0; return; }
    const terrain::PackInfo& i = terrain.info();
    snprintf(dst, cap, "%.1f-%.1f N, %.1f-%.1f E", i.latS_e7 / 1e7, i.latN_e7 / 1e7, i.lonW_e7 / 1e7, i.lonE_e7 / 1e7);
  }
  bool packAttribution(int i, char* dst, size_t cap) const override { return ok && vec.attribution(i, dst, cap); }
};
