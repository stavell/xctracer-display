#include "apps/games/sim/chase.h"
#include <math.h>
#include "platform/gfx/shapes.h"

namespace sim {

void ChaseCam::update(const Glider& g, float rollOverride, float dt) {
  heading_ = wrapPi(heading_ + wrapPi(g.heading - heading_) * lagFactor(dt, kHeadingTau));
  const float target = rollOverride != rollOverride ? kRollFrac * g.bank : rollOverride;
  roll_ += (target - roll_) * lagFactor(dt, kRollTau);
}

Camera ChaseCam::camera(const Glider& g, const HeightCache& hc) const {
  Camera c;
  c.heading = heading_ < 0 ? heading_ + 2 * kPi : heading_;
  c.x = g.x - kBack * sinf(heading_);
  c.y = g.y - kBack * cosf(heading_);
  c.z = g.z + kUp;
  float gh;
  if (hc.height(c.x, c.y, &gh) && c.z < gh + 8.0f) c.z = gh + 8.0f;       // never inside a hillside behind the glider
  c.roll = roll_;
  c.pitch = kPitch;
  return c;
}

bool ChaseCam::project(const Camera& c, float x, float y, float z, float* sx, float* sy, float* s) {
  const float fx = sinf(c.heading), fy = cosf(c.heading);
  const float dx = x - c.x, dy = y - c.y;
  const float fwd = dx * fx + dy * fy, right = dx * fy - dy * fx;
  if (fwd < 5.0f) return false;
  const float f = VoxelView::focal();
  const float cx = (VoxelView::kW - 1) * 0.5f;
  *sx = cx + f * right / fwd;
  const float horizon = VoxelView::kHorizonY + f * tanf(c.pitch) - (*sx - cx) * tanf(c.roll);
  *sy = horizon + (c.z - z) * f / fwd;
  *s = fwd;
  return true;
}

namespace {
struct Rot {
  float cx, cy, c, s;                     // pivot and rotation (clockwise on the screen)
  void pt(float x, float y, int* ox, int* oy) const {
    const float dx = x - cx, dy = y - cy;
    *ox = (int)lroundf(cx + dx * c - dy * s);
    *oy = (int)lroundf(cy + dx * s + dy * c);
  }
};

// The glider seen from behind, in one pass. DRAW_SET writes white, DRAW_CLEAR black: the white pass is a halo (outline only,
// drawn grown) so the black pass reads on dark ground; the black pass fills the canopy solid (arcs stacked down by the
// canopy's visible depth), draws the lines and the pilot. Hook point (px, py) is the harness; W the half span in pixels.
// Fill the quadrilateral p[0..3] (screen pixels, portrait rotation: a screen column is a run in one native row) with one vline per column.
// Measured: the canopy as a stack of 9 - 17 arches of 16 line segments each, drawn pixel by pixel, was 6.9 ms of the 3D frame.
void fillQuad(const Bitmap1& bm, const int (&px)[4], const int (&py)[4], DrawMode m, int pad, bool padLeft, bool padRight) {
  int x0 = px[0], x1 = px[0];
  float slope[4];
  for (int i = 0; i < 4; i++) {
    if (px[i] < x0) x0 = px[i];
    if (px[i] > x1) x1 = px[i];
    const int j = (i + 1) & 3;
    slope[i] = px[i] == px[j] ? 0.0f : (float)(py[j] - py[i]) / (float)(px[j] - px[i]);
  }
  for (int x = x0; x <= x1; x++) {
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 4; i++) {
      const int j = (i + 1) & 3;
      const int ax = px[i], bx = px[j];
      if ((x < ax && x < bx) || (x > ax && x > bx)) continue;
      if (ax == bx) {
        const float ya = (float)py[i], yb = (float)py[j];
        if (ya < lo) lo = ya;
        if (ya > hi) hi = ya;
        if (yb < lo) lo = yb;
        if (yb > hi) hi = yb;
      } else {
        const float y = (float)py[i] + slope[i] * (float)(x - ax);
        if (y < lo) lo = y;
        if (y > hi) hi = y;
      }
    }
    if (lo > hi) continue;
    const int ya = (int)(lo + 1000.5f) - 1000 - pad, yb = (int)(hi + 1000.5f) - 1000 + pad;     // rounded, without lroundf
    bm.vline(x, ya, yb, m);
    if (padLeft && x == x0) bm.vline(x - 1, ya, yb, m);
    if (padRight && x == x1) bm.vline(x + 1, ya, yb, m);
  }
}

// The glider seen from behind, in one pass. DRAW_SET writes white, DRAW_CLEAR black: the white pass is a halo (outline only,
// drawn grown) so the black pass reads on dark ground; the black pass fills the canopy solid (arcs stacked down by the
// canopy's visible depth), draws the lines and the pilot. Hook point (px, py) is the harness; W the half span in pixels.
// Fill the quadrilateral p[0..3] (screen pixels, portrait rotation: a screen column is a run in one native row) with one vline per column.
// Measured: the canopy as a stack of 9 - 17 arches of 16 line segments each, drawn pixel by pixel, was 6.9 ms of the 3D frame.
void fillQuad(const Bitmap1& bm, const int (&px)[4], const int (&py)[4], DrawMode m) {
  int x0 = px[0], x1 = px[0];
  for (int i = 1; i < 4; i++) { if (px[i] < x0) x0 = px[i]; if (px[i] > x1) x1 = px[i]; }
  for (int x = x0; x <= x1; x++) {
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 4; i++) {
      const int j = (i + 1) & 3;
      const int ax = px[i], bx = px[j];
      if ((x < ax && x < bx) || (x > ax && x > bx)) continue;
      if (ax == bx) {
        const float ya = (float)py[i], yb = (float)py[j];
        if (ya < lo) lo = ya; if (ya > hi) hi = ya;
        if (yb < lo) lo = yb; if (yb > hi) hi = yb;
      } else {
        const float y = py[i] + (float)(py[j] - py[i]) * (float)(x - ax) / (float)(bx - ax);
        if (y < lo) lo = y;
        if (y > hi) hi = y;
      }
    }
    if (lo <= hi) bm.vline(x, (int)lroundf(lo), (int)lroundf(hi), m);
  }
}

// The glider seen from behind, in one pass. DRAW_SET writes white, DRAW_CLEAR black: the black pass is a halo (the canopy filled
// and shifted by one pixel to the four sides, so it shows as an outline) so the white pass reads on dark ground; the white pass fills
// the canopy between its top and bottom arch, draws the lines and the pilot. Hook point (px, py) is the harness; W the half span in pixels.
void sprite(const Bitmap1& bm, float px, float py, float W, float thick, float rollRad, bool ink, int grow) {
  const DrawMode m = ink ? DRAW_SET : DRAW_CLEAR;
  const float L = 0.78f * W;                              // line length
  const Rot r{px, py - 0.45f * L, cosf(rollRad), sinf(rollRad)};
  const int N = 8;
  auto arch = [&](float u) { return -L - 0.10f * W * (1.0f - u * u) + 0.22f * W * u * u * u * u; };   // tips down
  int hx, hy;
  r.pt(px, py, &hx, &hy);
  int a, b, c2, d2, e2, f2;
  r.pt(px, py + 0.12f * W, &a, &b);
  r.pt(px - 0.12f * W, py + 0.42f * W, &c2, &d2);
  r.pt(px + 0.12f * W, py + 0.42f * W, &e2, &f2);
  int tx[2 * N + 1], ty[2 * N + 1], bx[2 * N + 1], by[2 * N + 1];       // the canopy's top and bottom edge
  for (int i = -N; i <= N; i++) {
    const float u = (float)i / N, shape = 1.0f - 0.35f * fabsf(u);
    r.pt(px + W * u, py + arch(u), &tx[i + N], &ty[i + N]);
    r.pt(px + W * u, py + arch(u) + thick * shape, &bx[i + N], &by[i + N]);
  }
  for (int i = 0; i < 2 * N; i++) {                                     // canopy fill; the halo is the same fill one pixel taller and wider
    const int qx[4] = {tx[i], tx[i + 1], bx[i + 1], bx[i]};
    const int qy[4] = {ty[i], ty[i + 1], by[i + 1], by[i]};
    fillQuad(bm, qx, qy, m, ink ? 0 : grow, !ink && grow && i == 0, !ink && grow && i == 2 * N - 1);
  }
  for (int oy = -grow; oy <= grow; oy++)
    for (int ox = -grow; ox <= grow; ox++) {
      // lines from the underside of the canopy to the harness
      for (int i = -N; i <= N; i += 4) line(bm, bx[i + N] + ox, by[i + N] + oy, hx + ox, hy + oy, m);
      line(bm, hx + ox, hy + oy, a + ox, b + oy, m);                // pilot: body and legs
      line(bm, a + ox, b + oy, c2 + ox, d2 + oy, m);
      line(bm, a + ox, b + oy, e2 + ox, f2 + oy, m);
    }
  fillDisc(bm, hx, hy + 0, 2 + grow, m);                             // head
  if (ink) {                                                         // cell seams of the canopy, black on the white fill
    for (int i = -6; i <= 6; i += 3) {
      const float u = (float)i / N * 1.0f * 0.75f;
      int x0, y0, x1, y1;
      r.pt(px + W * u, py + arch(u) + 1.0f, &x0, &y0);
      r.pt(px + W * u, py + arch(u) + thick * (1.0f - 0.35f * fabsf(u)) - 1.0f, &x1, &y1);
      line(bm, x0, y0, x1, y1, DRAW_CLEAR);
    }
  }
}
}  // namespace

void ChaseCam::drawGlider(const Bitmap1& bm, const Glider& g, const HeightCache& hc, const Camera& cam) const {
  float sx, sy, s;
  if (!project(cam, g.x, g.y, g.z, &sx, &sy, &s)) return;
  // Ground shadow: a flat ellipse where the glider's position meets the ground, while that is on the screen.
  float gh;
  if (hc.height(g.x, g.y, &gh)) {
    float shx, shy, ss;
    if (project(cam, g.x, g.y, gh, &shx, &shy, &ss) && shy < 396.0f && shy > sy + 12.0f) {
      const float w = 4.0f * VoxelView::focal() / ss;           // half of a 8 m shadow
      const int hw = (int)clampf(w, 3.0f, 26.0f), cy = (int)lroundf(shy);
      for (int dy = -2; dy <= 2; dy++) {
        const int span = (int)(hw * sqrtf(1.0f - (dy * dy) / 9.0f));
        line(bm, (int)shx - span, cy + dy, (int)shx + span, cy + dy, DRAW_CLEAR);
      }
    }
  }
  const float W = 46.0f;                                    // half span, px: exaggerated, a real 5 m would be 19 px at this distance
  const float kmh = g.airspeedKmh();
  const float thick = 9.0f + 8.0f * clampf((kmh - Polar::kTrimKmh) / (Polar::kBarKmh - Polar::kTrimKmh), -0.3f, 1.0f);
  const float rollRel = g.bank - cam.roll;
  const float py = sy + 14.0f;
  sprite(bm, sx, py, W, thick, rollRel, false, 1);           // black outline
  sprite(bm, sx, py, W, thick, rollRel, true, 0);
}

}  // namespace sim
