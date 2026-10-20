#include "platform/util/hot.h"
HOT_FILE_O3
#include "apps/compass3d/compass3d.h"
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"
#include "platform/gfx/strokes.h"
#include "platform/util/angle.h"
#include "platform/util/fastmem.h"

namespace compass3d {

static const float PI_F = 3.14159265f;

// ---- the stroked glyphs of the card (x 0..6, y 0..10 downward; -1 lifts the pen, -2 ends) -------------------------------
static const int8_t G_0[] = {1, 0, 5, 0, 6, 1, 6, 9, 5, 10, 1, 10, 0, 9, 0, 1, 1, 0, -2};
static const int8_t G_1[] = {1, 2, 3, 0, 3, 10, -2};
static const int8_t G_2[] = {0, 2, 1, 0, 5, 0, 6, 2, 6, 4, 0, 10, 6, 10, -2};
static const int8_t G_3[] = {0, 1, 1, 0, 5, 0, 6, 1, 6, 4, 5, 5, 3, 5, -1, 5, 5, 6, 6, 6, 9, 5, 10, 1, 10, 0, 9, -2};
static const int8_t G_4[] = {5, 10, 5, 0, 0, 7, 6, 7, -2};
static const int8_t G_5[] = {6, 0, 0, 0, 0, 5, 4, 4, 6, 6, 6, 9, 5, 10, 1, 10, 0, 9, -2};
static const int8_t G_6[] = {5, 0, 1, 0, 0, 1, 0, 9, 1, 10, 5, 10, 6, 9, 6, 6, 5, 5, 0, 5, -2};
static const int8_t G_7[] = {0, 0, 6, 0, 2, 10, -2};
static const int8_t G_8[] = {1, 0, 5, 0, 6, 1, 6, 4, 5, 5, 1, 5, 0, 4, 0, 1, 1, 0, -1, 1, 5, 0, 6, 0, 9, 1, 10, 5, 10, 6, 9, 6, 6, 5, 5, -2};
static const int8_t G_9[] = {1, 10, 5, 10, 6, 9, 6, 1, 5, 0, 1, 0, 0, 1, 0, 4, 1, 5, 6, 5, -2};
static const int8_t G_N[] = {0, 10, 0, 0, 6, 10, 6, 0, -2};
static const int8_t G_E[] = {6, 0, 0, 0, 0, 10, 6, 10, -1, 0, 5, 4, 5, -2};
static const int8_t G_S[] = {6, 1, 5, 0, 1, 0, 0, 1, 0, 4, 1, 5, 5, 5, 6, 6, 6, 9, 5, 10, 1, 10, 0, 9, -2};
static const int8_t G_W[] = {0, 0, 1, 10, 3, 3, 5, 10, 6, 0, -2};
static const int8_t* glyphOf(char c) {
  switch (c) {
    case '0': return G_0; case '1': return G_1; case '2': return G_2; case '3': return G_3; case '4': return G_4;
    case '5': return G_5; case '6': return G_6; case '7': return G_7; case '8': return G_8; case '9': return G_9;
    case 'N': return G_N; case 'E': return G_E; case 'S': return G_S; case 'W': return G_W;
    default: return nullptr;
  }
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int roundi(float v) { return (int)(v + 4096.5f) - 4096; }          // round to nearest without libm (|v| < 4096)

// The fixed light, in the camera frame: from the upper left, in front.
static const float kLx = -0.451f, kLy = 0.551f, kLz = 0.702f;

// Pure stuff for the pictograms and the vial.
enum Op { OP_WRITE = 0, OP_OR = 1, OP_AND = 2 };

Compass3d::Compass3d() {
  for (int i = 0; i <= kSeg; i++) {
    const float a = (float)i * (2.0f * PI_F / kSeg);
    sa_[i] = sinf(a);
    ca_[i] = cosf(a);
  }
  for (int lv = 0; lv <= 16; lv++)
    for (int xm = 0; xm < 8; xm++) {
      uint8_t b = 0;
      for (int k = 0; k < 8; k++)                                   // native bit k of any byte is logical y = 399 - 8 b - k: y & 7 = 7 - k
        if (kBayer8[((7 - k) << 3) | xm] < 4 * lv) b |= (uint8_t)(1u << k);
      pat_[lv][xm] = b;
    }
  reset();
}

void Compass3d::reset() {
  in_ = Input();
  qc_[0] = 1; qc_[1] = qc_[2] = qc_[3] = 0;
  wc_[0] = wc_[1] = wc_[2] = 0;
  yawWobble_ = 0;
  greyed_ = 1.0f;
  simMs_ = 0;
  snap_ = true;
  lastTenths_ = -2;
  lastTemp10_ = lastRh_ = -999999;
}

// ---- physics -------------------------------------------------------------------------------------------------------------
static inline void qmul(const float a[4], const float b[4], float o[4]) {
  o[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
  o[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
  o[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
  o[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

void Compass3d::step(const Input& in, uint32_t dtMs, uint32_t simMs) {
  in_ = in;
  simMs_ = simMs;
  float dt = (float)dtMs * 0.001f;
  if (dt < 0.001f) dt = 0.001f;
  if (dt > 0.05f) dt = 0.05f;
  float qt[4] = {in.q[0], in.q[1], in.q[2], in.q[3]};
  bool ok = in.valid;
  if (ok) {
    const float n2 = qt[0] * qt[0] + qt[1] * qt[1] + qt[2] * qt[2] + qt[3] * qt[3];
    ok = isfinite(n2) && n2 > 0.25f && n2 < 4.0f;                       // a degenerate attitude is no attitude
    if (ok) { const float k = 1.0f / sqrtf(n2); for (int i = 0; i < 4; i++) qt[i] *= k; }
  }
  const float trust = isfinite(in.trust) ? clampf(in.trust, 0.0f, 1.0f) : 1.0f;
  const float dist = ok ? clampf((0.8f - trust) * 1.25f, 0.0f, 1.0f) : 0.0f;   // 0 clean .. 1 rejected
  const float greyTarget = ok ? dist : 1.0f;
  greyed_ += 0.12f * (greyTarget - greyed_);
  if (!ok) return;                                                      // the card rests where it was
  // A disturbed field shakes the card about the vertical (the reading stays exact).
  const float t = (float)simMs;
  yawWobble_ = dist * 0.16f * (sinf(t * 0.0113f) + 0.5f * sinf(t * 0.0271f + 1.0f));
  const float h = 0.5f * yawWobble_;
  const float qy[4] = {cosf(h), 0, 0, sinf(h)};
  float tg[4];
  qmul(qy, qt, tg);
  if (snap_) {
    for (int i = 0; i < 4; i++) qc_[i] = tg[i];
    wc_[0] = wc_[1] = wc_[2] = 0;
    snap_ = false;
    return;
  }
  // e = target * conj(card): the world-frame rotation that takes the card to the target; its vector part (x2) is the spring's pull.
  const float cc[4] = {qc_[0], -qc_[1], -qc_[2], -qc_[3]};
  float e[4];
  qmul(tg, cc, e);
  const float sgn = e[0] < 0 ? -2.0f : 2.0f;
  const float K = kOmega * kOmega, C = 2.0f * kZeta * kOmega;
  for (int i = 0; i < 3; i++) {
    wc_[i] += (K * sgn * e[i + 1] - C * wc_[i]) * dt;
    wc_[i] = clampf(wc_[i], -40.0f, 40.0f);
  }
  const float dq[4] = {1.0f, 0.5f * wc_[0] * dt, 0.5f * wc_[1] * dt, 0.5f * wc_[2] * dt};
  float nq[4];
  qmul(dq, qc_, nq);
  const float n2 = nq[0] * nq[0] + nq[1] * nq[1] + nq[2] * nq[2] + nq[3] * nq[3];
  const float k = 1.0f / sqrtf(n2);
  for (int i = 0; i < 4; i++) qc_[i] = nq[i] * k;
}

// ---- camera --------------------------------------------------------------------------------------------------------------
Compass3d::Frame Compass3d::frame(bool clampTilt) const {
  const float w = qc_[0], x = qc_[1], y = qc_[2], z = qc_[3];
  // rows: world east, north, up in device coordinates (Ahrs::rotationMatrix)
  const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                         {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                         {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
  static const float cb = 0.93937f, sb = 0.34290f;                   // cos / sin of kBias (0.35 rad)
  Frame f;
  float* o[3] = {&f.ex, &f.nx, &f.ux};
  for (int i = 0; i < 3; i++) {
    o[i][0] = r[i][0];
    o[i][1] = r[i][1] * cb + r[i][2] * sb;                              // seen from below the bottom edge: the top of the card recedes
    o[i][2] = -r[i][1] * sb + r[i][2] * cb;
  }
  f.tx = r[2][0]; f.ty = r[2][1]; f.tz = r[2][2];
  // The drawn tilt is soft-limited to kTiltMax: past 28 degrees it eases toward the limit (d / (d + 10)), so the card turns smoothly
  // from a tilting card into a card that stays put while the dome still shows it from above. The bubble keeps the real tilt.
  const float sxy = sqrtf(f.ux * f.ux + f.uy * f.uy);
  const float theta = atan2f(sxy, f.uz) * 57.29578f;
  f.cue = clampf((theta - 36.0f) / 14.0f, 0.0f, 1.0f);
  if (clampTilt && theta > 28.0f) {
    const float d = theta - 28.0f, t2 = 28.0f + (kTiltMax - 28.0f) * d / (d + (kTiltMax - 28.0f));
    const float psi = (t2 - theta) * 0.0174533f, cp = cosf(psi), sp = sinf(psi);
    const float hx = sxy > 1e-4f ? f.ux / sxy : 0.0f, hy = sxy > 1e-4f ? f.uy / sxy : 1.0f;   // the way the card leans on the screen
    float* v[3] = {&f.ex, &f.nx, &f.ux};
    for (int i = 0; i < 3; i++) {
      const float p = v[i][0] * hx + v[i][1] * hy, z = v[i][2];
      const float pn = p * cp + z * sp;
      v[i][0] += (pn - p) * hx;
      v[i][1] += (pn - p) * hy;
      v[i][2] = z * cp - p * sp;
    }
  }
  const float nl = f.ux * kLx + f.uy * kLy + f.uz * kLz;
  f.lum = clampf(0.5f + 0.5f * nl, 0.0f, 1.0f) * (1.0f - 0.5f * greyed_);
  return f;
}

float Compass3d::cardTiltDeg() const {
  const Frame f = frame();
  return atan2f(sqrtf(f.ux * f.ux + f.uy * f.uy), f.uz) * 57.29578f;
}

void Compass3d::axes(float e[3], float n[3], float u[3]) const {
  const Frame f = frame(false);                                       // the real attitude, as the AHRS has it
  e[0] = f.ex; e[1] = f.ey; e[2] = f.ez;
  n[0] = f.nx; n[1] = f.ny; n[2] = f.nz;
  u[0] = f.ux; u[1] = f.uy; u[2] = f.uz;
}

bool Compass3d::topFacesViewer() const { return frame().uz > 0; }

bool Compass3d::proj(const Frame& f, float u, float v, float z, int16_t* sx, int16_t* sy) const {
  const float X = u * f.ex + v * f.nx + z * f.ux;
  const float Y = u * f.ey + v * f.ny + z * f.uy;
  const float Z = u * f.ez + v * f.nz + z * f.uz;
  float den = kDist - Z;
  bool ok = true;
  stats_.proj++;
  if (den < 0.4f) { den = 0.4f; ok = false; }
  const float s = kCardPx * kDist / den;
  *sx = (int16_t)clampi(roundi((float)DCX + X * s), -2000, 2000);
  *sy = (int16_t)clampi(roundi((float)DCY - Y * s), -2000, 2000);
  return ok;
}

bool Compass3d::project(float u, float v, float z, int* sx, int* sy) const {
  const Frame f = frame();
  int16_t a, b;
  const bool ok = proj(f, u, v, z, &a, &b);
  *sx = a; *sy = b;
  return ok;
}

void Compass3d::ringPoints(const Frame& f, float r, float z, int16_t* xs, int16_t* ys) const {
  for (int i = 0; i < kSeg; i++) proj(f, r * sa_[i], r * ca_[i], z, &xs[i], &ys[i]);
}

// ---- raster: native-row runs ---------------------------------------------------------------------------------------------
// A vertical run of logical column x from y0 to y1 (inclusive): in the portrait panel that is a run of bits in one native row.
void Compass3d::col(const Bitmap1& bm, int x, int y0, int y1, int level, int op) const {
  if ((unsigned)x >= (unsigned)W) return;
  if (y0 < 0) y0 = 0;
  if (y1 > H - 1) y1 = H - 1;
  if (y0 > y1) return;
  const int n0 = 399 - y1, n1 = 399 - y0;
  uint8_t* row = bm.buf + (size_t)x * bm.stride;
  const uint8_t p = pat_[level][x & 7];
  const int b0 = n0 >> 3, b1 = n1 >> 3;
  stats_.cols++;
  stats_.bytes += (uint32_t)(b1 - b0 + 1);
  const uint8_t first = (uint8_t)(0xFFu << (n0 & 7)), last = (uint8_t)(0xFFu >> (7 - (n1 & 7)));
  auto apply = [&](uint8_t& v, uint8_t m) {
    if (op == OP_WRITE) v = (uint8_t)((v & ~m) | (p & m));
    else if (op == OP_OR) v = (uint8_t)(v | (p & m));
    else v = (uint8_t)(v & (uint8_t)(~m | p));
  };
  if (b0 == b1) { apply(row[b0], (uint8_t)(first & last)); return; }
  apply(row[b0], first);
  if (op == OP_WRITE) { if (b1 > b0 + 1) fastFill(row + b0 + 1, p, (size_t)(b1 - b0 - 1)); }
  else for (int b = b0 + 1; b < b1; b++) apply(row[b], 0xFF);
  apply(row[b1], last);
}

// Fill a convex polygon (screen pixels) with a dither level, one run per column.
void Compass3d::fillPoly(const Bitmap1& bm, const int16_t* xs, const int16_t* ys, int n, int level, int op) {
  if (n < 3) return;
  int xmin = 100000, xmax = -100000;
  stats_.polys++;
  for (int i = 0; i < n; i++) { if (xs[i] < xmin) xmin = xs[i]; if (xs[i] > xmax) xmax = xs[i]; }
  if (xmin < 0) xmin = 0;
  if (xmax > W - 1) xmax = W - 1;
  if (xmin > xmax) return;
  for (int x = xmin; x <= xmax; x++) { lo_[x] = 32767; hi_[x] = -32768; }
  for (int i = 0; i < n; i++) {
    int ax = xs[i], ay = ys[i], bx = xs[i + 1 == n ? 0 : i + 1], by = ys[i + 1 == n ? 0 : i + 1];
    if (ax > bx) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    if (bx < 0 || ax > W - 1) continue;
    if (ax == bx) {
      const int lo = ay < by ? ay : by, hi = ay < by ? by : ay;
      if (lo < lo_[ax]) lo_[ax] = (int16_t)lo;
      if (hi > hi_[ax]) hi_[ax] = (int16_t)hi;
      continue;
    }
    const int32_t step = (int32_t)(((int64_t)(by - ay) * 65536) / (bx - ax));
    int32_t y16 = ay * 65536 + 32768;
    int x = ax;
    if (x < 0) { y16 += step * (-x); x = 0; }
    const int xe = bx > W - 1 ? W - 1 : bx;
    for (; x <= xe; x++, y16 += step) {
      const int y = (int)(y16 >> 16);
      if (y < lo_[x]) lo_[x] = (int16_t)y;
      if (y > hi_[x]) hi_[x] = (int16_t)y;
    }
  }
  for (int x = xmin; x <= xmax; x++)
    if (lo_[x] <= hi_[x]) col(bm, x, lo_[x], hi_[x], level, op);
}

// Ring of the screen (centre, radii) as column runs.
void Compass3d::annulus(const Bitmap1& bm, int cx, int cy, int ro, int ri, int level, int op) {
  for (int x = cx - ro; x <= cx + ro; x++) {
    const int dx = x - cx;
    const int so = (int)sqrtf((float)(ro * ro - dx * dx) + 0.5f);
    if (ri > 0 && dx > -ri && dx < ri) {
      const int si = (int)sqrtf((float)(ri * ri - dx * dx) + 0.5f);
      col(bm, x, cy - so, cy - si - 1, level, op);
      col(bm, x, cy + si + 1, cy + so, level, op);
    } else {
      col(bm, x, cy - so, cy + so, level, op);
    }
  }
}

void Compass3d::segment(const Bitmap1& bm, int x0, int y0, int x1, int y1, bool thick, DrawMode m) const {
  line(bm, x0, y0, x1, y1, m);
  if (!thick) return;
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
  if (dy > dx) line(bm, x0 + 1, y0, x1 + 1, y1, m); else line(bm, x0, y0 + 1, x1, y1 + 1, m);
}

// ---- cached pictures -----------------------------------------------------------------------------------------------------
void Compass3d::build(const Bitmap1& bm) {
  (void)bm;
  buildDigits();
  buildPlate();
  built_ = true;
}

void Compass3d::buildDigits() {
  Bitmap1 tmp{digitTmp_, kDigitW, kDigitH, (uint16_t)((kDigitW + 7) / 8), 0};
  const struct { int r; float w, h; } S = {3, 24.0f, 46.0f};
  for (int g = 0; g < 12; g++) {
    memset(digitTmp_, 0, sizeof digitTmp_);
    strokes::Pen p(tmp, S.r, DRAW_SET);
    if (g < 10) strokes::strokeDigit(p, 4, 4, S.w, S.h, g);
    else if (g == 10) fillDisc(tmp, kDigitW / 2, 4 + (int)S.h - 1, 4, DRAW_SET);       // the decimal point
    else strokes::strokeDigit(p, 4, 4, S.w, S.h, -1);                                    // a dash
    digit_[g].build(digitMem_[g], kDigitW, kDigitH, 1,
                    [](void* c, int x, int y) { return static_cast<Bitmap1*>(c)->getPixel(x, y) != 0; }, &tmp);
  }
}

void Compass3d::buildPlate() {
  Bitmap1 pb{plate_, 400, 240, 50, 1};
  pb.fill(DRAW_SET);
  // the liquid under the glass, lit like a bowl: nested discs whose centres drift to the upper left as they get lighter
  fillDisc(pb, DCX, DCY, RG, Gray(4));
  for (int k = 1; k <= 13; k++)
    fillDisc(pb, DCX - (k * 22) / 10, DCY - (k * 28) / 10, RG - (k * 62) / 10, Gray(4 + (k * 7 + 6) / 13));
  // the bezel: two bevels lit from the upper left
  const int N = 48;
  for (int j = 0; j < N; j++) {
    const float a0 = (float)j * (2.0f * PI_F / N), a1 = (float)(j + 1) * (2.0f * PI_F / N), am = 0.5f * (a0 + a1);
    const float lit = -0.7071f * cosf(am) + 0.7071f * sinf(am);                       // outward normal . light (x right, y up)
    int16_t xs[4], ys[4];
    for (int b = 0; b < 2; b++) {
      const int rin = b == 0 ? 109 : RG, rout = b == 0 ? RB : 109;
      const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
      xs[0] = (int16_t)roundi(DCX + rin * c0);  ys[0] = (int16_t)roundi(DCY - rin * s0);
      xs[1] = (int16_t)roundi(DCX + rout * c0); ys[1] = (int16_t)roundi(DCY - rout * s0);
      xs[2] = (int16_t)roundi(DCX + rout * c1); ys[2] = (int16_t)roundi(DCY - rout * s1);
      xs[3] = (int16_t)roundi(DCX + rin * c1);  ys[3] = (int16_t)roundi(DCY - rin * s1);
      const float v = b == 0 ? 9.5f + 6.0f * lit : 8.0f - 5.0f * lit;                // the outer bevel faces out, the inner one in
      fillPoly(pb, xs, ys, 4, clampi(roundi(v), 1, 16), OP_WRITE);
    }
  }
  circle(pb, DCX, DCY, RB, DRAW_CLEAR);
  circle(pb, DCX, DCY, RB - 1, DRAW_CLEAR);
  circle(pb, DCX, DCY, 109, DRAW_CLEAR);
  circle(pb, DCX, DCY, RG + 1, DRAW_CLEAR);
  circle(pb, DCX, DCY, RG, DRAW_CLEAR);
  for (int j = 0; j < 36; j++) {                                                        // engraved marks on the outer bevel
    const float a = (float)j * (PI_F / 18.0f);
    const int r0 = 111, r1 = j % 9 == 0 ? 115 : 113;
    line(pb, roundi(DCX + r0 * sinf(a)), roundi(DCY - r0 * cosf(a)), roundi(DCX + r1 * sinf(a)), roundi(DCY - r1 * cosf(a)), DRAW_CLEAR);
  }
  // heading window
  ui::roundFrame(pb, 6, 264, 228, 66, 10, 2, DRAW_CLEAR);
  pb.fillRect(12, 270, 216, 54, DRAW_SET);
  pb.hline(12, 227, 270, DRAW_CLEAR);                                                   // a recessed window: shadow along the top and left
  pb.hline(12, 227, 271, DRAW_CLEAR);
  pb.vline(12, 270, 323, DRAW_CLEAR);
  pb.vline(13, 272, 323, DRAW_CLEAR);
  // vial and the weather cells
  fillDisc(pb, kVialX, kVialY, 28, DRAW_CLEAR);
  fillDisc(pb, kVialX, kVialY, 26, DRAW_SET);
  fillDisc(pb, kVialX, kVialY, 26, Gray(13));
  circle(pb, kVialX, kVialY, 14, DRAW_CLEAR);
  circle(pb, kVialX, kVialY, 4, DRAW_CLEAR);
  line(pb, kVialX - 26, kVialY, kVialX - 19, kVialY, DRAW_CLEAR); line(pb, kVialX + 19, kVialY, kVialX + 26, kVialY, DRAW_CLEAR);
  line(pb, kVialX, kVialY - 26, kVialX, kVialY - 19, DRAW_CLEAR); line(pb, kVialX, kVialY + 19, kVialX, kVialY + 26, DRAW_CLEAR);
  ui::roundFrame(pb, 76, 338, 158, 56, 8, 2, DRAW_CLEAR);
  pb.vline(155, 344, 388, DRAW_CLEAR);
}

// ---- the scene -----------------------------------------------------------------------------------------------------------
void Compass3d::glyphs(const Bitmap1& bm, const Frame& f, int seg, const char* text, float rc, float height, bool thick) {
  const float sa = sa_[seg], ca = ca_[seg];
  const float cu = rc * sa, cv = rc * ca;
  const float tu = ca, tv = -sa, ou = sa, ov = ca;                                      // right (clockwise) and outward on the card
  const float unit = height * 0.1f;
  int n = 0;
  while (text[n]) n++;
  const float half = 0.5f * (7 * n - 1);
  for (int k = 0; k < n; k++) {
    const int8_t* g = glyphOf(text[k]);
    if (!g) continue;
    int px = 0, py = 0;
    bool down = false;
    while (*g != -2) {
      if (*g == -1) { down = false; g++; continue; }
      const float lx = ((float)(k * 7 + g[0]) - half) * unit, ly = (5.0f - (float)g[1]) * unit;
      int16_t sx, sy;
      proj(f, cu + tu * lx + ou * ly, cv + tv * lx + ov * ly, 0.0f, &sx, &sy);
      if (down) segment(bm, px, py, sx, sy, thick, DRAW_CLEAR);
      px = sx; py = sy; down = true;
      g += 2;
    }
  }
}

void Compass3d::cardTop(const Bitmap1& bm, const Frame& f) {
  const int paper = clampi(roundi(3.0f + 13.5f * f.lum), 4, 16);
  const int ringL = clampi(paper - 4, 2, 16);
  ringPoints(f, 1.0f, 0.0f, px_, py_);
  fillPoly(bm, px_, py_, kSeg, ringL, OP_WRITE);
  ringPoints(f, 0.80f, 0.0f, qx_, qy_);
  fillPoly(bm, qx_, qy_, kSeg, paper, OP_WRITE);
  for (int i = 0; i < kSeg; i++) {                                                      // rim and the inner ring line
    const int j = i + 1 == kSeg ? 0 : i + 1;
    segment(bm, px_[i], py_[i], px_[j], py_[j], true, DRAW_CLEAR);
    line(bm, qx_[i], qy_[i], qx_[j], qy_[j], DRAW_CLEAR);
  }
  // graduations: every 5 degrees, longer at 10, longest at 30
  for (int i = 0; i < kSeg; i++) {
    const bool big = i % 6 == 0, mid = i % 2 == 0;
    const float r1 = 0.97f, r0 = r1 - (big ? 0.13f : mid ? 0.08f : 0.045f);
    int16_t ax, ay, bx, by;
    proj(f, r0 * sa_[i], r0 * ca_[i], 0.0f, &ax, &ay);
    proj(f, r1 * sa_[i], r1 * ca_[i], 0.0f, &bx, &by);
    segment(bm, ax, ay, bx, by, big, DRAW_CLEAR);
  }
  // numerals every 30 degrees (in tens of degrees), the four letters inside
  static const char* kNum[12] = {"", "3", "6", "", "12", "15", "", "21", "24", "", "30", "33"};
  for (int k = 0; k < 12; k++)
    if (kNum[k][0]) glyphs(bm, f, k * 6, kNum[k], 0.705f, 0.15f, false);
  // the rose
  const float tips[2] = {0.34f, 0.21f};
  for (int ring = 1; ring >= 0; ring--)
    for (int k = 0; k < 4; k++) {
      const int seg = ring == 0 ? k * 18 : k * 18 + 9;
      const float tipR = ring == 0 ? tips[0] : tips[1], sideR = ring == 0 ? 0.075f : 0.05f;
      const int sl = (seg + kSeg - 9) % kSeg, sr = (seg + 9) % kSeg;
      int16_t xs[3], ys[3];
      proj(f, tipR * sa_[seg], tipR * ca_[seg], 0.0f, &xs[0], &ys[0]);
      proj(f, sideR * sa_[sl], sideR * ca_[sl], 0.0f, &xs[1], &ys[1]);
      proj(f, 0, 0, 0.0f, &xs[2], &ys[2]);
      fillPoly(bm, xs, ys, 3, ring == 0 ? 3 : 7, OP_WRITE);                              // the shadowed half
      proj(f, sideR * sa_[sr], sideR * ca_[sr], 0.0f, &xs[1], &ys[1]);
      fillPoly(bm, xs, ys, 3, 16, OP_WRITE);                                             // the lit half
      segment(bm, xs[0], ys[0], xs[1], ys[1], false, DRAW_CLEAR);
      proj(f, sideR * sa_[sl], sideR * ca_[sl], 0.0f, &xs[1], &ys[1]);
      segment(bm, xs[0], ys[0], xs[1], ys[1], false, DRAW_CLEAR);
      segment(bm, xs[1], ys[1], xs[2], ys[2], false, DRAW_CLEAR);
    }
  static const char kCard[4] = {'N', 'E', 'S', 'W'};
  for (int k = 0; k < 4; k++) {
    char s[2] = {kCard[k], 0};
    glyphs(bm, f, k * 18, s, 0.555f, k == 0 ? 0.27f : 0.23f, true);
  }
  north(bm, f);
}

// The north marker: a dark dithered arrowhead on the rim, pointing at the hub.
void Compass3d::north(const Bitmap1& bm, const Frame& f) {
  int16_t xs[3], ys[3];
  proj(f, 0, 0.80f, 0.0f, &xs[0], &ys[0]);
  proj(f, -0.085f, 0.99f, 0.0f, &xs[1], &ys[1]);
  proj(f, 0.085f, 0.99f, 0.0f, &xs[2], &ys[2]);
  fillPoly(bm, xs, ys, 3, 4, OP_WRITE);
  for (int i = 0; i < 3; i++) {
    const int j = (i + 1) % 3;
    segment(bm, xs[i], ys[i], xs[j], ys[j], true, DRAW_CLEAR);
  }
}

void Compass3d::scene(const Bitmap1& bm, const Frame& f) {
  const bool top = f.uz > 0;
  const float h = kThick;
  ringPoints(f, 1.0f, 0.0f, px_, py_);                                                 // top rim
  ringPoints(f, 1.0f, -h, qx_, qy_);                                                   // bottom rim
  ringPoints(f, 1.0f, -0.5f * h, mx_, my_);                                            // the middle of the wall
  // soft shadow on the floor of the dome, cast away from the light
  {
    int16_t sx[kSeg], sy[kSeg];                                                         // 288 B on the stack
    for (int pass = 0; pass < 2; pass++) {
      const int ox = pass == 0 ? 6 : 3, oy = pass == 0 ? 9 : 5;
      for (int i = 0; i < kSeg; i++) { sx[i] = (int16_t)(qx_[i] + ox); sy[i] = (int16_t)(qy_[i] + oy); }
      fillPoly(bm, sx, sy, kSeg, pass == 0 ? 10 : 8, OP_AND);
    }
  }
  // the wall: quads facing the camera, lit from the upper left, darker toward the floor; graduations engraved
  bool vis[kSeg];
  for (int i = 0; i < kSeg; i++) {
    const int j = i + 1 == kSeg ? 0 : i + 1;
    const float ns = sa_[i] + sa_[i + 1], nc = ca_[i] + ca_[i + 1];                      // the middle direction, length 2 cos(2.5 deg)
    const float inv = 1.0f / sqrtf(ns * ns + nc * nc);
    const float us = ns * inv, vs = nc * inv;                                            // east / north of the outward normal
    const float nz = us * f.ez + vs * f.nz;
    vis[i] = nz * kDist > 1.0f;
    if (!vis[i]) continue;
    const float lum = clampf(0.5f + 0.5f * ((us * f.ex + vs * f.nx) * kLx + (us * f.ey + vs * f.ny) * kLy + nz * kLz), 0.0f, 1.0f);
    const int lvTop = clampi(roundi(2.0f + 12.5f * lum), 1, 15) , lvBot = clampi(lvTop - 3, 0, 16);
    int16_t xs[4] = {px_[i], px_[j], mx_[j], mx_[i]}, ys[4] = {py_[i], py_[j], my_[j], my_[i]};
    fillPoly(bm, xs, ys, 4, lvTop, OP_WRITE);
    int16_t xb[4] = {mx_[i], mx_[j], qx_[j], qx_[i]}, yb[4] = {my_[i], my_[j], qy_[j], qy_[i]};
    fillPoly(bm, xb, yb, 4, lvBot, OP_WRITE);
    if (i % 2 == 0) {                                                                    // a graduation on the wall, every 10 degrees
      const int mxp = i % 6 == 0 ? qx_[i] : (px_[i] + qx_[i]) / 2, myp = i % 6 == 0 ? qy_[i] : (py_[i] + qy_[i]) / 2 + 1;
      line(bm, px_[i], py_[i], mxp, myp, DRAW_CLEAR);
    }
  }
  for (int i = 0; i < kSeg; i++) {                                                       // wall outline: bottom edge and silhouettes
    const int j = i + 1 == kSeg ? 0 : i + 1, p = i == 0 ? kSeg - 1 : i - 1;
    if (vis[i]) segment(bm, qx_[i], qy_[i], qx_[j], qy_[j], true, DRAW_CLEAR);
    if (vis[i] != vis[p] && (vis[i] || vis[p])) line(bm, px_[i], py_[i], qx_[i], qy_[i], DRAW_CLEAR);
  }
  if (top) {
    cardTop(bm, f);
  } else {                                                                               // seen from below: the dark underside
    fillPoly(bm, qx_, qy_, kSeg, 6, OP_WRITE);
    for (int i = 0; i < kSeg; i++) {
      const int j = i + 1 == kSeg ? 0 : i + 1;
      segment(bm, qx_[i], qy_[i], qx_[j], qy_[j], true, DRAW_CLEAR);
    }
    for (int k = 0; k < 2; k++) {                                                        // turned rings on the base
      ringPoints(f, k == 0 ? 0.78f : 0.42f, -h, px_, py_);
      for (int i = 0; i < kSeg; i++) { const int j = i + 1 == kSeg ? 0 : i + 1; line(bm, px_[i], py_[i], px_[j], py_[j], DRAW_CLEAR); }
    }
    int16_t hx, hy;
    proj(f, 0, 0, -h, &hx, &hy);
    fillDisc(bm, hx, hy, 5, DRAW_SET);
    circle(bm, hx, hy, 5, DRAW_CLEAR);
  }
}

// ---- glass, lubber line ---------------------------------------------------------------------------------------------------
void Compass3d::glass(const Bitmap1& bm, const Frame& f) {
  (void)f;
  annulus(bm, DCX, DCY, RG - 1, RG - 8, 13, OP_AND);                                     // the dome curves away at the edge
  annulus(bm, DCX, DCY, RG - 1, RG - 3, 10, OP_AND);
  // reflections of a window at the upper left: a long tapered arc and a short one inside it
  for (int k = 0; k < 2; k++) {
    const int r = k == 0 ? RG - 9 : RG - 17;
    const float a0 = k == 0 ? 112.0f : 128.0f, a1 = k == 0 ? 178.0f : 165.0f;           // degrees, counter-clockwise from the right
    const int n = k == 0 ? 22 : 12;
    for (int i = 0; i <= n; i++) {
      const float t = (float)i / (float)n;
      const float a = (a0 + (a1 - a0) * t) * (PI_F / 180.0f);
      const float w = (k == 0 ? 2.4f : 1.4f) * (1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f)) + 0.3f;   // thickest in the middle
      const int x = DCX + roundi((float)r * cosf(a)), y = DCY - roundi((float)r * sinf(a));
      fillDisc(bm, x, y, roundi(w), DRAW_SET);
    }
  }
}

void Compass3d::lubber(const Bitmap1& bm) const {
  // the fixed index of the instrument: a white-edged black pointer from the bezel into the glass, at the top (the box's forward)
  const int cx = DCX, top = DCY - RG - 11, tip = DCY - RG + 26;
  for (int y = top; y <= tip; y++) {
    const int half = y < DCY - RG + 4 ? 8 : (tip - y) * 8 / (tip - (DCY - RG + 4));
    bm.hspan(cx - half - 2, y, 2 * half + 5, DRAW_SET);
  }
  for (int y = top + 1; y <= tip - 1; y++) {
    const int half = y < DCY - RG + 4 ? 8 : (tip - y) * 8 / (tip - (DCY - RG + 4));
    bm.hspan(cx - half, y, 2 * half + 1, DRAW_CLEAR);
  }
  bm.hspan(cx - 8, top + 1, 17, DRAW_CLEAR);
}

// ---- readouts ------------------------------------------------------------------------------------------------------------
// "-12.3": tenths with one decimal, "--" for not finite. No snprintf.
static const char* fmt1(char* b, size_t n, float v) {
  if (!isfinite(v) || v > 999.0f || v < -99.0f) { b[0] = '-'; b[1] = '-'; b[2] = 0; return b; }
  int t = roundi(v * 10.0f);
  size_t k = 0;
  if (t < 0) { b[k++] = '-'; t = -t; }
  const int ip = t / 10;
  if (ip >= 100) b[k++] = (char)('0' + ip / 100);
  if (ip >= 10) b[k++] = (char)('0' + (ip / 10) % 10);
  b[k++] = (char)('0' + ip % 10);
  b[k++] = '.';
  b[k++] = (char)('0' + t % 10);
  b[k] = 0;
  (void)n;
  return b;
}

void Compass3d::heading(const Bitmap1& bm) {
  const int tenths = in_.valid ? bearingTenths(in_.headingDeg) : -1;       // the vario's helper: 359.96 reads 0.0, never 360.0
  const int dotX = 156, y = 270;
  if (tenths != lastTenths_) {
    lastTenths_ = tenths;
    int k = 0;
    if (tenths < 0) {
      hdgGlyph_[k++] = 11; hdgGlyph_[k++] = 11; hdgGlyph_[k++] = 11;
      hdgGlyph_[k++] = 10; hdgGlyph_[k++] = 11;
    } else {
      const int ip = tenths / 10;
      if (ip >= 100) hdgGlyph_[k++] = (int8_t)(ip / 100);
      if (ip >= 10) hdgGlyph_[k++] = (int8_t)((ip / 10) % 10);
      hdgGlyph_[k++] = (int8_t)(ip % 10);
      hdgGlyph_[k++] = 10;
      hdgGlyph_[k++] = (int8_t)(tenths % 10);
    }
    hdgGlyph_[k] = -1;
  }
  int nInt = 0;
  while (hdgGlyph_[nInt] != 10) nInt++;
  int x = dotX - 9 - nInt * 36 - 1;                                          // the integer part ends 9 px before the point's centre
  for (int i = 0; hdgGlyph_[i] >= 0; i++) {
    const int g = hdgGlyph_[i];
    if (g == 10) { digit_[g].draw(bm, dotX - kDigitW / 2, y, DRAW_CLEAR); x = dotX + 9 - 1; continue; }
    digit_[g].draw(bm, x, y, DRAW_CLEAR);
    x += 36;
  }
  circle(bm, 213, 283, 6, DRAW_CLEAR);                                       // the degree sign
  circle(bm, 213, 283, 5, DRAW_CLEAR);
}

static const char* point8(float deg) {
  static const char* n[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const int tenths = bearingTenths(deg);
  if (tenths < 0) return "";
  return n[((tenths + 225) / 450) % 8];
}

void Compass3d::bubble(const Bitmap1& bm, const Frame& f) {
  // The bubble sits at the high side of the vial: toward where "up" leans in the device frame. Full travel at 15 degrees; beyond the
  // dome's tilt limit the cue fades in: a heavy rim on the vial and a bigger, thick-edged bubble pinned at the edge.
  const float mag = sqrtf(f.tx * f.tx + f.ty * f.ty);
  const float lim = 0.2588f;                                                 // sin 15 degrees
  const float k = mag > lim ? lim / mag : 1.0f;
  const int bx = kVialX + roundi(f.tx * k * (15.0f / lim)), by = kVialY - roundi(f.ty * k * (15.0f / lim));
  const int heavy = f.cue > 0.66f ? 2 : f.cue > 0.2f ? 1 : 0;
  if (heavy) {
    for (int r = kVialR + 1; r >= kVialR - 1 - heavy; r--) circle(bm, kVialX, kVialY, r, DRAW_CLEAR);
  }
  fillDisc(bm, bx, by, 9 + heavy, DRAW_CLEAR);
  fillDisc(bm, bx, by, 7, DRAW_SET);
  fillDisc(bm, bx, by, 7, Gray(heavy ? 5 : 11));
  bm.fillRect(bx - 3, by - 4, 3, 2, DRAW_SET);                               // a small glint
}

void Compass3d::badges(const Bitmap1& bm, uint32_t nowMs) {
  if (!in_.calibrated) {                                                     // a figure 8: move the unit like this
    const int cx = 214, cy = 13;
    int px = 0, py = 0;
    for (int i = 0; i <= 24; i++) {
      const float t = (float)i * (2.0f * PI_F / 24.0f);
      const int x = cx + roundi(18.0f * sinf(t)), y = cy + roundi(8.0f * sinf(2.0f * t));
      if (i) { line(bm, px, py, x, y, DRAW_CLEAR); line(bm, px, py + 1, x, y + 1, DRAW_CLEAR); }
      px = x; py = y;
    }
    const float ph = (float)(nowMs % 1800) * (2.0f * PI_F / 1800.0f);
    fillDisc(bm, cx + roundi(18.0f * sinf(ph)), cy + roundi(8.0f * sinf(2.0f * ph)), 4, DRAW_CLEAR);
    fillDisc(bm, cx + roundi(18.0f * sinf(ph)), cy + roundi(8.0f * sinf(2.0f * ph)), 2, DRAW_SET);
  }
  if (in_.valid && in_.trust < 0.8f) {                                       // a horseshoe magnet
    const int cx = 26, cy = 14;
    for (int a = 0; a <= 12; a++) {
      const float t = PI_F * (float)a / 12.0f;
      const int x = cx + roundi(10.0f * cosf(t)), y = cy - roundi(10.0f * sinf(t)) + 6;
      fillDisc(bm, x, y, 2, DRAW_CLEAR);
    }
    bm.fillRect(cx - 13, cy + 3, 6, 9, DRAW_CLEAR);
    bm.fillRect(cx + 7, cy + 3, 6, 9, DRAW_CLEAR);
    bm.fillRect(cx - 12, cy + 8, 4, 4, DRAW_SET);
    bm.fillRect(cx + 8, cy + 8, 4, 4, DRAW_SET);
  }
}

void Compass3d::readouts(const Bitmap1& bm, const Frame& f, uint32_t nowMs) {
  fonts_.ensure(bm.rotation);
  heading(bm);
  bubble(bm, f);
  badges(bm, nowMs);
  if (in_.valid) ui::centered(bm, fonts_.mid, 120, 21, point8(in_.headingDeg), DRAW_CLEAR);
  // weather cells
  const int t10 = isfinite(in_.tempC) ? roundi(in_.tempC * 10.0f) : -999998;
  const int rh = isfinite(in_.rhPct) ? roundi(in_.rhPct) : -999998;
  if (t10 != lastTemp10_) { lastTemp10_ = t10; fmt1(tempText_, sizeof tempText_, in_.tempC); }
  if (rh != lastRh_) {
    lastRh_ = rh;
    if (rh == -999998) { rhText_[0] = '-'; rhText_[1] = '-'; rhText_[2] = 0; }
    else { char* p = rhText_; if (rh >= 100) *p++ = (char)('0' + rh / 100); if (rh >= 10) *p++ = (char)('0' + (rh / 10) % 10); *p++ = (char)('0' + rh % 10); *p = 0; }
  }
  ui::centered(bm, fonts_.mid, 116, 380, tempText_, DRAW_CLEAR);
  ui::centered(bm, fonts_.small, 116, 359, "TEMP C", DRAW_CLEAR);
  ui::centered(bm, fonts_.mid, 195, 380, rhText_, DRAW_CLEAR);
  ui::centered(bm, fonts_.small, 195, 359, "RH %", DRAW_CLEAR);
}

void Compass3d::draw(const Bitmap1& bm, uint32_t nowMs) {
  if (bm.rotation != 1 || bm.w != 400 || bm.h != 240 || bm.stride != 50) return;
  if (!built_) build(bm);
  stats_ = Stats();
  fastCopy(bm.buf, plate_, sizeof plate_);
  const Frame f = frame();
  scene(bm, f);
  glass(bm, f);
  lubber(bm);
  readouts(bm, f, nowMs);
}

}  // namespace compass3d
