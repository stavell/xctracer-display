// Renders a map window from a map pack the way the map page will: dithered hillshade, contour lines (index
// contours bolder), main roads with a white casing, towns with Latin labels. Writes a PGM (build.sh converts it).
//   terrain_demo PACK LAT LON HEADING_DEG M_PER_PX OUT.pgm [W H]
//   terrain_demo PACK bench          decode timing of every tile, and a full-window relief render
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <vector>
#include <Fonts/FreeSansBold9pt7b.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/terrain.h"

using namespace terrain;

static Terrain gTerrain;
static MapVectors gVec;
static RoadIter gRoads;
static TownIter gTowns;
static Font1 gFont;

static void thickLine(const Bitmap1& bm, int x0, int y0, int x1, int y1, int w, DrawMode m) {
  for (int a = -(w - 1) / 2; a <= w / 2; a++)
    for (int b = -(w - 1) / 2; b <= w / 2; b++) line(bm, x0 + a, y0 + b, x1 + a, y1 + b, m);
}

int main(int argc, char** argv) {
  if (argc < 3 || (argc < 7 && strcmp(argv[2], "bench") != 0)) { fprintf(stderr, "usage: terrain_demo PACK LAT LON HEADING M_PER_PX OUT.pgm [W H]\n"); return 2; }
  std::vector<uint8_t> bytes;
  FILE* f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  fseek(f, 0, SEEK_END); bytes.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) return 2;
  fclose(f);
  MemReader rd(bytes.data(), (uint32_t)bytes.size());
  if (gTerrain.open(&rd) != Terrain::OK) { fprintf(stderr, "pack rejected\n"); return 3; }
  gVec.open(&rd, gTerrain.info());

  if (strcmp(argv[2], "bench") == 0) {
    const Layer& L = gTerrain.info().layer[0];
    std::vector<uint32_t> off, len;
    for (uint32_t i = 0; i < (uint32_t)L.ty * L.tx; i++) {
      uint8_t ib[6];
      rd.read(L.idx_off + 3 * i, ib, 6);
      uint32_t a = le24(ib), b = le24(ib + 3);
      if (b > a) { off.push_back(L.data_off + a); len.push_back(b - a); }
    }
    static int16_t out[kMaxTileCells];
    uint64_t bytesTotal = 0;
    for (uint32_t l : len) bytesTotal += l;
    for (int clz = 1; clz >= 0; clz--) {
      auto t0 = std::chrono::steady_clock::now();
      const int reps = 5;
      for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < off.size(); i++)
          (clz ? decodeTile : decodeTileSimple)(bytes.data() + off[i], len[i], 32, out);
      double us = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / reps / off.size() * 1e6;
      printf("%s: %.2f us per tile (%.1f ns/cell), %zu tiles, %.0f coded bytes/tile\n", clz ? "CLZ" : "bit by bit", us,
             us * 1000 / 1024, off.size(), (double)bytesTotal / off.size());
    }
    uint32_t t0c = 0;
    for (float hd : {0.0f, 45.0f}) {
      for (float sc : {100.0f, 200.0f}) {
        Window w; w.lat = 42.5; w.lon = 25.0; w.headingDeg = hd; w.mPerPx = sc;
        static uint8_t g[240 * 400], c[240 * 400];
        gTerrain.flush(); t0c = gTerrain.tileDecodes();
        auto t0 = std::chrono::steady_clock::now();
        gTerrain.relief(w, ReliefStyle::light(), 0, 0, 400, g, c);
        double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e3;
        printf("relief 240x400 @%.0f m/px heading %.0f: %.1f ms, %u tile decodes\n", sc, hd, ms, gTerrain.tileDecodes() - t0c);
      }
    }
    return 0;
  }

  Window win;
  win.lat = atof(argv[2]); win.lon = atof(argv[3]); win.headingDeg = (float)atof(argv[4]); win.mPerPx = (float)atof(argv[5]);
  if (argc >= 9) { win.w = (int16_t)atoi(argv[7]); win.h = (int16_t)atoi(argv[8]); }
  const int W = win.w, H = win.h;
  std::vector<uint8_t> grey((size_t)W * H), cont((size_t)W * H), buf((size_t)((W + 7) / 8) * H, 0);
  Bitmap1 bm{buf.data(), (int16_t)W, (int16_t)H, (uint16_t)((W + 7) / 8), 0};

  int valid = gTerrain.relief(win, ReliefStyle::light(), 0, 0, H, grey.data(), cont.data());
  printf("%d of %d pixels with data, contour every %d m, %u tile decodes\n", valid, W * H,
         contourIntervalFor(win.mPerPx), (unsigned)gTerrain.tileDecodes());
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int g = grey[(size_t)y * W + x];
      bool white = g == kNoShade ? ((x ^ y) & 1) == 0 : bayer8(x, y) < 4 * g;
      bm.pixel(x, y, white ? DRAW_SET : DRAW_CLEAR);
    }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t c = cont[(size_t)y * W + x];
      if (!c) continue;
      bm.pixel(x, y, DRAW_CLEAR);
      if (c == CONTOUR_INDEX) { bm.pixel(x + 1, y, DRAW_CLEAR); bm.pixel(x, y + 1, DRAW_CLEAR); }
    }

  const VecXform xf = gVec.makeXform(win);
  const UnitBox box = gVec.boxForWindow(win);
  static const int width[4] = {3, 2, 2, 1};
  for (int pass = 0; pass < 2; pass++) {
    gRoads.begin(gVec, box);
    while (const RoadPoly* p = gRoads.next()) {
      int w = width[p->cls & 3];
      for (int i = 0; i + 1 < p->n; i++) {
        int x0 = (int)floorf(xf.px(p->x[i], p->y[i])), y0 = (int)floorf(xf.py(p->x[i], p->y[i]));
        int x1 = (int)floorf(xf.px(p->x[i + 1], p->y[i + 1])), y1 = (int)floorf(xf.py(p->x[i + 1], p->y[i + 1]));
        if (pass == 0) thickLine(bm, x0, y0, x1, y1, w + 2, DRAW_SET);
        else thickLine(bm, x0, y0, x1, y1, w, DRAW_CLEAR);
      }
    }
  }

  gFont.build(&FreeSansBold9pt7b, 0);
  struct R { int x0, y0, x1, y1; };
  std::vector<R> placed;
  Town t;
  gTowns.begin(gVec, box);
  while (gTowns.next(&t)) {                        // most populous first
    float px = xf.px((float)t.x, (float)t.y), py = xf.py((float)t.x, (float)t.y);
    int x = (int)floorf(px), y = (int)floorf(py);
    if (x < 3 || y < 3 || x >= W - 3 || y >= H - 3) continue;
    int tw = textWidth(t.latin, gFont);
    R r{x + 6, y - 14, x + 6 + tw, y + 2};
    if (r.x1 >= W) { r.x0 = x - 6 - tw; r.x1 = x - 6; }
    bool hit = r.x0 < 0;
    for (const R& o : placed) hit |= !(r.x0 > o.x1 || r.x1 < o.x0 || r.y0 > o.y1 || r.y1 < o.y0);
    fillDisc(bm, x, y, t.city ? 5 : 4, DRAW_SET);
    fillDisc(bm, x, y, t.city ? 3 : 2, DRAW_CLEAR);
    if (hit) continue;
    placed.push_back(r);
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        if (dx || dy) drawText(bm, r.x0 + dx, r.y1 - 3 + dy, t.latin, gFont, DRAW_SET);
    drawText(bm, r.x0, r.y1 - 3, t.latin, gFont, DRAW_CLEAR);
  }

  FILE* o = fopen(argv[6], "wb");
  fprintf(o, "P5\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) fputc(bm.getPixel(x, y) ? 255 : 0, o);
  fclose(o);
  return 0;
}
