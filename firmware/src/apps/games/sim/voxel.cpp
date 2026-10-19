#include "platform/util/hot.h"
HOT_FILE_O3
#include "apps/games/sim/voxel.h"
#include <math.h>
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/util/fastmem.h"

namespace sim {

namespace {
constexpr float kFogLevel = 12.0f;        // distant terrain fades to this grey, so ridges still stand out against the sky
constexpr float kBias = 4096.0f;          // keeps cell coordinates positive so a float->int cast floors
constexpr float kCx = (VoxelView::kW - 1) * 0.5f;
}  // namespace

float VoxelView::focal() { return (kW * 0.5f) / tanf(kFovDeg * 0.5f * 0.017453292f); }

VoxelView::VoxelView() {
  // Step table: the step length grows with the distance (24 m + 7 % of it), so far steps are not wasted on
  // sub-pixel changes. Fog from 25 % of the range to the full range.
  const float f = focal();
  float s = kNear;
  nSteps_ = 0;
  while (s < kMaxDist && nSteps_ < kMaxSteps) {
    const float ds = 24.0f + 0.07f * s;
    s_[nSteps_] = s;
    ds_[nSteps_] = ds;
    fs_[nSteps_] = f / s;
    const float t = (s - 0.25f * kMaxDist) / (0.75f * kMaxDist);
    const float fog = t <= 0 ? 0.0f : (t >= 1 ? 1.0f : t * t * (3.0f - 2.0f * t));
    fog_[nSteps_] = (uint8_t)lroundf(fog * 16.0f);
    // level = tone(shade) * (1 - fog) + fogLevel * fog, tone(shade) = 7.5 + (shade - 11) * 0.8 (darker than the sky; the deepest
    // shade stays a grey so the ground keeps its texture), + 0.5 for rounding.
    const float fg = fog_[nSteps_] * (1.0f / 16.0f);
    ka_[nSteps_] = 0.8f * (1.0f - fg);
    kb_[nSteps_] = (7.5f - 11.0f * 0.8f) * (1.0f - fg) + kFogLevel * fg + 0.5f;
    s += ds;
    nSteps_++;
  }
  for (int l = 0; l <= 16; l++)
    for (int x = 0; x < 8; x++) {
      uint8_t b = 0;
      for (int k = 0; k < 8; k++)
        if (bayer8(x, 7 - k) < 4 * l) b |= (uint8_t)(1u << k);
      pat_[l][x] = b;
    }
}

// Rows y0..y1 (inclusive, 0 <= y0 <= y1 < 400, top = 0) of one column: native bits 399 - y1 .. 399 - y0.
void VoxelView::fillSpan(uint8_t* row, int y0, int y1, uint8_t pat) const {
  const int a = 399 - y1, b = 399 - y0;
  const int ba = a >> 3, bb = b >> 3;
  const uint8_t lm = (uint8_t)(0xFFu << (a & 7)), rm = (uint8_t)(0xFFu >> (7 - (b & 7)));
  if (ba == bb) {
    const uint8_t m = lm & rm;
    row[ba] = (uint8_t)((row[ba] & ~m) | (pat & m));
    return;
  }
  row[ba] = (uint8_t)((row[ba] & ~lm) | (pat & lm));
  row[bb] = (uint8_t)((row[bb] & ~rm) | (pat & rm));
  const int mid = bb - ba - 1;                    // whole bytes between the two masked ones
  if (mid >= 12) fastFill(row + ba + 1, pat, (size_t)mid);
  else for (int i = 1; i <= mid; i++) row[ba + i] = pat;
}

void VoxelView::sky(uint8_t* row, int x, int hy, int ymin) {
  int y = ymin - 1;
  const int xm = x & 7;
  while (y >= 0) {
    const int d = hy - y;
    int lvl, top;
    if (d < 0) {                                  // below the horizon, beyond the last terrain: haze
      lvl = 12;                                  // beyond the last terrain sample: the same haze grey as the far terrain
      top = hy;
    } else {
      const int k = d / 48;
      lvl = 16 - (k > 3 ? 3 : k);
      top = hy - (k + 1) * 48 + 1;
    }
    if (top < 0) top = 0;
    fillSpan(row, top, y, pat_[lvl][xm]);
    stats_.spans++;
    y = top - 1;
  }
}

void VoxelView::render(const Bitmap1& bm, const HeightCache& hc, const Camera& cam) {
  stats_ = Stats();
  if (bm.rotation != 1 || bm.w != 400 || bm.h != 240 || bm.stride < 50) return;
  const float f = focal();
  const float fx = sinf(cam.heading), fy = cosf(cam.heading);
  const float rx = fy, ry = -fx;                            // right = forward rotated clockwise
  const float tanRoll = tanf(cam.roll);
  const float horizon = kHorizonY + f * tanf(cam.pitch);
  const int x0 = hc.x0(), x1 = hc.x1(), y0 = hc.y0(), y1 = hc.y1();
  const int16_t* H = hc.heights();
  const uint8_t* S = hc.shades();
  const int n = nSteps_;
  const float cxk = cam.x * HeightCache::kInv + kBias, cyk = cam.y * HeightCache::kInv + kBias;
  const float camZ = cam.z;
  int walked = 0, bilin = 0, lastSpans = 0;

  // One ray per pair of screen columns (kColStep), through the middle of the pair; its spans are written to both rows,
  // each with its own dither phase, so the picture keeps its full dither detail and only the silhouette is two pixels coarse.
  for (int x = 0; x < kW; x += kColStep) {
    const float xc = (float)x + 0.5f * (kColStep - 1);
    const float u = (xc - kCx) / f;
    const float dxk = (fx + rx * u) * HeightCache::kInv, dyk = (fy + ry * u) * HeightCache::kInv;
    const float hyf = horizon - (xc - kCx) * tanRoll;
    int ymin = kH, ns = 0, i = 0;
    int16_t* yb = yb_[x / kColStep];
    for (; i < n; i++) {
      if (!(i & 1) && i) yb[(i >> 1) - 1 < kYBins ? (i >> 1) - 1 : kYBins - 1] = (int16_t)ymin;
      const float s = s_[i];
      const float pxk = cxk + s * dxk, pyk = cyk + s * dyk;
      float h, sh;
      if (i < kBilinearSteps) {
        const int ix = (int)pxk, iy = (int)pyk;
        const float tx = pxk - (float)ix, ty = pyk - (float)iy;
        const int cx0 = ix - (int)kBias, cy0 = iy - (int)kBias;
        if (cx0 < x0 || cx0 + 1 >= x1 || cy0 < y0 || cy0 + 1 >= y1) break;
        const int s00 = HeightCache::slot(cx0, cy0), s10 = HeightCache::slot(cx0 + 1, cy0);
        const int s01 = HeightCache::slot(cx0, cy0 + 1), s11 = HeightCache::slot(cx0 + 1, cy0 + 1);
        const float a = H[s00], b = H[s10], c = H[s01], d = H[s11];
        h = (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
        const float sa = S[s00], sb = S[s10], sc = S[s01], sd = S[s11];
        sh = (sa + (sb - sa) * tx) * (1.0f - ty) + (sc + (sd - sc) * tx) * ty;
      } else {
        const int cx0 = (int)(pxk + 0.5f) - (int)kBias, cy0 = (int)(pyk + 0.5f) - (int)kBias;
        if (cx0 < x0 || cx0 >= x1 || cy0 < y0 || cy0 >= y1) break;
        const int sl = HeightCache::slot(cx0, cy0);
        h = H[sl];
        sh = S[sl];
      }
      const float yf = hyf + (camZ - h) * fs_[i];
      if (yf >= (float)ymin) continue;
      const int yi = yf < 0 ? 0 : (int)yf;
      if (yi >= ymin) continue;
      // Tone and fog folded into one multiply-add per step; alternate 25 m height bands one grey step apart: the terraces
      // show the lie of the land and give the ground a texture that streams past.
      int lvl = (int)(sh * ka_[i] + kb_[i]) + ((((int)(h * (1.0f / 25.0f))) & 1) ? (i < 30 ? 2 : 1) : 0);
      if (lvl > 16) lvl = 16;
      if (lvl < 3) lvl = 3;
      spans_[ns].y0 = (int16_t)yi;
      spans_[ns].y1 = (int16_t)(ymin - 1);
      spans_[ns].lvl = (uint8_t)lvl;
      ns++;
      ymin = yi;
      if (yi == 0) break;
    }
    for (int b = (i >> 1) < kYBins ? (i >> 1) : kYBins; b < kYBins; b++) yb[b] = (int16_t)ymin;
    walked += i;
    bilin += i < kBilinearSteps ? i : kBilinearSteps;
    const int hyi = hyf < -2000.0f ? -2000 : (hyf > 2000.0f ? 2000 : (int)floorf(hyf));
    for (int c = 0; c < kColStep; c++) {
      uint8_t* row = bm.buf + (size_t)(x + c) * bm.stride;
      const int xm = (x + c) & 7;
      for (int k = 0; k < ns; k++) fillSpan(row, spans_[k].y0, spans_[k].y1, pat_[spans_[k].lvl][xm]);
      if (ymin > 0) sky(row, x + c, hyi, ymin);
    }
    stats_.spans += ns * kColStep;
    lastSpans += ns;
    if (ymin == 0) stats_.early++;
    stats_.columns += kColStep;
  }
  stats_.rays = kW / kColStep;
  stats_.steps = walked;
  stats_.samples = walked;
  stats_.bilinear = bilin;
  (void)lastSpans;
}

}  // namespace sim
