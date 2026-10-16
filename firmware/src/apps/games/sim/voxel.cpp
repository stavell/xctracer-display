#include "apps/games/sim/voxel.h"
#include <math.h>
#include <string.h>
#include "platform/gfx/gray.h"

namespace sim {

namespace {
constexpr float kFogLevel = 12.0f;        // distant terrain fades to this grey, so ridges still stand out against the sky
constexpr float kBias = 4096.0f;          // keeps cell coordinates positive so a float->int cast floors
constexpr float kCx = (VoxelView::kW - 1) * 0.5f;
}  // namespace

float VoxelView::focal() { return (kW * 0.5f) / tanf(kFovDeg * 0.5f * 0.017453292f); }

VoxelView::VoxelView() {
  // Step table: the step length grows with the distance (22 m + 5 % of it), so far steps are not wasted on
  // sub-pixel changes. Fog from 25 % of the range to the full range.
  const float f = focal();
  float s = kNear;
  nSteps_ = 0;
  while (s < kMaxDist && nSteps_ < kMaxSteps) {
    const float ds = 22.0f + 0.05f * s;
    s_[nSteps_] = s;
    ds_[nSteps_] = ds;
    fs_[nSteps_] = f / s;
    const float t = (s - 0.25f * kMaxDist) / (0.75f * kMaxDist);
    const float fog = t <= 0 ? 0.0f : (t >= 1 ? 1.0f : t * t * (3.0f - 2.0f * t));
    fog_[nSteps_] = (uint8_t)lroundf(fog * 16.0f);
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
  if (bb - ba > 1) memset(row + ba + 1, pat, (size_t)(bb - ba - 1));
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

  for (int x = 0; x < kW; x++) {
    uint8_t* row = bm.buf + (size_t)x * bm.stride;
    const float u = ((float)x - kCx) / f;
    const float dx = fx + rx * u, dy = fy + ry * u;
    const float hyf = horizon - ((float)x - kCx) * tanRoll;
    int ymin = kH;
    const int xm = x & 7;
    const float cxk = cam.x * HeightCache::kInv + kBias, cyk = cam.y * HeightCache::kInv + kBias;
    const float dxk = dx * HeightCache::kInv, dyk = dy * HeightCache::kInv;
    int i = 0;
    for (; i < n; i++) {
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
        stats_.bilinear++;
      } else {
        const int cx0 = (int)(pxk + 0.5f) - (int)kBias, cy0 = (int)(pyk + 0.5f) - (int)kBias;
        if (cx0 < x0 || cx0 >= x1 || cy0 < y0 || cy0 >= y1) break;
        const int sl = HeightCache::slot(cx0, cy0);
        h = H[sl];
        sh = S[sl];
      }
      stats_.samples++;
      const float yf = hyf + (cam.z - h) * fs_[i];
      if (yf >= (float)ymin) continue;
      const int yi = yf < 0 ? 0 : (int)yf;
      if (yi >= ymin) continue;
      const int fog = fog_[i];
      sh = 7.5f + (sh - 11.0f) * 0.8f;                     // darker than the sky; the deepest shade stays a grey so the ground keeps its texture
      int lvl = (int)(sh + (kFogLevel - sh) * (fog * (1.0f / 16.0f)) + 0.5f);
      if (lvl > 16) lvl = 16;
      if (lvl < 3) lvl = 3;
      fillSpan(row, yi, ymin - 1, pat_[lvl][xm]);
      stats_.spans++;
      stats_.bytes += ((ymin - yi) >> 3) + 1;
      ymin = yi;
      if (yi == 0) { stats_.early++; break; }
    }
    stats_.steps += i;
    if (ymin > 0) {
      int hyi = hyf < -2000.0f ? -2000 : (hyf > 2000.0f ? 2000 : (int)floorf(hyf));
      sky(row, x, hyi, ymin);
    }
    stats_.columns++;
  }
}

}  // namespace sim
