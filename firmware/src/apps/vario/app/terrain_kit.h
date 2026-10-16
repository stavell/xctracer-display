#pragma once
// Everything the vario needs for terrain, in one object that its owner (main_<image>.cpp, or the PC player)
// declares as a static: about 65 kB (tile cache 20 kB + the map page's cache and bands 24 kB + vector reader
// buffers 3 kB + the ground track 2 kB). The App only holds a pointer, so an image without a pack pays nothing.
//
//   static TerrainKit kit;                        // a global, never a local
//   if (kit.open(&reader)) app.setTerrain(&kit);  // no pack, or a damaged one: the app shows no terrain at all
#include "apps/vario/view/terrain_map.h"
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/terrain.h"

struct TerrainKit {
  terrain::Terrain terrain;
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
};
