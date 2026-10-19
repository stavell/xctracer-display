#pragma once
// Contact sheets of the start-up animation, rendered through the whole box (Box::boot with the intro): every 15th step
// of the 225, 8 frames to a row, with the map pack (the real 3D version) and without (the 2D version).
// Written as out/intro_sheet.pgm and out/intro_flat_sheet.pgm; build.sh converts them to PNG. Included by emu_main.cpp.
#include <stdio.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "box.h"

namespace is {

static bool loadPack(const char* path, std::vector<uint8_t>& bytes) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  bytes.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  const bool ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  fclose(f);
  return ok;
}

// Plays the animation from a power-on and returns the frames at steps 1, 15, 30 ... 225 (each 240 x 400 bytes, 0 / 255).
static std::vector<std::vector<uint8_t>> frames(Box& b, int* lastApp = nullptr) {
  std::vector<std::vector<uint8_t>> out;
  uint32_t now = 1000;
  b.boot(now, true);
  int seen = b.r.panel.presents, lastSteps = 0;
  for (int i = 0; i < 600 && b.host.active() == &b.introApp; i++) {
    now += 10;
    b.tick(now);
    if (!b.introApp.alive()) break;
    const int st = b.introApp->stepsDone();
    if (b.r.panel.presents != seen && st != lastSteps) {
      seen = b.r.panel.presents;
      lastSteps = st;
      if (st == 1 || st % 15 == 0) {
        std::vector<uint8_t> f(240 * 400);
        Bitmap1 bm = b.r.panel.bitmap();
        for (int y = 0; y < 400; y++)
          for (int x = 0; x < 240; x++) f[(size_t)y * 240 + x] = bm.getPixel(x, y) ? 255 : 0;
        out.push_back(f);
      }
    }
  }
  if (lastApp) *lastApp = b.host.activeIndex();
  return out;
}

static void sheet(const std::vector<std::vector<uint8_t>>& fr, const std::string& path, int cols = 8) {
  const int rows = ((int)fr.size() + cols - 1) / cols, cw = 244, ch = 404;
  const int W = cols * cw, H = rows * ch;
  std::vector<uint8_t> img((size_t)W * H, 128);
  for (size_t k = 0; k < fr.size(); k++) {
    const int ox = (int)(k % cols) * cw + 2, oy = (int)(k / cols) * ch + 2;
    for (int y = 0; y < 400; y++)
      for (int x = 0; x < 240; x++) img[(size_t)(oy + y) * W + ox + x] = fr[k][(size_t)y * 240 + x];
  }
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P5\n%d %d\n255\n", W, H);
  fwrite(img.data(), 1, img.size(), f);
  fclose(f);
}

static void shots(const char* dir, const char* packArg = nullptr) {
  // The study's Bulgaria pack when it is there (the real look: the preferred site is on it), else the Sopot fixture.
  const char* study = "../../maps/bulgaria.pack";
  const char* packPath = packArg ? packArg : (access(study, R_OK) == 0 ? study : "fixtures/sopot.pack");
  static std::vector<uint8_t> bytes;
  static terrain::MemReader rd;
  static TerrainKit kit;
  static Box b3;                                    // static: the arena apps are big
  static Box b2;
  if (loadPack(packPath, bytes)) {
    rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
    if (kit.open(&rd)) b3.setTerrain(&kit);
  }
  sheet(frames(b3), std::string(dir) + "/intro_sheet.pgm");
  printf("intro 3D sheet: pack %s %s\n", packPath, bytes.empty() ? "not found" : "loaded");
  sheet(frames(b2), std::string(dir) + "/intro_flat_sheet.pgm");
}

}  // namespace is
