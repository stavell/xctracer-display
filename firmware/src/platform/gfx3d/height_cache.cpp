#include "platform/gfx3d/height_cache.h"
#include <string.h>

namespace gfx3d {

namespace {
// The sun: from the south-west, 38 degrees up.
inline int imin(int a, int b) { return a < b ? a : b; }
constexpr float kLx = -0.5571f, kLy = -0.5571f, kLz = 0.6157f;
}  // namespace

void HeightCache::invalidateAll() {
  warming_ = shading_ = false;
  vx0_ = vx1_ = vy0_ = vy1_ = 0;
  haveTarget_ = false;
  curKind_ = NONE;
  curPos_ = 0;
  loaded_ = 0;
}

void HeightCache::setCentre(float x, float y) {
  const int cx = (int)floorf(x * kInv + 0.5f), cy = (int)floorf(y * kInv + 0.5f);
  tx0_ = cx - N / 2;
  ty0_ = cy - N / 2;
  haveTarget_ = true;
}

void HeightCache::loadCell(int ix, int iy) {
  float h = 0;
  if (!src_ || !src_->at(ix * kCell, iy * kCell, &h) || !isfinite(h)) h = 0;
  h = clampf(h, -400.0f, 8000.0f);
  const int s = slot(ix, iy);
  h_[s] = (int16_t)lroundf(h);
  s_[s] = 11;
  loaded_++;
}

// Shade of one cell from the heights around it (one-sided at the edge of the valid rectangle).
void HeightCache::shadeCell(int ix, int iy) {
  if (!inside(ix, iy)) return;
  const float h0 = h_[slot(ix, iy)];
  const bool l = inside(ix - 1, iy), r = inside(ix + 1, iy), d = inside(ix, iy - 1), u = inside(ix, iy + 1);
  const float hl = l ? h_[slot(ix - 1, iy)] : h0, hr = r ? h_[slot(ix + 1, iy)] : h0;
  const float hd = d ? h_[slot(ix, iy - 1)] : h0, hu = u ? h_[slot(ix, iy + 1)] : h0;
  const float sx = (l && r) ? 2.0f : ((l || r) ? 1.0f : 1.0f);
  const float sy = (d && u) ? 2.0f : ((d || u) ? 1.0f : 1.0f);
  const float dhx = (hr - hl) / (sx * kCell) * kExag;     // east
  const float dhy = (hu - hd) / (sy * kCell) * kExag;     // north
  const float inv = 1.0f / sqrtf(dhx * dhx + dhy * dhy + 1.0f);
  const float dot = (-dhx * kLx - dhy * kLy + kLz) * inv;   // normal = (-dhx, -dhy, 1) / len
  const float level = 11.0f + 24.0f * (dot - kLz);
  s_[slot(ix, iy)] = (uint8_t)clampf(lroundf(level), 1.0f, 16.0f);
}

void HeightCache::shadeLine(Kind k, int idx) {
  if (k == COL) for (int iy = vy0_; iy < vy1_; iy++) shadeCell(idx, iy);
  else for (int ix = vx0_; ix < vx1_; ix++) shadeCell(ix, idx);
}

int HeightCache::refresh(int budget) {
  if (!clock_) return refreshWork(budget);
  const uint32_t t0 = clock_();
  const int done = refreshWork(budget);
  const uint32_t d = clock_() - t0;
  rs_.calls++; rs_.sumUs += d; rs_.cells += (uint32_t)done;
  if (d > rs_.maxUs) rs_.maxUs = d;
  return done;
}

int HeightCache::refreshWork(int budget) {
  if (!haveTarget_ || !src_) return 0;
  const int tx1 = tx0_ + N, ty1 = ty0_ + N;
  // Keep only what is still inside the wanted window.
  if (vx0_ < tx0_) vx0_ = tx0_;
  if (vx1_ > tx1) vx1_ = tx1;
  if (vy0_ < ty0_) vy0_ = ty0_;
  if (vy1_ > ty1) vy1_ = ty1;
  int done = 0;
  const uint32_t cost0 = src_->cost();
  if (vx0_ >= vx1_ || vy0_ >= vy1_) {                       // nothing left: seed with the middle cell
    const int mx = tx0_ + N / 2, my = ty0_ + N / 2;
    vx0_ = mx; vx1_ = mx + 1; vy0_ = my; vy1_ = my + 1;
    loadCell(mx, my);
    shadeCell(mx, my);
    curKind_ = NONE;
    done++;
  }
  while (done < budget) {
    Kind k;
    int idx;
    bool atMin = false;
    // Grow on the side that reaches least far from the middle: a window that starts small grows evenly all round.
    const int mx = tx0_ + N / 2, my = ty0_ + N / 2;
    const int eL = vx0_ > tx0_ ? mx - vx0_ : 1 << 20, eR = vx1_ < tx1 ? vx1_ - 1 - mx : 1 << 20;
    const int eD = vy0_ > ty0_ ? my - vy0_ : 1 << 20, eU = vy1_ < ty1 ? vy1_ - 1 - my : 1 << 20;
    const int best = imin(imin(eL, eR), imin(eD, eU));
    if (best == 1 << 20) break;
    if (eL == best) { k = COL; idx = vx0_ - 1; atMin = true; }
    else if (eR == best) { k = COL; idx = vx1_; }
    else if (eD == best) { k = ROW; idx = vy0_ - 1; atMin = true; }
    else { k = ROW; idx = vy1_; }
    if (k != curKind_ || idx != curIdx_) { curKind_ = k; curIdx_ = idx; curPos_ = 0; }
    const int lo = k == COL ? vy0_ : vx0_, hi = k == COL ? vy1_ : vx1_;
    int i = lo + curPos_;
    while (i < hi && done < budget) {
      if (k == COL) loadCell(idx, i); else loadCell(i, idx);
      i++;
      done++;
      if (src_->cost() != cost0) budget = done;             // a tile was decoded: that is this call's share, the rest waits
    }
    curPos_ = i - lo;
    if (i < hi) break;                                      // budget used up inside the strip
    // The strip is complete: it joins the valid rectangle, and its shade (and its inner neighbour's) is computed.
    if (k == COL) { if (atMin) vx0_ = idx; else vx1_ = idx + 1; }
    else { if (atMin) vy0_ = idx; else vy1_ = idx + 1; }
    shadeLine(k, idx);
    shadeLine(k, atMin ? idx + 1 : idx - 1);
    curKind_ = NONE;
    curPos_ = 0;
  }
  return done;
}

// Load a rectangle of the window from nothing, in square blocks: a row of the window crosses 5 map tiles and the box keeps 2, so
// the strip order of refresh() decodes about 5 tiles per line (693 decodes, 2.7 s on the device); a block of 32 cells touches 1
// to 4 tiles, and the blocks of a row of blocks follow one another through the tiles (160 decodes).
void HeightCache::loadRect(int x0, int y0, int x1, int y1) {
  constexpr int kBlock = 32;
  for (int by = y0; by < y1; by += kBlock)
    for (int bx = x0; bx < x1; bx += kBlock)
      for (int iy = by; iy < by + kBlock && iy < y1; iy++)
        for (int ix = bx; ix < bx + kBlock && ix < x1; ix++) loadCell(ix, iy);
  vx0_ = x0; vx1_ = x1; vy0_ = y0; vy1_ = y1;
  for (int iy = y0; iy < y1; iy++)
    for (int ix = x0; ix < x1; ix++) shadeCell(ix, iy);
  curKind_ = NONE;
}

int HeightCache::fill(float x, float y) {
  setCentre(x, y);
  if (src_ && vx0_ >= vx1_) {
    loadRect(tx0_, ty0_, tx0_ + N, ty0_ + N);
    return N * N;
  }
  int total = 0, n;
  while ((n = refresh(4096)) > 0) total += n;
  return total;
}

// Only the 2r x 2r cells around (x, y); refresh() grows the rest. For a start that must show something at once.
int HeightCache::fillNear(float x, float y, int r) {
  setCentre(x, y);
  if (!src_ || vx0_ < vx1_ || r >= N / 2) return fill(x, y);
  const int mx = tx0_ + N / 2, my = ty0_ + N / 2;
  loadRect(mx - r, my - r, mx + r, my + r);
  warming_ = true;
  shading_ = false;
  wx0_ = tx0_; wy0_ = ty0_; wpos_ = 0;
  return 4 * r * r;
}

int HeightCache::warmStep(int budget) {
  constexpr int kBlock = 32, kPerBlock = kBlock * kBlock, kBlocksRow = N / kBlock, kShadeRows = 8;
  int done = 0;
  if (warming_) {
    const uint32_t cost0 = src_->cost();
    while (done < budget && wpos_ < N * N) {
      const int blk = wpos_ / kPerBlock, w = wpos_ % kPerBlock;
      const int ix = wx0_ + (blk % kBlocksRow) * kBlock + w % kBlock, iy = wy0_ + (blk / kBlocksRow) * kBlock + w / kBlock;
      wpos_++;
      if (inside(ix, iy)) continue;                           // the near rectangle is loaded already
      loadCell(ix, iy);
      done++;
      if (src_->cost() != cost0) break;                       // a tile was decoded: that is this frame's share
    }
    if (wpos_ >= N * N) {                                     // everything is in: the valid rectangle is the whole window
      vx0_ = wx0_; vx1_ = wx0_ + N; vy0_ = wy0_; vy1_ = wy0_ + N;
      warming_ = false;
      shading_ = true;
      shadeRow_ = vy0_;
      curKind_ = NONE;
    }
  } else if (shading_) {
    for (int n = 0; n < kShadeRows && shadeRow_ < vy1_; n++, shadeRow_++)
      for (int ix = vx0_; ix < vx1_; ix++) { shadeCell(ix, shadeRow_); done++; }
    if (shadeRow_ >= vy1_) shading_ = false;
  }
  return done;
}

bool HeightCache::complete() const {
  return haveTarget_ && vx0_ == tx0_ && vx1_ == tx0_ + N && vy0_ == ty0_ && vy1_ == ty0_ + N;
}

bool HeightCache::height(float x, float y, float* out) const {
  const float fx = x * kInv, fy = y * kInv;
  const float flx = floorf(fx), fly = floorf(fy);
  const int ix = (int)flx, iy = (int)fly;
  if (ix < vx0_ || ix + 1 >= vx1_ || iy < vy0_ || iy + 1 >= vy1_) return false;
  const float tx = fx - flx, ty = fy - fly;
  const float a = h_[slot(ix, iy)], b = h_[slot(ix + 1, iy)], c = h_[slot(ix, iy + 1)], d = h_[slot(ix + 1, iy + 1)];
  *out = lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
  return true;
}

bool HeightCache::shade16(float x, float y, int* out) const {
  const float fx = x * kInv, fy = y * kInv;
  const float flx = floorf(fx), fly = floorf(fy);
  const int ix = (int)flx, iy = (int)fly;
  if (ix < vx0_ || ix + 1 >= vx1_ || iy < vy0_ || iy + 1 >= vy1_) return false;
  const float tx = fx - flx, ty = fy - fly;
  const float a = s_[slot(ix, iy)], b = s_[slot(ix + 1, iy)], c = s_[slot(ix, iy + 1)], d = s_[slot(ix + 1, iy + 1)];
  *out = (int)(lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty) * 16.0f);
  return true;
}

bool HeightCache::gradient(float x, float y, float d, float* gx, float* gy) const {
  float a, b, c, e;
  if (!height(x + d, y, &a) || !height(x - d, y, &b) || !height(x, y + d, &c) || !height(x, y - d, &e)) return false;
  *gx = (a - b) / (2 * d);
  *gy = (c - e) / (2 * d);
  return true;
}

}  // namespace gfx3d
