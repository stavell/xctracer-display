#include "apps/games/sim/topdown.h"
#include <math.h>
#include <string.h>
#include "apps/games/sim/sim_math.h"
#include "platform/gfx/gray.h"

namespace sim {

namespace {
constexpr float kBias = 4096.0f;
constexpr float kContour = 200.0f;
constexpr int kIndexEvery = 5;

float angDiffDeg(float a, float b) {
  float d = fabsf(wrapPi(a - b)) * kRad2Deg;
  return d;
}
}  // namespace

void TopDownView::reset() {
  memset(buf_, 0xFF, sizeof buf_);
  front_ = 0;
  shownValid_ = busy_ = false;
  col_ = 0;
  ox_ = oy_ = 0;
  renders_ = 0;
  pixels_ = 0;
}

// One screen column of the picture: a native row of the cache.
void TopDownView::renderColumn(const HeightCache& hc, const Params& p, uint8_t* buf, int px) {
  const float sn = sinf(p.heading), cs = cosf(p.heading);
  const float rx = cs, ry = -sn, fx = sn, fy = cs;               // right and forward in world axes
  const float k = HeightCache::kInv;
  // World position of (px, row 0); row y is y * m behind it.
  const float ahead = (float)kGliderY, side = (float)(px - kGliderX);
  const float wx0 = p.x + p.m * (side * rx + ahead * fx), wy0 = p.y + p.m * (side * ry + ahead * fy);
  const float dxk = -p.m * fx * k, dyk = -p.m * fy * k;
  float pxk = wx0 * k + kBias, pyk = wy0 * k + kBias;
  const int16_t* H = hc.heights();
  const uint8_t* S = hc.shades();
  const int x0 = hc.x0(), x1 = hc.x1(), y0 = hc.y0(), y1 = hc.y1();
  for (int y = 0; y < kH; y++, pxk += dxk, pyk += dyk) {
    const int ix = (int)pxk, iy = (int)pyk;
    const int cx = ix - (int)kBias, cy = iy - (int)kBias;
    if (cx < x0 || cx + 1 >= x1 || cy < y0 || cy + 1 >= y1) { pix_[y] = 1; band_[y] = 0x7fff; continue; }
    const float tx = pxk - (float)ix, ty = pyk - (float)iy;
    const int s00 = HeightCache::slot(cx, cy), s10 = HeightCache::slot(cx + 1, cy);
    const int s01 = HeightCache::slot(cx, cy + 1), s11 = HeightCache::slot(cx + 1, cy + 1);
    const float a = H[s00], b = H[s10], c = H[s01], d = H[s11];
    const float h = (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
    const float sa = S[s00], sb = S[s10], sc = S[s01], sd = S[s11];
    const float sh = (sa + (sb - sa) * tx) * (1.0f - ty) + (sc + (sd - sc) * tx) * ty;
    int lvl = (int)(sh + 3.0f + 0.5f);
    if (lvl > 16) lvl = 16;
    uint8_t white = bayer8(px, y) < 4 * lvl ? 1 : 0;
    const int16_t band = (int16_t)floorf(h * (1.0f / kContour));
    band_[y] = band;
    int boundary = -99999;                                   // contour boundary index between this pixel and a neighbour
    if (y > 0 && band_[y - 1] != 0x7fff && band_[y - 1] != band) boundary = band > band_[y - 1] ? band : band_[y - 1];
    if (px > 0 && prevBand_[y] != 0x7fff && prevBand_[y] != band) {
      const int bx = band > prevBand_[y] ? band : prevBand_[y];
      if (bx > boundary) boundary = bx;
    }
    if (boundary != -99999) {
      white = 0;
      if (((boundary % kIndexEvery) + kIndexEvery) % kIndexEvery == 0 && y > 0) pix_[y - 1] = 0;   // index lines 2 px wide
    }
    pix_[y] = white;
  }
  uint8_t* row = buf + (size_t)px * kStride;
  memset(row, 0xFF, kStride);
  for (int y = 0; y < kH; y++)
    if (!pix_[y]) {
      const int nx = kH - 1 - y;
      row[nx >> 3] &= (uint8_t)~(1u << (nx & 7));
    }
  memcpy(prevBand_, band_, sizeof prevBand_);
}

int TopDownView::update(const HeightCache& hc, float x, float y, float heading, float mPerPx, int budgetPx) {
  if (!busy_) {
    bool stale = !shownValid_ || mPerPx != shown_.m;
    if (!stale) {
      const float dx = x - shown_.x, dy = y - shown_.y;
      stale = sqrtf(dx * dx + dy * dy) / shown_.m > kStalePx || angDiffDeg(heading, shown_.heading) > kStaleDeg;
    }
    if (!stale) return 0;
    making_ = Params{x, y, heading, mPerPx};
    busy_ = true;
    col_ = 0;
    for (int i = 0; i < kH; i++) prevBand_[i] = 0x7fff;
  }
  int used = 0;
  do {
    renderColumn(hc, making_, back(), col_);
    col_++;
    used += kH;
  } while (col_ < kW && used < budgetPx);
  pixels_ += (uint32_t)used;
  if (col_ >= kW) {
    front_ = 1 - front_;
    shown_ = making_;
    shownValid_ = true;
    busy_ = false;
    renders_++;
  }
  return used;
}

void TopDownView::renderNow(const HeightCache& hc, float x, float y, float heading, float mPerPx) {
  busy_ = false;
  shownValid_ = false;
  while (!shownValid_ || angDiffDeg(heading, shown_.heading) > 0.01f) update(hc, x, y, heading, mPerPx, kW * kH);
}

int TopDownView::shownPixel(int px, int py) const {
  if (px < 0 || px >= kW || py < 0 || py >= kH) return 1;
  const int nx = kH - 1 - py;
  return (buf_[front_][(size_t)px * kStride + (nx >> 3)] >> (nx & 7)) & 1;
}

void TopDownView::worldToScreen(float wx, float wy, float* sx, float* sy) const {
  const float sn = sinf(shown_.heading), cs = cosf(shown_.heading);
  const float dx = wx - shown_.x, dy = wy - shown_.y;
  const float right = (dx * cs - dy * sn) / shown_.m, fwd = (dx * sn + dy * cs) / shown_.m;
  *sx = kGliderX + right + (float)ox_;
  *sy = kTopY + kGliderY - fwd + (float)oy_;
}

namespace {
// Bits [b, b + 8) of a native row of 47 bytes (LSB first); outside the row they read white.
inline uint8_t srcBits(const uint8_t* row, int b) {
  const int p = b >> 3, s = b & 7;
  const unsigned lo = (p >= 0 && p < TopDownView::kStride) ? row[p] : 0xFFu;
  const unsigned hi = (p + 1 >= 0 && p + 1 < TopDownView::kStride) ? row[p + 1] : 0xFFu;
  return (uint8_t)((lo >> s) | (hi << (8 - s)));
}
}  // namespace

void TopDownView::blit(const Bitmap1& screen, float gx, float gy) {
  if (!shownValid_ || screen.rotation != 1 || screen.w != 400 || screen.h != 240) return;
  // Where the glider is in the shown picture's frame, and the integer offset that puts it at the anchor.
  const float sn = sinf(shown_.heading), cs = cosf(shown_.heading);
  const float dx = gx - shown_.x, dy = gy - shown_.y;
  const float right = (dx * cs - dy * sn) / shown_.m, fwd = (dx * sn + dy * cs) / shown_.m;
  ox_ = -(int)lroundf(right);
  oy_ = (int)lroundf(fwd);
  // Picture pixel (px, py) -> screen (px + ox, py + oy + kTopY). Native bit: 399 - (py + oy + kTopY) = (375 - py) - oy.
  const int shift = -oy_;                                   // dest bit = source bit + shift
  const int lo = shift > 0 ? shift : 0, hi = shift < 0 ? kH - 1 + shift : kH - 1;
  if (lo > hi) return;
  const uint8_t* src = buf_[front_];
  for (int px = 0; px < kW; px++) {
    const int sxp = px + ox_;
    if (sxp < 0 || sxp >= 240) continue;
    const uint8_t* srow = src + (size_t)px * kStride;
    uint8_t* drow = screen.buf + (size_t)sxp * screen.stride;
    for (int j = lo >> 3; j <= (hi >> 3); j++) {
      const uint8_t v = srcBits(srow, 8 * j - shift);
      uint8_t m = 0xFF;
      if (8 * j < lo) m &= (uint8_t)(0xFFu << (lo - 8 * j));
      if (8 * j + 7 > hi) m &= (uint8_t)(0xFFu >> (8 * j + 7 - hi));
      drow[j] = (uint8_t)((drow[j] & ~m) | (v & m));
    }
  }
}

}  // namespace sim
