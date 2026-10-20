#include "platform/util/hot.h"
HOT_FILE_O3
#include "platform/gfx3d/chase.h"
#include <math.h>
#include "platform/gfx/shapes.h"

namespace gfx3d {

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
  auto arch = [&](float u) { return -L - 0.09f * W * (1.0f - u * u) + 0.30f * W * u * u * u * u; };   // tips down
  // The pilot lies in a pod harness: seen from behind-above, a streamlined egg (helmet on top, shoulders, back, tapering tail
  // where the legs and the speed bag are) hanging from two carabiners at the shoulders. pod[k] = {half width, y} down the body.
  const float podY[4] = {0.07f, 0.19f, 0.32f, 0.46f}, podW[4] = {0.12f, 0.155f, 0.13f, 0.05f};
  int hx, hy, ax, ay, bxx, byy;                           // harness centre, left and right carabiner
  r.pt(px, py, &hx, &hy);
  r.pt(px - 0.09f * W, py + 0.08f * W, &ax, &ay);
  r.pt(px + 0.09f * W, py + 0.08f * W, &bxx, &byy);
  int pl[4], pt[4], pr[4], ps[4];                         // pod outline: left x, y; right x, y at the four stations
  int plx[4], ply[4], prx[4], pry[4];
  for (int k = 0; k < 4; k++) {
    r.pt(px - podW[k] * W, py + podY[k] * W, &plx[k], &ply[k]);
    r.pt(px + podW[k] * W, py + podY[k] * W, &prx[k], &pry[k]);
  }
  (void)pl; (void)pt; (void)pr; (void)ps;
  int tx[2 * N + 1], ty[2 * N + 1], bx[2 * N + 1], by[2 * N + 1];       // the canopy's top and bottom edge
  for (int i = -N; i <= N; i++) {
    const float u = (float)i / N, shape = 1.0f - 0.55f * u * u;
    r.pt(px + W * u, py + arch(u), &tx[i + N], &ty[i + N]);
    r.pt(px + W * u, py + arch(u) + thick * shape, &bx[i + N], &by[i + N]);
  }
  for (int i = 0; i < 2 * N; i++) {                                     // canopy fill; the halo is the same fill one pixel taller and wider
    const int qx[4] = {tx[i], tx[i + 1], bx[i + 1], bx[i]};
    const int qy[4] = {ty[i], ty[i + 1], by[i + 1], by[i]};
    fillQuad(bm, qx, qy, m, ink ? 0 : grow, !ink && grow && i == 0, !ink && grow && i == 2 * N - 1);
  }
  // lines from the underside of the canopy to the carabiners (the left half to the left one, the right half to the right one)
  for (int i = -N; i <= N; i += 4) lineGrown(bm, bx[i + N], by[i + N], i < 0 ? ax : (i > 0 ? bxx : hx), i < 0 ? ay : (i > 0 ? byy : hy), grow, m);
  for (int k = 0; k < 3; k++) {                           // the pod: three quads, wider at the back, narrow at the tail
    const int qx[4] = {plx[k], prx[k], prx[k + 1], plx[k + 1]};
    const int qy[4] = {ply[k], pry[k], pry[k + 1], ply[k + 1]};
    fillQuad(bm, qx, qy, m, ink ? 0 : grow, false, false);
  }
  fillDisc(bm, hx, hy + 0, (int)lroundf(0.07f * W) + (ink ? 0 : grow), m);          // helmet
  if (ink) {                                                         // the wing's colour scheme: a panel line sweeping from the trailing edge up to the leading edge at each tip
    int px0 = 0, py0 = 0;
    for (int i = 0; i <= 2 * N; i++) {                              // its boundary: a line from the trailing edge toward the leading edge at the tip
      const float f = clampf((fabsf((float)(i - N) / N) - 0.35f) / 0.65f, 0.0f, 1.0f);
      const int yy = ty[i] + (int)lroundf((by[i] - ty[i]) * (1.0f - 0.7f * f));
      if (i > 0 && i != N + 1 && (f > 0.0f || i < N)) line(bm, px0, py0, tx[i], yy, DRAW_CLEAR);
      px0 = tx[i]; py0 = yy;
    }
  }
  if (ink) {                                                         // cell seams of the canopy, black on the white fill
    for (int i = -7; i <= 7; i += 2) {
      const float u = (float)i / N * 1.0f * 0.75f;
      int x0, y0, x1, y1;
      r.pt(px + W * u, py + arch(u) + 1.0f, &x0, &y0);
      r.pt(px + W * u, py + arch(u) + thick * (1.0f - 0.55f * u * u) - 1.0f, &x1, &y1);
      line(bm, x0, y0, x1, y1, DRAW_CLEAR);
    }
    int mx0, my0, mx1, my1;                                        // pod: the spine seam and the neck line under the helmet
    r.pt(px, py + 0.15f * W, &mx0, &my0);
    r.pt(px, py + 0.42f * W, &mx1, &my1);
    line(bm, mx0, my0, mx1, my1, DRAW_CLEAR);
    line(bm, plx[0], ply[0], prx[0], pry[0], DRAW_CLEAR);
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
  const float thick = 7.0f + 6.0f * clampf((kmh - Polar::kTrimKmh) / (Polar::kBarKmh - Polar::kTrimKmh), -0.3f, 1.0f);
  const float rollRel = g.bank - cam.roll;
  const float py = sy + 14.0f;
  sprite(bm, sx, py, W, thick, rollRel, false, 1);           // black outline
  sprite(bm, sx, py, W, thick, rollRel, true, 0);
}

}  // namespace gfx3d
