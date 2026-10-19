#include "platform/util/hot.h"
HOT_FILE_O3
#include "apps/games/sim/sky3d.h"
#include <math.h>
#include "apps/games/sim/sim_math.h"
#include "platform/gfx/shapes.h"

namespace sim {

struct SkyFx::Pj {                                   // the chase camera's projection (as ChaseCam::project), set up once per frame
  float fx, fy, f, cx, hz, tr, x, y, z;
  bool pt(float wx, float wy, float wz, float* sx, float* sy, float* s) const {
    const float dx = wx - x, dy = wy - y;
    const float fwd = dx * fx + dy * fy;
    if (fwd < 20.0f) return false;
    const float right = dx * fy - dy * fx;
    *sx = cx + f * right / fwd;
    *sy = hz + (z - wz) * f / fwd - (*sx - cx) * tr;
    *s = fwd;
    return true;
  }
};

namespace {
constexpr int kW = VoxelView::kW, kH = VoxelView::kH;

// Rows y0..y1 of one column (top = 0), clipped to 0..yMax. kMode 0: replace by the pattern (clouds), 2: clear the pattern's set bits
// (the black dashes of a shaft: the picture shows through between them).
template <int kMode>
inline void span(uint8_t* row, int y0, int y1, int yMax, uint8_t pat) {
  if (y1 > yMax) y1 = yMax;
  if (y0 < 0) y0 = 0;
  if (y0 > y1) return;
  const int a = 399 - y1, b = 399 - y0;
  const int ba = a >> 3, bb = b >> 3;
  const uint8_t lm = (uint8_t)(0xFFu << (a & 7)), rm = (uint8_t)(0xFFu >> (7 - (b & 7)));
  auto put = [&](uint8_t& r, uint8_t m) { r = kMode == 2 ? (uint8_t)(r & ~(pat & m)) : (uint8_t)((r & ~m) | (pat & m)); };
  if (ba == bb) { put(row[ba], (uint8_t)(lm & rm)); return; }
  put(row[ba], lm);
  put(row[bb], rm);
  for (int i = ba + 1; i < bb; i++) put(row[i], 0xFF);
}
inline float fractf(float v) { return v - floorf(v); }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------- clouds

void SkyFx::drawCloud(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, const ColumnInfo& c, float sx, float sy, float s, int cs) {
  const float Wp = c.width * pj.f / s;                   // width in pixels
  // Discs in units of the width: five along the flat base, two on top.
  struct Puff { float ux, cy, r; } p[7];
  for (int i = 0; i < 5; i++) {
    const float t = (i + 0.5f) / 5.0f;
    const float a = 1.0f - (2 * t - 1) * (2 * t - 1);
    const float u1 = unit(hash3(c.id, (uint32_t)i, 0xC10D)), u2 = unit(hash3(c.id, (uint32_t)i, 0xC10E));
    p[i].ux = (t - 0.5f) * 0.82f + (u1 - 0.5f) * 0.05f;
    p[i].r = 0.14f + 0.13f * a + 0.03f * u2;
    p[i].cy = 0.65f * p[i].r;
  }
  for (int i = 0; i < 2; i++) {
    const float u1 = unit(hash3(c.id, (uint32_t)(5 + i), 0xC10D)), u2 = unit(hash3(c.id, (uint32_t)(5 + i), 0xC10E));
    p[5 + i].ux = (i ? 0.12f : -0.14f) + (u1 - 0.5f) * 0.12f;
    p[5 + i].r = 0.18f + 0.06f * u2;
    p[5 + i].cy = 0.20f + 0.5f * p[5 + i].r;
  }
  const int ic = vv.stepFor(s);
  const int xa = (int)fmaxf(0.0f, floorf(sx - 0.6f * Wp)), xb = (int)fminf((float)kW - 1, ceilf(sx + 0.6f * Wp));
  const int k0 = xa / cs, k1 = xb / cs;
  const int nk = k1 - k0 + 1;
  if (nk <= 0 || nk > kW / 2 + 2) return;
  // Pass 1: the top edge of each column group (30000 = nothing there).
  for (int k = 0; k < nk; k++) {
    const float xc = (float)((k0 + k) * cs) + 0.5f * (cs - 1);
    const float yb = sy - (xc - sx) * pj.tr;
    float top = 1e9f;
    for (int i = 0; i < 7; i++) {
      const float dx = xc - (sx + p[i].ux * Wp), r = p[i].r * Wp;
      if (fabsf(dx) >= r) continue;
      const float y = yb - p[i].cy * Wp - sqrtf(r * r - dx * dx);
      if (y < top) top = y;
    }
    top_[k] = top > 1e8f ? (int16_t)30000 : (int16_t)lroundf(top);
  }
  const int bt = (int)clampf(0.055f * Wp, 2.0f, 9.0f);
  for (int k = 0; k < nk; k++) {
    const int t = top_[k];
    if (t == 30000) continue;
    const int xs = (k0 + k) * cs;
    const float xc = (float)xs + 0.5f * (cs - 1);
    const int bot = (int)lroundf(sy - (xc - sx) * pj.tr);
    if (t >= bot) continue;
    const int tp = k > 0 && top_[k - 1] != 30000 ? top_[k - 1] : bot, tn = k + 1 < nk && top_[k + 1] != 30000 ? top_[k + 1] : bot;
    const int d = abs(t - tp) > abs(t - tn) ? abs(t - tp) : abs(t - tn);
    const int o = d <= 2 ? 2 : (d > 10 ? 10 : d);                    // steep flanks: the outline fills the gap
    const int yE = vv.nearerRow(xs, ic) - 1;                          // terrain nearer than the cloud covers rows below this
    if (t > yE) continue;
    const int h = bot - t;
    const int b1 = t + o + (h * 3) / 10, b2 = t + o + (h * 6) / 10;
    for (int c2 = 0; c2 < cs; c2++) {
      if (xs + c2 >= kW) break;
      uint8_t* row = bm.buf + (size_t)(xs + c2) * bm.stride;
      const int xm = (xs + c2) & 7;
      span<0>(row, t, t + o - 1, yE, 0x00);
      span<0>(row, t + o, b1 - 1, yE, vv.pattern(16, xm));
      span<0>(row, b1, b2 - 1, yE, vv.pattern(13, xm));
      span<0>(row, b2, bot - bt, yE, vv.pattern(10, xm));
      span<0>(row, bot - bt + 1, bot - 1, yE, vv.pattern(6, xm));
      span<0>(row, bot, bot, yE, 0x00);
    }
    st_.cloudCols += cs;
  }
}

int SkyFx::cloudShots(CloudShot* out, int cap) const {
  int n = 0;
  for (; n < nShots_ && n < cap; n++) out[n] = shots_[n];
  return n;
}

// ---------------------------------------------------------------------------------------------------------------- shafts

void SkyFx::drawShaft(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, const Air& air, const ColumnInfo& c, float t, float cb, int nseg,
                      int cs, int level) {
  struct P { float sx, sy, w, s; bool ok; } q[7];
  for (int k = 0; k <= nseg; k++) {
    const float z = c.z0 + (cb - c.z0) * k / (float)nseg;
    float x, y;
    air.columnPoint(c, z, t, &x, &y);
    q[k].ok = pj.pt(x, y, z, &q[k].sx, &q[k].sy, &q[k].s);
    q[k].w = Air::coreRadius(c, z) * 1.5f * pj.f / (q[k].s > 1 ? q[k].s : 1);
  }
  for (int k = 0; k < nseg; k++) {
    if (!q[k].ok || !q[k + 1].ok) continue;
    const P& a = q[k];
    const P& b = q[k + 1];
    // Quad A(-w) B(+w) at the lower end, D(-w) C(+w) at the upper end; a vertical line crosses its edges in an interval.
    const float vx[4] = {a.sx - a.w, a.sx + a.w, b.sx + b.w, b.sx - b.w};
    const float vy[4] = {a.sy, a.sy, b.sy, b.sy};
    float lo = vx[0], hi = vx[0];
    for (int i = 1; i < 4; i++) { if (vx[i] < lo) lo = vx[i]; if (vx[i] > hi) hi = vx[i]; }
    int xa = (int)fmaxf(0.0f, floorf(lo)), xb = (int)fminf((float)kW - 1, ceilf(hi));
    xa -= xa % cs;
    const int ic = vv.stepFor(0.5f * (a.s + b.s));
    for (int xs = xa; xs <= xb; xs += cs) {
      const float xc = (float)xs + 0.5f * (cs - 1);
      float ylo = 1e9f, yhi = -1e9f;
      for (int e = 0; e < 4; e++) {
        const int e2 = (e + 1) & 3;
        float x0 = vx[e], x1 = vx[e2], y0 = vy[e], y1 = vy[e2];
        if (x0 > x1) { float tx = x0; x0 = x1; x1 = tx; float ty = y0; y0 = y1; y1 = ty; }
        if (xc < x0 || xc > x1) continue;
        const float y = x1 - x0 < 1e-3f ? y0 : y0 + (y1 - y0) * (xc - x0) / (x1 - x0);
        if (y < ylo) ylo = y;
        if (y > yhi) yhi = y;
      }
      if (ylo > yhi) continue;
      const int r0 = (int)ceilf(ylo), r1 = (int)ceilf(yhi) - 1;
      if (r1 < r0 || r1 < 0) continue;
      const int yE = vv.nearerRow(xs, ic) - 1;
      // Edge pixels of the shaft (the outline that stays readable over a dithered hillside) get a denser dot pattern than the inside.
      const float f = b.sy == a.sy ? 0.5f : clampf((0.5f * (float)(r0 + r1) - a.sy) / (b.sy - a.sy), 0.0f, 1.0f);
      const float edge = lerpf(a.w, b.w, f) - fabsf(xc - lerpf(a.sx, b.sx, f));
      // The outline (two dashed black lines) stays readable over sky and hillside; inside, vertical dashed hatching on every
      // `level`-th column (staggered), so the shaft reads as a translucent curtain and never as a dither of its own.
      const bool isEdge = edge < 2.5f;
      st_.assistCycles += 100.0f;
      if (!isEdge && (level == 0 || (xs % level) != 0 || lerpf(a.w, b.w, f) > 60.0f)) continue;
      st_.assistCycles += cs * (40.0f + 3.0f * (float)(r1 - r0 + 1) / 8.0f);
      for (int c2 = 0; c2 < cs && xs + c2 < kW; c2++) {
        uint8_t* row = bm.buf + (size_t)(xs + c2) * bm.stride;
        span<2>(row, r0, r1, yE, isEdge ? (uint8_t)0x77 : (uint8_t)(((xs / (level ? level : 1)) & 1) ? 0xF0 : 0x0F));
      }
      st_.shaftCols += cs;
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------- bubbles

void SkyFx::drawBubbles(const Bitmap1& bm, const VoxelView& vv, const Pj& pj, const Air& air, const ColumnInfo& c, float t, float cb, int n,
                        const Glider* core) {
  const float H = cb - c.z0;
  const float rate = kRiseVisual * c.S;
  for (int k = 0; k < n && (core || st_.bubbles < kMaxBubbles); k++) {
    float z;
    const uint32_t hk = hash3(c.id, (uint32_t)k, core ? 0xB0B1u : 0xB0B0u);
    if (core) z = core->z - 40.0f + 140.0f * fractf((float)k / n + (t - c.t0) * rate / 140.0f);       // -40 m .. +100 m around the glider
    else z = c.z0 + H * (0.03f + 0.94f * fractf((float)k / n + (t - c.t0) * rate / H + 0.37f * unit(hk)));
    const float R = Air::coreRadius(c, z);
    const float lat = R * (core ? 0.6f * unit(hash32(hk ^ 1)) : 1.2f * unit(hash32(hk ^ 1)) * unit(hash32(hk ^ 5)));      // piled up toward the core
    const float rm = core ? 0.10f * R + 4.0f : 0.15f * R + 4.0f;
    const float a = 2 * kPi * unit(hash32(hk ^ 2));
    float x, y;
    air.columnPoint(c, z, t, &x, &y);
    x += lat * cosf(a);
    y += lat * sinf(a);
    float sx, sy, s;
    if (!pj.pt(x, y, z, &sx, &sy, &s) || s < 25.0f) continue;
    int r = (int)lroundf(rm * pj.f / s);
    if (r < 3) r = 3;
    if (r > 16) r = 16;
    const int ix = (int)lroundf(sx), iy = (int)lroundf(sy);
    if (ix < -r || ix >= kW + r || iy < -r || iy >= kH + r) continue;
    const int xe = ix < 0 ? 0 : (ix >= kW ? kW - 1 : ix);
    if (iy >= vv.nearerRow(xe, vv.stepFor(s))) continue;      // behind the ground
    const ClipBox clip(0, 0, kW - 1, kH - 1);
    fillDisc(bm, ix, iy, r + 1, DRAW_SET, clip);              // white halo, then the black ring: reads on sky and on hillside
    fillRing(bm, ix, iy, r, r > 2 ? (r > 7 ? r - 2 : r - 1) : 0, DRAW_CLEAR, clip);
    if (r > 6) fillDisc(bm, ix - r / 3, iy - r / 3, 1, DRAW_CLEAR, clip);      // highlight dot
    st_.bubbles++;
    st_.assistCycles += 500.0f + 45.0f * r;
  }
}

// ---------------------------------------------------------------------------------------------------------------- frame

void SkyFx::draw(const Bitmap1& bm, const VoxelView& vv, const Air& air, const Glider& g, const Camera& cam, float t, bool assist,
                 Style style, bool clouds) {
  st_ = Stats();
  nShots_ = 0;
  Pj pj;
  pj.fx = sinf(cam.heading); pj.fy = cosf(cam.heading);
  pj.f = VoxelView::focal();
  pj.cx = (kW - 1) * 0.5f;
  pj.hz = VoxelView::kHorizonY + pj.f * tanf(cam.pitch);
  pj.tr = tanf(cam.roll);
  pj.x = cam.x; pj.y = cam.y; pj.z = cam.z;
  const float cb = air.cloudBase();
  const int n = air.columns(cam.x, cam.y, t, kViewRange, col_, kMaxCols);
  st_.found = n;

  // ---- clouds: the nearest few that are in front of the camera, far to near
  if (clouds) {
    struct Cand { int i; float sx, sy, s, wpx; } cand[kMaxCols];
    int nc = 0;
    for (int i = 0; i < n; i++) {
      const ColumnInfo& c = col_[i];
      if (c.cloud < 0.03f) continue;
      float sx, sy, s;
      if (!pj.pt(c.cloudX, c.cloudY, cb, &sx, &sy, &s) || s < 350.0f || s > VoxelView::kMaxDist) continue;
      const float wpx = c.width * pj.f / s;
      if (wpx < 5.0f || sx < -0.6f * wpx || sx > kW + 0.6f * wpx) continue;
      cand[nc++] = {i, sx, sy, s, wpx};
    }
    for (int a = 1; a < nc; a++) {                        // far to near
      Cand k = cand[a];
      int b = a - 1;
      while (b >= 0 && cand[b].s < k.s) { cand[b + 1] = cand[b]; b--; }
      cand[b + 1] = k;
    }
    int first = nc > kMaxClouds && !unlimited_ ? nc - kMaxClouds : 0;
    // Cost per column evaluation about 330 cycles (7 sqrt and six spans); coarser (4 columns per evaluation) halves it.
    int cs = 2;
    auto cost = [&](int step) { float c = 0; for (int a = first; a < nc; a++) c += fminf(cand[a].wpx * 1.2f, (float)kW) / step * 330.0f; return c; };
    if (cost(2) > kCloudBudget && !unlimited_) { cs = 4; st_.level = 1; }
    while (!unlimited_ && first < nc - 1 && cost(cs) > kCloudBudget) first++;           // leave the farthest out
    for (int a = first; a < nc; a++) {
      const Cand& k = cand[a];
      const int before = st_.cloudCols;
      drawCloud(bm, vv, pj, col_[k.i], k.sx, k.sy, k.s, cs);
      st_.clouds++;
      shots_[nShots_++] = {col_[k.i].id, k.sx, k.sy, k.wpx, k.s, st_.cloudCols == before};
    }
    st_.cloudCycles = st_.cloudCols / (float)cs * 330.0f;
  }

  // ---- assist: the thermals
  if (!assist) return;
  const bool shafts = style != Style::BUBBLES, bubbles = style != Style::COLUMNS;
  int idx[kMaxCols], na = 0;
  float dist[kMaxCols];
  for (int i = 0; i < n; i++) {
    if (col_[i].strength < 1.0f) continue;
    // Only columns in front of the camera: the middle of the column (trigger and top) within the view cone.
    const float mx = 0.5f * (col_[i].x0 + col_[i].cx) - cam.x, my = 0.5f * (col_[i].y0 + col_[i].cy) - cam.y;
    const float fwd = mx * pj.fx + my * pj.fy, right = mx * pj.fy - my * pj.fx;
    if (fwd < 100.0f || fabsf(right) > 1.1f * fwd + 1500.0f) continue;
    const float dx = col_[i].cx - cam.x, dy = col_[i].cy - cam.y;
    dist[i] = sqrtf(dx * dx + dy * dy);
    int b = na;
    while (b > 0 && dist[idx[b - 1]] > dist[i]) { idx[b] = idx[b - 1]; b--; }
    idx[b] = i;
    na++;
  }
  if (na > kMaxShafts) na = kMaxShafts;
  // Plan: full detail (5 segments, 2 columns per evaluation, 8 bubbles per thermal) or coarse (3 segments, 4 columns, 4 bubbles).
  auto estimate = [&](int level, int upto) {
    float cyc = 0;
    for (int a = 0; a < upto; a++) {
      const ColumnInfo& c = col_[idx[a]];
      const float s = fmaxf(dist[idx[a]], 100.0f);
      const float wpx = fminf(2.0f * 1.5f * Air::coreRadius(c, 0.5f * (c.z0 + cb)) * pj.f / s + 6.0f, 240.0f);
      const float hseg = fminf((cb - c.z0) * pj.f / s, 330.0f) / (level ? 3 : 5);
      if (shafts) cyc += (level ? 3 : 5) * wpx * 0.5f * (100.0f + 2.0f * (40.0f + 3.0f * hseg / 8.0f));
      if (bubbles) cyc += (level ? 4 : 8) * 700.0f;
    }
    return cyc;
  };
  int upto = na, level = 0;
  if (estimate(0, upto) > kAssistBudget) level = 1;
  while (upto > 1 && estimate(level, upto) > kAssistBudget) upto--;
  if (level > st_.level) st_.level = level;
  const int cs2 = 2, nseg = level ? 3 : 5, nb = level ? 4 : 8;
  for (int a = upto - 1; a >= 0; a--) {                    // far to near
    const ColumnInfo& c = col_[idx[a]];
    if (shafts) {
      drawShaft(bm, vv, pj, air, c, t, cb, nseg, cs2, style == Style::BOTH ? 12 : (c.strength > 3.2f ? 4 : 6));
      st_.shafts++;
    }
    if (bubbles) {
      const int cnt = (int)lroundf(fminf((float)nb, 2.0f + 0.5f * c.strength * nb / 4.0f));
      drawBubbles(bm, vv, pj, air, c, t, cb, cnt < 2 ? 2 : cnt, nullptr);
    }
  }
  // The thermal the glider is in: a few large bubbles around it, in every style.
  for (int i = 0; i < n; i++) {
    const ColumnInfo& c = col_[i];
    if (c.strength < 0.8f) continue;
    float x, y;
    air.columnPoint(c, g.z, t, &x, &y);
    const float dx = g.x - x, dy = g.y - y, R = Air::coreRadius(c, g.z);
    if (dx * dx + dy * dy < 1.4f * 1.4f * R * R) drawBubbles(bm, vv, pj, air, c, t, cb, 16, &g);
  }
}

}  // namespace sim
