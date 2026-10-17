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
  const int rows = ink ? (int)thick : 1;
  for (int oy = -grow; oy <= grow; oy++)
    for (int ox = -grow; ox <= grow; ox++) {
      // canopy: with ink a stack of arches, each one pixel lower, else just the top edge and the bottom edge
      for (int k = 0; k <= (ink ? rows : 1); k++) {
        const float dy = ink ? (float)k : (k ? thick : 0.0f);
        int lx = 0, ly = 0;
        for (int i = -N; i <= N; i++) {
          const float u = (float)i / N;
          const float yy = py + arch(u) + dy * (1.0f - 0.35f * fabsf(u));
          int x, y;
          r.pt(px + W * u, yy, &x, &y);
          if (i > -N) line(bm, lx + ox, ly + oy, x + ox, y + oy, m);
          lx = x; ly = y;
        }
      }
      // lines from the underside of the canopy to the harness
      for (int i = -N; i <= N; i += 4) {
        const float u = (float)i / N;
        int x, y;
        r.pt(px + W * u, py + arch(u) + thick * (1.0f - 0.35f * fabsf(u)), &x, &y);
        line(bm, x + ox, y + oy, hx + ox, hy + oy, m);
      }
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
