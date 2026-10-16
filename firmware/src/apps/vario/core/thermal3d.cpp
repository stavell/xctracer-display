#include "apps/vario/core/thermal3d.h"
#include <string.h>

static const float R2D = 57.29577951f;

static float wrap180(float a) {
  a = fmodf(a, 360.0f);
  if (a > 180) a -= 360;
  if (a < -180) a += 360;
  return a;
}

void ThermalColumn::reset() {
  n_ = 0;
  haveOrigin_ = false;
  rev_ = 0;
  analyzedRev_ = 0xffffffffu;
  headSet_ = false;
  headOk_ = false;
  clearResults();
}

void ThermalColumn::clearResults() {
  valid = false;
  circling = false;
  ageS = 0;
  nCircles = 0;
  nBands = 0;
  bandM = 50;
  leanValid = false;
  leanDeg = leanPer100 = leanRms = 0;
  coreX0 = coreY0 = coreZ0 = coreSx = coreSy = 0;
  trend = TREND_NONE;
  trendRatio = 0;
  zMin = zMax = 0;
  axisDeg = 90;
  axisKind = AXIS_EAST;
  p0_ = nP_ = 0;
}

// The column's origin is the first point; a flight that wanders more than 6 km from it moves the origin so the
// floats stay fine (1 mm at 6 km).
void ThermalColumn::rebase(double dx, double dy) {
  for (int i = 0; i < n_; i++) { pts_[i].gx -= (float)dx; pts_[i].gy -= (float)dy; }
  lon0_ += dx / kx_;
  lat0_ += dy / 111320.0;
}

void ThermalColumn::push(uint32_t clockMs, bool fix, double lat, double lon, float alt, float vario, float windE, float windN) {
  if (!fix || !isfinite(lat) || !isfinite(lon) || !isfinite(alt)) return;
  if (n_ > 0 && (int32_t)(clockMs - pts_[n_ - 1].tMs) <= 0) return;
  if (!haveOrigin_ || n_ == 0) {
    lat0_ = lat;
    lon0_ = lon;
    kx_ = 111320.0 * cos(lat * 0.017453292519943295);
    haveOrigin_ = true;
  }
  double gx = (lon - lon0_) * kx_, gy = (lat - lat0_) * 111320.0;
  if (fabs(gx) > 6000.0 || fabs(gy) > 6000.0) {
    rebase(gx, gy);
    gx = gy = 0;
  }
  if (n_ == MAXP) {
    memmove(pts_, pts_ + 1, sizeof(Pt) * (MAXP - 1));
    n_--;
  }
  Pt p;
  p.gx = (float)gx;
  p.gy = (float)gy;
  p.z = alt;
  p.v = isfinite(vario) ? vario : 0;
  p.hdg = NAN;
  p.rate = NAN;
  p.tMs = clockMs;
  if (n_ > 0) {
    const Pt& q = pts_[n_ - 1];
    const float dt = (clockMs - q.tMs) / 1000.0f;
    if (dt > 0 && dt <= 3.0f) {
      // the air-mass velocity: ground velocity minus the wind, so a circle turns at a steady rate in any wind
      const float vx = (p.gx - q.gx) / dt - windE, vy = (p.gy - q.gy) / dt - windN;
      if (hypotf(vx, vy) >= 1.5f) {
        p.hdg = atan2f(vx, vy) * R2D;
        if (isfinite(q.hdg)) p.rate = wrap180(p.hdg - q.hdg) / dt;
      }
    }
  }
  pts_[n_++] = p;
  rev_++;
}

void ThermalColumn::setHead(bool valid_, double lat, double lon, float alt, float vario) {
  headSet_ = valid_ && isfinite(lat) && isfinite(lon) && isfinite(alt) && haveOrigin_ && n_ > 0;
  headLat_ = lat;
  headLon_ = lon;
  headAlt_ = alt;
  headVario_ = isfinite(vario) ? vario : 0;
  placeHead();
}

void ThermalColumn::placeHead() {
  headOk_ = headSet_ && n_ > 0 && valid;
  if (!headOk_) return;
  hx_ = (float)((headLon_ - lon0_) * kx_) - pts_[n_ - 1].gx;
  hy_ = (float)((headLat_ - lat0_) * 111320.0) - pts_[n_ - 1].gy;
  hz_ = headAlt_;
  hv_ = headVario_;
  if (hypotf(hx_, hy_) > 60.0f) headOk_ = false;      // not the same place (data gap): do not draw a long line to it
}

// Centre, mean climb, best-lift point and core point of one circle (points a..b of pts_/ax_/ay_).
void ThermalColumn::circleMetrics(CoreCircle& c) const {
  const int a = c.a, b = c.b;
  const int last = b > a ? b - 1 : b;                     // the end point is the next circle's start
  float sx = 0, sy = 0;
  for (int k = a; k <= last; k++) { sx += ax_[k]; sy += ay_[k]; }
  c.cx = sx / (last - a + 1);
  c.cy = sy / (last - a + 1);
  float sz = 0, sv = 0;
  for (int k = a; k <= b; k++) { sz += pts_[k].z; sv += pts_[k].v; }
  c.z = sz / (b - a + 1);
  c.climb = sv / (b - a + 1);
  float best = -1e9f, wsum = 0, wx = 0, wy = 0;
  int bk = a;
  for (int k = a; k <= b; k++) {
    const float v = (pts_[k > a ? k - 1 : a].v + pts_[k].v + pts_[k < b ? k + 1 : b].v) / 3.0f;
    if (v > best) { best = v; bk = k; }
    if (v > 0) { wsum += v; wx += v * ax_[k]; wy += v * ay_[k]; }
  }
  c.bestClimb = best;
  c.bestX = ax_[bk];
  c.bestY = ay_[bk];
  float bd = atan2f(c.bestX - c.cx, c.bestY - c.cy) * R2D;
  c.bestDeg = bd < 0 ? bd + 360.0f : bd;
  // The core: where the lift is, weighted by the climb itself. Weak or sinking circles say nothing about it.
  if (wsum > 0 && c.climb >= 0.3f) {
    c.x = wx / wsum;
    c.y = wy / wsum;
  } else {
    c.x = c.cx;
    c.y = c.cy;
  }
}

namespace {
struct Run { int s, e; float ang; };

struct Fit {
  bool ok = false;
  float xm = 0, ym = 0, zm = 0, sx = 0, sy = 0;
};

// Least squares x(z), y(z) through the core points of circles [i0, i1) skipping those flagged in `skip`.
Fit fitLine(const CoreCircle* c, int i0, int i1, const bool* skip) {
  Fit f;
  int n = 0;
  float zlo = 1e9f, zhi = -1e9f;
  for (int i = i0; i < i1; i++) {
    if (skip && skip[i]) continue;
    n++;
    f.xm += c[i].x; f.ym += c[i].y; f.zm += c[i].z;
    if (c[i].z < zlo) zlo = c[i].z;
    if (c[i].z > zhi) zhi = c[i].z;
  }
  if (n < 3 || zhi - zlo < 30.0f) return f;
  f.xm /= n; f.ym /= n; f.zm /= n;
  float szz = 0, szx = 0, szy = 0;
  for (int i = i0; i < i1; i++) {
    if (skip && skip[i]) continue;
    const float dz = c[i].z - f.zm;
    szz += dz * dz;
    szx += dz * (c[i].x - f.xm);
    szy += dz * (c[i].y - f.ym);
  }
  if (szz < 1.0f) return f;
  f.sx = szx / szz;
  f.sy = szy / szz;
  f.ok = true;
  return f;
}
}  // namespace

void ThermalColumn::analyze(float windE, float windN) {
  if (analyzedRev_ == rev_ && analyzedWE_ == windE && analyzedWN_ == windN) { placeHead(); return; }
  analyzedRev_ = rev_;
  analyzedWE_ = windE;
  analyzedWN_ = windN;
  clearResults();
  const int n = n_;
  if (n < 12) { placeHead(); return; }

  // 1. air frame, relative to the newest point: a point flown a seconds ago moved downwind by a * wind since
  const uint32_t tNew = pts_[n - 1].tMs;
  const float gxN = pts_[n - 1].gx, gyN = pts_[n - 1].gy;
  for (int i = 0; i < n; i++) {
    const float age = (tNew - pts_[i].tMs) / 1000.0f;
    ax_[i] = pts_[i].gx - gxN + windE * age;
    ay_[i] = pts_[i].gy - gyN + windN * age;
  }

  // 2. circling runs: smoothed turn rate of at least 6 deg/s, one direction, gaps up to 3 s bridged
  static int8_t circ[MAXP];            // static: too big for the 4 KB loop stack
  for (int i = 0; i < n; i++) {
    float s = 0;
    int c = 0;
    for (int k = i - 2; k <= i + 2; k++)
      if (k >= 1 && k < n && isfinite(pts_[k].rate)) { s += pts_[k].rate; c++; }
    circ[i] = (c >= 3 && fabsf(s / c) >= 6.0f) ? (s > 0 ? 1 : -1) : 0;
  }
  Run runs[16];
  int nr = 0;
  for (int i = 1; i < n;) {
    if (!circ[i]) { i++; continue; }
    const int sg = circ[i];
    int e = i, gap = 0, j = i;
    for (; j < n; j++) {
      if (circ[j] == sg) { e = j; gap = 0; }
      else if (circ[j] == -sg) break;
      else if (++gap > 3) break;
    }
    Run r;
    r.s = i > 2 ? i - 2 : 1;               // the smoothing window lets the turn start two points early
    r.e = e + 2 < n ? e + 2 : n - 1;
    r.ang = 0;
    for (int k = r.s; k <= r.e; k++)
      if (isfinite(pts_[k].rate)) r.ang += pts_[k].rate * (pts_[k].tMs - pts_[k - 1].tMs) / 1000.0f;
    if (fabsf(r.ang) >= 270.0f) {
      if (nr == 16) { memmove(runs, runs + 1, sizeof(Run) * 15); nr--; }
      runs[nr++] = r;
    }
    i = e + 1;
  }
  if (nr == 0) { placeHead(); return; }

  // 3. the latest climb: the newest run and the runs before it that follow within 30 s of each other
  int first = nr - 1;
  while (first > 0 && (pts_[runs[first].s].tMs - pts_[runs[first - 1].e].tMs) <= 30000u) first--;
  const int lastPt = runs[nr - 1].e;
  const uint32_t age = (tNew - pts_[lastPt].tMs) / 1000u;
  if (age > (uint32_t)MAX_AGE_S) { placeHead(); return; }
  ageS = age;
  circling = age <= 4;

  // circles: cut each run every 360 degrees of heading change; a leftover of 270 degrees or more is a circle too
  int cut = 0;
  for (int ri = first; ri < nr; ri++) {
    const Run& r = runs[ri];
    const int8_t sg = r.ang > 0 ? 1 : -1;
    cut = r.s;
    float acc = 0;
    auto add = [&](int a, int b) {
      if (b - a < 6) return;
      if (nCircles == MAXC) { memmove(circles, circles + 1, sizeof(CoreCircle) * (MAXC - 1)); nCircles--; }
      CoreCircle& c = circles[nCircles++];
      c = CoreCircle();
      c.a = a;
      c.b = b;
      c.turn = sg;
    };
    for (int k = r.s + 1; k <= r.e; k++) {
      if (isfinite(pts_[k].rate)) acc += sg * pts_[k].rate * (pts_[k].tMs - pts_[k - 1].tMs) / 1000.0f;   // signed: heading noise cancels
      if (acc >= 360.0f) { add(cut, k); cut = k; acc -= 360.0f; }
    }
    if (acc >= 270.0f) add(cut, r.e);
  }
  if (nCircles == 0) { nCircles = 0; placeHead(); return; }
  valid = true;

  // 4. per circle metrics; then circle indices are renumbered relative to the climb's first point
  for (int i = 0; i < nCircles; i++) circleMetrics(circles[i]);
  p0_ = circles[0].a > 5 ? circles[0].a - 5 : 0;
  nP_ = n - p0_;
  for (int i = 0; i < nCircles; i++) { circles[i].a -= p0_; circles[i].b -= p0_; }
  const int endAbs = circles[nCircles - 1].b + p0_;

  // 5. the lean: a line through the core points of the newest circles, with one pass that drops outliers
  const int i0 = nCircles > LEAN_CIRCLES ? nCircles - LEAN_CIRCLES : 0;
  bool skip[MAXC];
  for (int i = 0; i < MAXC; i++) skip[i] = false;
  Fit f = fitLine(circles, i0, nCircles, skip);
  if (f.ok) {
    float ss = 0;
    for (int i = i0; i < nCircles; i++) {
      const float dx = circles[i].x - (f.xm + f.sx * (circles[i].z - f.zm));
      const float dy = circles[i].y - (f.ym + f.sy * (circles[i].z - f.zm));
      ss += dx * dx + dy * dy;
    }
    const float sigma = sqrtf(ss / (nCircles - i0));
    const float lim = fmaxf(2.5f * sigma, 8.0f);
    int dropped = 0;
    for (int i = i0; i < nCircles; i++) {
      const float dx = circles[i].x - (f.xm + f.sx * (circles[i].z - f.zm));
      const float dy = circles[i].y - (f.ym + f.sy * (circles[i].z - f.zm));
      if (nCircles - i0 - dropped > 4 && hypotf(dx, dy) > lim) { skip[i] = true; dropped++; }
    }
    if (dropped) {
      Fit g = fitLine(circles, i0, nCircles, skip);
      if (g.ok) f = g;
    }
    int used = 0;
    float ss2 = 0;
    for (int i = i0; i < nCircles; i++) {
      if (skip[i]) continue;
      const float dx = circles[i].x - (f.xm + f.sx * (circles[i].z - f.zm));
      const float dy = circles[i].y - (f.ym + f.sy * (circles[i].z - f.zm));
      ss2 += dx * dx + dy * dy;
      used++;
    }
    leanValid = true;
    leanRms = sqrtf(ss2 / (used ? used : 1));
    coreX0 = f.xm; coreY0 = f.ym; coreZ0 = f.zm;
    coreSx = f.sx; coreSy = f.sy;
    leanPer100 = 100.0f * hypotf(f.sx, f.sy);
    float d = atan2f(f.sx, f.sy) * R2D;
    leanDeg = d < 0 ? d + 360.0f : d;
  } else {
    const CoreCircle& c = circles[nCircles - 1];          // no lean yet: a vertical line through the newest core point
    coreX0 = c.x; coreY0 = c.y; coreZ0 = c.z;
  }

  // 6. strength by altitude band (the points of the climb up to its last circle)
  zMin = 1e9f;
  zMax = -1e9f;
  for (int k = p0_; k < n; k++) {
    if (pts_[k].z < zMin) zMin = pts_[k].z;
    if (pts_[k].z > zMax) zMax = pts_[k].z;
  }
  {
    const float span = zMax - zMin;
    bandM = 25;
    while (bandM < 400 && span / bandM + 2 > MAXB) bandM *= 2;
    const int b0 = (int)floorf(zMin / bandM);
    float sum[MAXB];
    int cnt[MAXB];
    for (int i = 0; i < MAXB; i++) { sum[i] = 0; cnt[i] = 0; }
    for (int k = p0_; k <= endAbs; k++) {
      const int bi = (int)floorf(pts_[k].z / bandM) - b0;
      if (bi < 0 || bi >= MAXB) continue;
      sum[bi] += pts_[k].v;
      cnt[bi]++;
    }
    nBands = 0;
    for (int i = 0; i < MAXB; i++)
      if (cnt[i] >= 3) {
        bands[nBands].z0 = (b0 + i) * bandM;
        bands[nBands].climb = sum[i] / cnt[i];
        bands[nBands].n = cnt[i];
        nBands++;
      }
  }

  // 7. trend: the newest two circles against the best two before them
  if (nCircles >= 4) {
    const float last2 = 0.5f * (circles[nCircles - 1].climb + circles[nCircles - 2].climb);
    float ref = -1e9f;
    for (int j = 0; j + 1 <= nCircles - 3; j++) ref = fmaxf(ref, 0.5f * (circles[j].climb + circles[j + 1].climb));
    trendRatio = ref > 0.3f ? last2 / ref : 1.0f;
    if ((ref >= 0.8f && last2 < 0.4f) || (ref > 0.3f && trendRatio < 0.4f)) trend = TREND_TOP;
    else if (trendRatio < 0.7f) trend = TREND_WEAKENING;
    else if (trendRatio >= 1.15f && last2 >= 0.8f) trend = TREND_BUILDING;
    else trend = TREND_STEADY;
  }

  // 8. the side view's cross-section: along the lean when there is one worth showing, else along the wind
  if (leanValid && leanPer100 >= 3.0f) {
    axisKind = AXIS_LEAN;
    axisDeg = leanDeg;
  } else if (hypotf(windE, windN) >= 1.0f) {
    axisKind = AXIS_WIND;
    float d = atan2f(windE, windN) * R2D;
    axisDeg = d < 0 ? d + 360.0f : d;
  }
  placeHead();
}
