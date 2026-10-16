#include "apps/vario/core/wind.h"
#include <string.h>

static const double D2R = 0.017453292519943295;

static double det3(const double m[3][3]) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
       - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
       + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

WindFit fitWind(const TrackPoint* pts, int n) {
  WindFit res;
  if (n < 15 || n > 64) return res;
  // Cheap rejections first, trig only for a window that can still pass: straight flight (the normal
  // case) leaves after the turn check without evaluating a single sin/cos.
  double vx[64], vy[64], ang[64];
  for (int i = 0; i < n; i++) {
    const TrackPoint& s = pts[i];
    if (!s.fix || !isfinite(s.speedKmh) || !isfinite(s.course) || s.speedKmh < 8) return res;
    ang[i] = fmod(fmod(s.course, 360.0) + 360.0, 360.0);
  }
  // the window must be turning at both ends: a window that starts or ends in a straight glide mixes in
  // vectors from a different airspeed and bends the fit
  auto turningPairs = [&](int from, int to) {
    int c = 0;
    for (int i = from; i < to; i++) {
      double dc = fmod(fabs(pts[i].course - pts[i - 1].course), 360.0);
      if (dc > 180) dc = 360 - dc;
      if (dc >= 5.0) c++;
    }
    return c;
  };
  if (turningPairs(1, 6) < 4 || turningPairs(n - 5, n) < 4) return res;

  // angular coverage = 360 - largest gap between the sorted headings
  for (int i = 1; i < n; i++) {
    double a = ang[i];
    int j = i - 1;
    while (j >= 0 && ang[j] > a) { ang[j + 1] = ang[j]; j--; }
    ang[j + 1] = a;
  }
  double gap = 0;
  for (int i = 0; i < n; i++) {
    double g = fmod(ang[(i + 1) % n] - ang[i] + 360.0, 360.0);
    if (g > gap) gap = g;
  }
  if (360.0 - gap < 200.0) return res;

  for (int i = 0; i < n; i++) {
    double ms = pts[i].speedKmh / 3.6;
    vx[i] = ms * sin(pts[i].course * D2R);
    vy[i] = ms * cos(pts[i].course * D2R);
  }
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sz = 0, sxz = 0, syz = 0;
  for (int i = 0; i < n; i++) {
    double z = vx[i] * vx[i] + vy[i] * vy[i];
    sx += vx[i]; sy += vy[i]; sxx += vx[i] * vx[i]; syy += vy[i] * vy[i]; sxy += vx[i] * vy[i];
    sz += z; sxz += vx[i] * z; syz += vy[i] * z;
  }
  double A[3][3] = {{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, (double)n}};
  double B[3] = {sxz, syz, sz};
  double D = det3(A);
  if (fabs(D) < 1e-6) return res;
  double sol[3];
  for (int k = 0; k < 3; k++) {
    double M[3][3];
    memcpy(M, A, sizeof M);
    for (int i = 0; i < 3; i++) M[i][k] = B[i];
    sol[k] = det3(M) / D;
  }
  double a = sol[0] / 2, b = sol[1] / 2;
  double r2 = sol[2] + a * a + b * b;
  if (!(r2 > 0)) return res;
  double r = sqrt(r2);
  double rms = 0;
  for (int i = 0; i < n; i++) { double e = hypot(vx[i] - a, vy[i] - b) - r; rms += e * e; }
  rms = sqrt(rms / n);
  double windKmh = hypot(a, b) * 3.6;
  if (r * 3.6 < 14 || r * 3.6 > 72 || rms > 0.2 * r || windKmh > 72) return res;   // not an airspeed circle
  res.ok = true;
  res.east = a;
  res.north = b;
  return res;
}
