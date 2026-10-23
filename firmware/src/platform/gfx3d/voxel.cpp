#include "platform/util/hot.h"
HOT_FILE_O3
#include "platform/gfx3d/voxel.h"
#include <math.h>
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/util/fastmem.h"

namespace gfx3d {

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
      pat32_[l][x] = b * 0x01010101u;
    }
}

// The sky above the terrain of one ray (rows ymin - 1 up to 0) as runs: bands of 48 rows that lighten upward from the horizon hy,
// and haze below it where the terrain ended early.
int VoxelView::skyRuns(int ns, int hy, int ymin) {
  int y = ymin - 1;
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
    spans_[ns].y0 = (int16_t)top;
    spans_[ns].y1 = (int16_t)y;
    spans_[ns].lvl = (uint8_t)lvl;
    ns++;
    y = top - 1;
  }
  return ns;
}

// Writes the kColStep (4) native rows of one ray. The runs cover the column without gaps from the bottom (y 399 = native bit 0) to
// the top, so each row is built in 32-bit words from the bit-0 end and stored once: no per-span read-modify-write. The four rows share
// the run boundaries and differ only in their dither phase (pat32_[lvl][xm0 + c]; x is a multiple of 4, so xm0 is 0 or 4). Rows are
// 50 bytes: 12 whole words (unaligned stores, fine on the M4) and a final half word.
void VoxelView::compose(uint8_t* row, int stride, int n, int xm0) const {
  static_assert(kColStep == 4, "compose writes four rows");
  uint8_t* r1 = row + stride;
  uint8_t* r2 = r1 + stride;
  uint8_t* r3 = r2 + stride;
  uint32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
  int w = 0;
  for (int k = 0; k < n; k++) {
    int lo = 399 - spans_[k].y1;
    const int hi = 399 - spans_[k].y0;
    const uint32_t* P = pat32_[spans_[k].lvl] + xm0;
    const uint32_t p0 = P[0], p1 = P[1], p2 = P[2], p3 = P[3];
    for (;;) {
      const int wEnd = (w << 5) | 31;
      const int h = hi < wEnd ? hi : wEnd;
      const uint32_t m = (0xFFFFFFFFu << (lo & 31)) & (0xFFFFFFFFu >> (31 - (h & 31)));
      a0 |= p0 & m; a1 |= p1 & m; a2 |= p2 & m; a3 |= p3 & m;
      if (h != wEnd) break;                       // the run ends inside this word
      const int o = w << 2;
      memcpy(row + o, &a0, 4); memcpy(r1 + o, &a1, 4); memcpy(r2 + o, &a2, 4); memcpy(r3 + o, &a3, 4);
      a0 = a1 = a2 = a3 = 0;
      w++;
      if (h == hi) break;
      lo = h + 1;
    }
  }
  const uint16_t b0 = (uint16_t)a0, b1 = (uint16_t)a1, b2 = (uint16_t)a2, b3 = (uint16_t)a3;   // bits 384..399
  memcpy(row + 48, &b0, 2); memcpy(r1 + 48, &b1, 2); memcpy(r2 + 48, &b2, 2); memcpy(r3 + 48, &b3, 2);
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
  // Cell bounds as one unsigned compare each, with the float bias folded in: a near (bilinear) step needs cells ix and ix + 1 inside
  // [x0, x1), a far step cell ix. A window too small gives 0: every step is outside, as before.
  const int bx = x0 + (int)kBias, by = y0 + (int)kBias;
  const unsigned limBx = x1 - x0 - 1 > 0 ? (unsigned)(x1 - x0 - 1) : 0u, limBy = y1 - y0 - 1 > 0 ? (unsigned)(y1 - y0 - 1) : 0u;
  const unsigned limNx = x1 - x0 > 0 ? (unsigned)(x1 - x0) : 0u, limNy = y1 - y0 > 0 ? (unsigned)(y1 - y0) : 0u;
  static_assert(kBilinearSteps % 2 == 0 && kBilinearSteps / 2 <= kYBins, "the near steps are walked in pairs");
  const int nBil = n < kBilinearSteps ? (n & ~1) : kBilinearSteps;

  // One ray per pair of screen columns (kColStep), through the middle of the pair; its spans are written to both rows,
  // each with its own dither phase, so the picture keeps its full dither detail and only the silhouette is two pixels coarse.
  for (int x = 0; x < kW; x += kColStep) {
    const float xc = (float)x + 0.5f * (kColStep - 1);
    const float u = (xc - kCx) / f;
    const float dxk = (fx + rx * u) * HeightCache::kInv, dyk = (fy + ry * u) * HeightCache::kInv;
    const float hyf = horizon - (xc - kCx) * tanRoll;
    int ymin = kH, ns = 0, i = 0;
    float yminF = (float)kH;                                 // ymin as a float: the visibility test needs no conversion per step
    int16_t* yb = yb_[x / kColStep];
    // A sample that rises above ymin becomes a span. Returns false when the ray ends (the top of the screen is reached).
    auto span = [&](int si, float h, float sh, float yf) -> bool {
      const int yi = yf < 0 ? 0 : (int)yf;
      if (yi >= ymin) return true;
      // Tone and fog folded into one multiply-add per step; alternate 25 m height bands one grey step apart: the terraces
      // show the lie of the land and give the ground a texture that streams past.
      int lvl = (int)(sh * ka_[si] + kb_[si]) + ((((int)(h * (1.0f / 25.0f))) & 1) ? (si < 30 ? 2 : 1) : 0);
      if (lvl > 16) lvl = 16;
      if (lvl < 3) lvl = 3;
      spans_[ns].y0 = (int16_t)yi;
      spans_[ns].y1 = (int16_t)(ymin - 1);
      spans_[ns].lvl = (uint8_t)lvl;
      ns++;
      ymin = yi;
      yminF = (float)yi;
      return yi != 0;
    };
    // Near steps: bilinear height; the shade (also bilinear) only where the terrain shows. False: the ray ends at this step.
    auto bilinear = [&](int si) -> bool {
      const float s = s_[si];
      const float pxk = cxk + s * dxk, pyk = cyk + s * dyk;
      const int ix = (int)pxk, iy = (int)pyk;
      if ((unsigned)(ix - bx) >= limBx || (unsigned)(iy - by) >= limBy) return false;   // the 2 x 2 cells must be inside
      const float tx = pxk - (float)ix, ty = pyk - (float)iy;
      const int s00 = HeightCache::slot(ix, iy), s10 = HeightCache::slot(ix + 1, iy);   // the bias is a multiple of N
      const int s01 = HeightCache::slot(ix, iy + 1), s11 = HeightCache::slot(ix + 1, iy + 1);
      const float a = H[s00], b = H[s10], c = H[s01], d = H[s11];
      const float h = (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
      const float yf = hyf + (camZ - h) * fs_[si];
      if (yf >= yminF) return true;
      const float sa = S[s00], sb = S[s10], sc = S[s01], sd = S[s11];
      const float sh = (sa + (sb - sa) * tx) * (1.0f - ty) + (sc + (sd - sc) * tx) * ty;
      return span(si, h, sh, yf);
    };
    // Far steps: the nearest cell.
    auto nearest = [&](int si) -> bool {
      const float s = s_[si];
      const float pxk = cxk + s * dxk, pyk = cyk + s * dyk;
      const int ix = (int)(pxk + 0.5f), iy = (int)(pyk + 0.5f);
      if ((unsigned)(ix - bx) >= limNx || (unsigned)(iy - by) >= limNy) return false;
      const int sl = HeightCache::slot(ix, iy);
      const float h = H[sl];
      const float yf = hyf + (camZ - h) * fs_[si];
      if (yf >= yminF) return true;
      return span(si, h, S[sl], yf);
    };
    // Steps in pairs: the depth buffer takes ymin before every even step (bin b after steps 0 .. 2b + 1).
    for (; i < nBil; i += 2) {
      if (i) yb[(i >> 1) - 1] = (int16_t)ymin;
      if (!bilinear(i)) goto done;
      if (!bilinear(i + 1)) { i++; goto done; }
    }
    for (; i < n; i += 2) {
      yb[(i >> 1) - 1 < kYBins ? (i >> 1) - 1 : kYBins - 1] = (int16_t)ymin;
      if (!nearest(i)) goto done;
      if (i + 1 < n && !nearest(i + 1)) { i++; goto done; }
    }
    i = n;
  done:
    for (int b = (i >> 1) < kYBins ? (i >> 1) : kYBins; b < kYBins; b++) yb[b] = (int16_t)ymin;
    walked += i;
    bilin += i < kBilinearSteps ? i : kBilinearSteps;
    const int hyi = hyf < -2000.0f ? -2000 : (hyf > 2000.0f ? 2000 : (int)floorf(hyf));
    const int nr = ymin > 0 ? skyRuns(ns, hyi, ymin) : ns;
    compose(bm.buf + (size_t)x * bm.stride, bm.stride, nr, x & 7);
    stats_.spans += nr * kColStep;
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

}  // namespace gfx3d
