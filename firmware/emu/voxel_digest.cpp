// Regression oracle for the 3D renderer (gfx3d::VoxelView): renders a fixed set of cameras over the real map pack and either
// saves the pictures and depth buffers (--save FILE) or compares against a saved set (--cmp FILE), printing how many pixels and
// depth-buffer entries differ and the PC time per render. For speed work on the renderer: a change that claims to keep the
// picture must report 0 / 0 here.
//   ./build.sh voxdigest --save out/voxel_ref.bin     (before the change)
//   ./build.sh voxdigest --cmp out/voxel_ref.bin      (after)
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <chrono>
#include <vector>
#include "platform/gfx3d/voxel.h"
#include "platform/gfx3d/height_cache.h"
#include "platform/gfx3d/terrain_source.h"
#include "platform/gfx3d/takeoffs.h"
#include "platform/terrain/terrain.h"
#include "platform/terrain/block_reader.h"

using namespace gfx3d;

static terrain::FullTerrain gTerrain;
static HeightCache gCache;
static VoxelView gVoxel;

static void writePgm(const char* path, const uint8_t* fb) {   // portrait 240 x 400 as seen on the panel
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P5\n240 400\n255\n");
  for (int y = 0; y < 400; y++)
    for (int x = 0; x < 240; x++) {
      const int bit = 399 - y;
      fputc((fb[x * 50 + (bit >> 3)] >> (bit & 7)) & 1 ? 255 : 0, f);
    }
  fclose(f);
}

int main(int argc, char** argv) {
  const char* mode = argc > 2 ? argv[1] : "";
  const char* file = argc > 2 ? argv[2] : "";
  const char* packPath = argc > 3 ? argv[3] : "fixtures/sopot.pack";
  const bool save = !strcmp(mode, "--save"), cmp = !strcmp(mode, "--cmp");
  if (!save && !cmp) { fprintf(stderr, "usage: voxel_digest --save|--cmp FILE [PACK]\n"); return 2; }
  std::vector<uint8_t> bytes;
  FILE* f = fopen(packPath, "rb");
  if (!f) { perror(packPath); return 2; }
  fseek(f, 0, SEEK_END); bytes.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) return 2;
  fclose(f);
  terrain::MemReader rd(bytes.data(), (uint32_t)bytes.size());
  if (gTerrain.open(&rd) != terrain::Terrain::OK) { fprintf(stderr, "pack rejected\n"); return 3; }
  static TerrainSource src(&gTerrain, kTakeoffs[0].lat, kTakeoffs[0].lon);
  gCache.attach(&src);

  // Cameras: 4 cache centres x 6 headings x 4 heights above ground x 3 attitudes = 288 renders.
  struct Cam { Camera c; };
  std::vector<Camera> cams;
  const float centres[4][2] = {{0, 0}, {0, -1500}, {2500, 800}, {-3000, -2500}};
  const float agl[4] = {30, 250, 900, 2500};
  const float att[3][2] = {{0, 0.05f}, {0.45f, 0.12f}, {-0.6f, -0.08f}};   // roll, pitch
  static uint8_t fb[50 * 240];
  const Bitmap1 bm{fb, 400, 240, 50, 1};
  const int nX = VoxelView::kW / VoxelView::kColStep;
  std::vector<uint8_t> pics;
  std::vector<int16_t> depth;
  double pcMs = 0;
  long sSteps = 0, sBil = 0, sSpans = 0;
  int renders = 0;
  for (auto& ce : centres) {
    gCache.fill(ce[0], ce[1]);
    for (int hd = 0; hd < 6; hd++)
      for (float a : agl)
        for (auto& at : att) {
          Camera c;
          c.x = ce[0] + 37.0f * hd; c.y = ce[1] - 23.0f * hd;
          float h = 0;
          if (!gCache.height(c.x, c.y, &h)) h = 0;
          c.z = h + a;
          c.heading = 0.3f + hd * 1.0472f;
          c.roll = at[0]; c.pitch = at[1];
          memset(fb, renders & 1 ? 0x00 : 0xFF, sizeof fb);            // every pixel must be written
          const auto t0 = std::chrono::steady_clock::now();
          gVoxel.render(bm, gCache, c);
          pcMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
          { const auto& st = gVoxel.stats(); sSteps += st.steps; sBil += st.bilinear; sSpans += st.spans; }
          pics.insert(pics.end(), fb, fb + sizeof fb);
          for (int x = 0; x < VoxelView::kW; x += VoxelView::kColStep)
            for (int s = 0; s <= gVoxel.stepCount(); s++) depth.push_back((int16_t)gVoxel.nearerRow(x, s));
          if (renders % 37 == 0) { char p[64]; snprintf(p, sizeof p, "out/voxdigest_%03d.pgm", renders); writePgm(p, fb); }
          renders++;
        }
  }
  (void)nX;
  printf("voxel_digest: %d renders, PC %.3f ms per render; per render: %ld steps (%ld bilinear), %ld spans\n", renders, pcMs / renders,
         sSteps / renders, sBil / renders, sSpans / renders);
  if (save) {
    FILE* o = fopen(file, "wb");
    if (!o) { perror(file); return 2; }
    const uint32_t np = (uint32_t)pics.size(), nd = (uint32_t)depth.size();
    fwrite(&np, 4, 1, o); fwrite(&nd, 4, 1, o);
    fwrite(pics.data(), 1, np, o); fwrite(depth.data(), 2, nd, o);
    fclose(o);
    printf("saved %s\n", file);
    return 0;
  }
  FILE* in = fopen(file, "rb");
  if (!in) { perror(file); return 2; }
  uint32_t np = 0, nd = 0;
  if (fread(&np, 4, 1, in) != 1 || fread(&nd, 4, 1, in) != 1) return 2;
  std::vector<uint8_t> rp(np);
  std::vector<int16_t> rdp(nd);
  if (fread(rp.data(), 1, np, in) != np || fread(rdp.data(), 2, nd, in) != nd) return 2;
  fclose(in);
  if (np != pics.size() || nd != depth.size()) { printf("FAIL: size differs (%u/%zu pics, %u/%zu depth)\n", np, pics.size(), nd, depth.size()); return 1; }
  long bits = 0, worst = 0, dd = 0;
  for (int r = 0; r < renders; r++) {
    long b = 0;
    for (size_t i = 0; i < sizeof fb; i++) b += __builtin_popcount(rp[r * sizeof fb + i] ^ pics[r * sizeof fb + i]);
    bits += b;
    if (b > worst) worst = b;
  }
  for (uint32_t i = 0; i < nd; i++) dd += rdp[i] != depth[i];
  printf("%s: %ld pixels differ in total (worst render %ld of 96000), %ld depth entries differ\n", bits || dd ? "DIFF" : "SAME", bits, worst, dd);
  return bits || dd ? 1 : 0;
}
