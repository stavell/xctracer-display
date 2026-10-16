// Tests of the gyro/accel/mag fusion (core/ahrs) against a synthetic IMU, and of the case mounting table.
// Included by tests.cpp (uses its CHECK / NEAR).
#pragma once
#include <random>
#include "platform/sensors/ahrs.h"
#include "platform/sensors/mount.h"

// A rigid body with a known attitude R (world east/north/up <- device), turned by commanded body rates.
// Sensors: gyro = rate + bias + noise, accel = +g along "up" (+ noise), mag = Earth's field (+ noise).
struct ImuSim {
  double R[3][3];                    // row i = world axis i in device coordinates
  double bias[3] = {0, 0, 0};        // rad/s
  double gyroNoise = 0.1 * 0.017453292519943295;   // rad/s rms
  double accNoise = 0.05;            // m/s^2 rms
  double magNoise = 1.0;             // uT, uniform +-
  double field = 48.0, incl = 60.0;  // uT, degrees
  std::mt19937 rng{42};

  ImuSim(double headingDeg, double pitchDeg, double rollDeg) {
    MockCompass mc;                  // same construction as the compass tests
    const double D = 0.017453292519943295;
    double p = headingDeg * D, t = pitchDeg * D, r = rollDeg * D;
    double x0[3] = {cos(p), -sin(p), 0}, y0[3] = {sin(p), cos(p), 0}, z0[3] = {0, 0, 1};
    double ax[3][3];
    for (int i = 0; i < 3; i++) {
      double y1 = y0[i] * cos(t) + z0[i] * sin(t), z1 = -y0[i] * sin(t) + z0[i] * cos(t);
      ax[0][i] = x0[i] * cos(r) - z1 * sin(r);
      ax[1][i] = y1;
      ax[2][i] = x0[i] * sin(r) + z1 * cos(r);
    }
    for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) R[i][k] = ax[k][i];
  }
  double gauss(double s) { return std::normal_distribution<double>(0, s)(rng); }
  double unif(double a) { return std::uniform_real_distribution<double>(-a, a)(rng); }

  // turn by the body-frame rotation vector w*dt (Rodrigues): R <- R * Rot(w dt)
  void step(const double w[3], double dt) {
    double th[3] = {w[0] * dt, w[1] * dt, w[2] * dt};
    double a = sqrt(th[0] * th[0] + th[1] * th[1] + th[2] * th[2]);
    if (a < 1e-12) return;
    double k[3] = {th[0] / a, th[1] / a, th[2] / a};
    double K[3][3] = {{0, -k[2], k[1]}, {k[2], 0, -k[0]}, {-k[1], k[0], 0}};
    double Rot[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
      double kk = k[i] * k[j];
      Rot[i][j] = (i == j ? cos(a) : 0) + sin(a) * K[i][j] + (1 - cos(a)) * kk;
    }
    double N[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { N[i][j] = 0; for (int m = 0; m < 3; m++) N[i][j] += R[i][m] * Rot[m][j]; }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R[i][j] = N[i][j];
  }
  // body rates for a turn of `rate` rad/s about the world vertical (clockwise from above when negative)
  void yawRates(double rate, double w[3]) const { for (int j = 0; j < 3; j++) w[j] = R[2][j] * rate; }
  double truthHeading() const { double h = atan2(R[0][1], R[1][1]) * 57.29577951308232; return h < 0 ? h + 360 : h; }
  void sample(const double w[3], Vec3& g, Vec3& a, Vec3& m, bool noisy = true) {
    double B[3] = {0, field * cos(incl * 0.017453292519943295), -field * sin(incl * 0.017453292519943295)};
    double gv[3] = {w[0] + bias[0], w[1] + bias[1], w[2] + bias[2]};
    double av[3], mv[3];
    for (int j = 0; j < 3; j++) {
      av[j] = 9.81 * R[2][j];
      mv[j] = R[0][j] * B[0] + R[1][j] * B[1] + R[2][j] * B[2];
      if (noisy) { gv[j] += gauss(gyroNoise); av[j] += gauss(accNoise); mv[j] += unif(magNoise); }
    }
    g.x = (float)gv[0]; g.y = (float)gv[1]; g.z = (float)gv[2];
    a.x = (float)av[0]; a.y = (float)av[1]; a.z = (float)av[2];
    m.x = (float)mv[0]; m.y = (float)mv[1]; m.z = (float)mv[2];
  }
};

static float angErr(float a, float b) { return fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f); }
static float upErrDeg(const ImuSim& s, const Ahrs& f) {
  Vec3 u = f.up();
  double c = u.x * s.R[2][0] + u.y * s.R[2][1] + u.z * s.R[2][2];
  return (float)(acos(fmin(1.0, c)) * 57.29577951308232);
}

// Runs the sim at 80 Hz (timestamps in whole ms like the device) for `seconds`, rates from `rates(t, w)`.
template <class F>
static void runImu(ImuSim& s, Ahrs& f, double& t, double seconds, F rates, float* maxErr = nullptr, float* maxTilt = nullptr,
                   bool noMag = false, double tStartStats = 0) {
  const double dt = 1.0 / 80.0;
  double end = t + seconds;
  while (t < end) {
    double w[3];
    rates(t, w);
    s.step(w, dt);
    t += dt;
    Vec3 g, a, m;
    s.sample(w, g, a, m);
    if (noMag) m = Vec3();
    f.update(g, a, m, (uint32_t)llround(t * 1000));
    if (t >= tStartStats && maxErr) *maxErr = fmaxf(*maxErr, angErr(f.heading(), (float)s.truthHeading()));
    if (t >= tStartStats && maxTilt) *maxTilt = fmaxf(*maxTilt, upErrDeg(s, f));
  }
}
static void noRate(double, double w[3]) { w[0] = w[1] = w[2] = 0; }

static void test_ahrs() {
  puts("ahrs: gyro/accel/mag fusion on a synthetic IMU");
  const double D = 0.017453292519943295;

  // still box, noisy magnetometer (+-1 uT), gyro bias, converged: heading steady
  { ImuSim s(123, 20, -10);
    s.bias[0] = 1.0 * D; s.bias[1] = -1.5 * D; s.bias[2] = 2.0 * D;
    Ahrs f; double t = 0; float err = 0, tilt = 0;
    runImu(s, f, t, 30, noRate);                       // converge (bias learned in the first seconds; the leftover heading error decays with tau 10 s)
    CHECK(f.valid());
    float h0 = f.heading();
    float lo = 1e9f, hi = -1e9f;                       // peak-to-peak of the fused heading (what the dial would show)
    { const double dt = 1.0 / 80.0; double end = t + 40;
      ImuSim s2 = s; Ahrs f2 = f; double t2 = t;
      while (t2 < end) { double w[3] = {0, 0, 0}; s2.step(w, dt); t2 += dt; Vec3 g, a, m; s2.sample(w, g, a, m);
        f2.update(g, a, m, (uint32_t)llround(t2 * 1000));
        float sg = fmodf(f2.heading() - h0 + 540.0f, 360.0f) - 180.0f;
        lo = fminf(lo, sg); hi = fmaxf(hi, sg); } }
    printf("  still, +-1 uT mag noise: fused heading peak-to-peak %.3f deg over 40 s\n", hi - lo);
    CHECK(hi - lo < 0.4f);
    runImu(s, f, t, 60, noRate, &err, &tilt, false, 30);
    printf("  still, noisy: max heading error %.3f deg, max tilt error %.3f deg, bias %.2f dps (true %.2f)\n", err, tilt, f.biasDps(),
           sqrt(1.0 + 2.25 + 4.0));
    CHECK(err < 0.3f);
    CHECK(tilt < 0.3f);
    CHECK(angErr(f.heading(), h0) < 0.3f);
    CHECK(f.still());
    NEAR(f.bias().z / D, 2.0, 0.15);
    // the bias is learned: with the magnetometer gone the gyro alone holds the heading for 60 s
    float drift = 0;
    runImu(s, f, t, 60, noRate, &drift, nullptr, true, t);
    printf("  gyro bias 2 dps learned: heading drift without magnetometer over 60 s: %.2f deg\n", drift);
    CHECK(drift < 1.0f); }

  // fast convergence from the first sample
  { ImuSim s(250, -15, 25);
    Ahrs f; double t = 0; float err = 0;
    runImu(s, f, t, 0.1, noRate);
    CHECK(f.valid());
    runImu(s, f, t, 2.0, noRate, &err, nullptr, false, 0.1 + 1.0);
    printf("  start: error after 1 s .. 2.1 s: max %.2f deg\n", err);
    CHECK(err < 1.5f); }

  // 90 degree turn at 180 deg/s, followed in real time
  for (int variant = 0; variant < 3; variant++) {
    ImuSim s(30, variant == 1 ? 35 : 5, variant == 2 ? -40 : 3);
    s.bias[2] = 1.0 * D;
    Ahrs f; double t = 0, errAll = 0; float err = 0;
    runImu(s, f, t, 6, noRate);
    double t0 = t;
    runImu(s, f, t, 0.5, [&](double, double w[3]) { s.yawRates(-180 * D, w); }, &err);   // clockwise: heading grows
    printf("  turn 90 deg at 180 dps (pose %d): max error during the turn %.2f deg, heading %.1f -> %.1f\n", variant, err,
           (double)f.heading() - 0, s.truthHeading());
    (void)t0; (void)errAll;
    CHECK(err < 2.0f);
    float after = 0;
    runImu(s, f, t, 3, noRate, &after, nullptr, false, t + 2.0);
    CHECK(after < 0.5f); }

  // a magnetic disturbance: strong (rejected by the gate) and moderate (accepted, but only twists the heading)
  { ImuSim s(80, 25, 15);
    Ahrs f; double t = 0; float hErr = 0, tilt = 0;
    runImu(s, f, t, 8, noRate);
    // 1 s with the field 2.5x and pointing elsewhere
    { double end = t + 1.0;
      while (t < end) {
        double w[3] = {0, 0, 0}; s.step(w, 1 / 80.0); t += 1 / 80.0;
        Vec3 g, a, m; s.sample(w, g, a, m);
        m.x = 60; m.y = -80; m.z = 30;
        f.update(g, a, m, (uint32_t)llround(t * 1000));
        hErr = fmaxf(hErr, angErr(f.heading(), (float)s.truthHeading()));
        tilt = fmaxf(tilt, upErrDeg(s, f));
      } }
    printf("  strong field spike: heading error %.2f deg, tilt error %.3f deg\n", hErr, tilt);
    CHECK(hErr < 0.5f); CHECK(tilt < 0.3f);
    // 3 s with the field turned by 25 degrees about the vertical and 10% stronger
    hErr = 0; tilt = 0;
    { double end = t + 3.0;
      while (t < end) {
        double w[3] = {0, 0, 0}; s.step(w, 1 / 80.0); t += 1 / 80.0;
        Vec3 g, a, m; s.sample(w, g, a, m);
        double c = cos(25 * D), sn = sin(25 * D);
        double mw0 = s.R[0][0] * m.x + s.R[0][1] * m.y + s.R[0][2] * m.z, mw1 = s.R[1][0] * m.x + s.R[1][1] * m.y + s.R[1][2] * m.z,
               mw2 = s.R[2][0] * m.x + s.R[2][1] * m.y + s.R[2][2] * m.z;
        double e0 = 1.05 * (c * mw0 - sn * mw1), e1 = 1.05 * (sn * mw0 + c * mw1), e2 = 1.05 * mw2;
        m.x = (float)(s.R[0][0] * e0 + s.R[1][0] * e1 + s.R[2][0] * e2);
        m.y = (float)(s.R[0][1] * e0 + s.R[1][1] * e1 + s.R[2][1] * e2);
        m.z = (float)(s.R[0][2] * e0 + s.R[1][2] * e1 + s.R[2][2] * e2);
        f.update(g, a, m, (uint32_t)llround(t * 1000));
        tilt = fmaxf(tilt, upErrDeg(s, f));
        hErr = fmaxf(hErr, angErr(f.heading(), (float)s.truthHeading()));
      } }
    printf("  25 deg field twist: tilt error %.3f deg (heading may move, %.1f deg)\n", tilt, hErr);
    CHECK(tilt < 0.3f);
    // and it recovers once the field is right again
    float after = 0;
    runImu(s, f, t, 50, noRate, &after, nullptr, false, t + 45);   // tau 10 s: back within 0.5 deg after about 45 s
    CHECK(after < 0.5f); }

  // the field's trust falls gradually: a 12% stronger field (between full trust and the gate) twists the
  // heading less than a clean one would, and never tilts the attitude
  { ImuSim s(140, 15, 5);
    Ahrs f; double t = 0; float tilt = 0;
    runImu(s, f, t, 8, noRate);
    s.field = 48.0 * 1.12;                             // dev 12%: trust (0.2-0.12)/0.15 = 0.53
    s.magNoise = 0;
    // a 20 deg twist with this field strength: compare the pull against the same twist at full trust
    Ahrs g2 = f; ImuSim sg = s; double tg = t;
    auto twisted = [&](ImuSim& sim, Ahrs& fl, double& tt, double seconds, double fieldScale) {
      sim.field = 48.0 * fieldScale;
      double end = tt + seconds;
      while (tt < end) {
        double w[3] = {0, 0, 0}; sim.step(w, 1 / 80.0); tt += 1 / 80.0;
        Vec3 g, a, m; sim.sample(w, g, a, m, false);
        double c = cos(20 * D), sn = sin(20 * D);
        double mw0 = sim.R[0][0] * m.x + sim.R[0][1] * m.y + sim.R[0][2] * m.z, mw1 = sim.R[1][0] * m.x + sim.R[1][1] * m.y + sim.R[1][2] * m.z,
               mw2 = sim.R[2][0] * m.x + sim.R[2][1] * m.y + sim.R[2][2] * m.z;
        double e0 = c * mw0 - sn * mw1, e1 = sn * mw0 + c * mw1;
        m.x = (float)(sim.R[0][0] * e0 + sim.R[1][0] * e1 + sim.R[2][0] * mw2);
        m.y = (float)(sim.R[0][1] * e0 + sim.R[1][1] * e1 + sim.R[2][1] * mw2);
        m.z = (float)(sim.R[0][2] * e0 + sim.R[1][2] * e1 + sim.R[2][2] * mw2);
        fl.update(g, a, m, (uint32_t)llround(tt * 1000));
        tilt = fmaxf(tilt, upErrDeg(sim, fl));
      } };
    twisted(s, f, t, 2.0, 1.0);                        // clean magnitude
    twisted(sg, g2, tg, 2.0, 1.0);
    // second filter sees the 12% stronger field instead
    Ahrs f3; ImuSim s3(140, 15, 5); s3.magNoise = 0; double t3 = 0; runImu(s3, f3, t3, 8, noRate);
    twisted(s3, f3, t3, 2.0, 1.12);
    float pullClean = angErr(g2.heading(), (float)sg.truthHeading());
    float pullWeak = angErr(f3.heading(), (float)s3.truthHeading());
    printf("  20 deg twist for 2 s: heading error %.2f deg at full trust, %.2f deg with a 12%% off field\n", pullClean, pullWeak);
    CHECK(pullWeak < pullClean);
    CHECK(tilt < 0.3f); }

  // hand shaking: accelerations of +-0.5 g ignore gravity for those samples; heading and tilt hold
  { ImuSim s(200, 10, 10);
    Ahrs f; double t = 0; float err = 0, tilt = 0;
    runImu(s, f, t, 6, noRate);
    // free-fall-like: accel replaced by zero for 2 s; the gyro carries the attitude
    { double end = t + 2.0;
      while (t < end) {
        double w[3] = {0, 0, 0}; s.step(w, 1 / 80.0); t += 1 / 80.0;
        Vec3 g, a, m; s.sample(w, g, a, m);
        a.x = a.y = a.z = 0.3f;
        f.update(g, a, m, (uint32_t)llround(t * 1000));
        err = fmaxf(err, angErr(f.heading(), (float)s.truthHeading())); tilt = fmaxf(tilt, upErrDeg(s, f));
      } }
    CHECK(f.valid()); CHECK(err < 0.5f); CHECK(tilt < 0.5f); }

  // a gap restarts from accel + mag; a frozen gyro is reported
  { ImuSim s(310, 0, 0);
    Ahrs f; double t = 0;
    runImu(s, f, t, 3, noRate);
    t += 5;                                          // 5 s with no samples, the box is turned meanwhile
    double w[3] = {0, 0, 0};
    s.yawRates(-90 * D, w); s.step(w, 1.0);
    runImu(s, f, t, 0.2, noRate);
    CHECK(f.valid());
    CHECK(angErr(f.heading(), (float)s.truthHeading()) < 3.0f);
    Ahrs z; Vec3 g, a, m; double w0[3] = {0, 0, 0};
    ImuSim s2(10, 0, 0); s2.sample(w0, g, a, m, false);
    Vec3 zero;
    for (int i = 0; i < 200; i++) z.update(zero, a, m, (uint32_t)(i * 12));
    CHECK(z.gyroFailed()); CHECK(!z.valid());
    Vec3 live = zero; live.x = 0.001f;
    z.update(live, a, m, 2500);
    CHECK(!z.gyroFailed()); CHECK(z.valid()); }

  // garbage in: ignored, never NaN out
  { Ahrs f; Vec3 g, a, m; ImuSim s(5, 0, 0); double w[3] = {0, 0, 0}; s.sample(w, g, a, m);
    for (int i = 0; i < 50; i++) f.update(g, a, m, i * 12);
    Vec3 bad = g; bad.y = NAN;
    f.update(bad, a, m, 700);
    a.x = a.y = a.z = 0;
    for (int i = 0; i < 50; i++) f.update(g, a, m, 800 + i * 12);
    CHECK(isfinite(f.heading())); }
}

static void test_mount() {
  puts("mounting table (24 rotations)");
  // every index: a proper rotation (orthonormal, det +1), all distinct, inverse and remap consistent
  float M[MOUNT_COUNT][9];
  for (int i = 0; i < MOUNT_COUNT; i++) {
    Vec3 ex, ey, ez; ex.x = 1; ey.y = 1; ez.z = 1;
    Vec3 c[3] = {mountApply(i, ex), mountApply(i, ey), mountApply(i, ez)};   // columns of R
    for (int k = 0; k < 3; k++) { M[i][k] = c[k].x; M[i][3 + k] = c[k].y; M[i][6 + k] = c[k].z; }
    for (int k = 0; k < 3; k++) {
      float l = c[k].x * c[k].x + c[k].y * c[k].y + c[k].z * c[k].z;
      CHECK(l == 1.0f);
      int nz = (c[k].x != 0) + (c[k].y != 0) + (c[k].z != 0);
      CHECK(nz == 1);
    }
    float det = c[0].x * (c[1].y * c[2].z - c[2].y * c[1].z) - c[1].x * (c[0].y * c[2].z - c[2].y * c[0].z) +
                c[2].x * (c[0].y * c[1].z - c[1].y * c[0].z);
    CHECK(det == 1.0f);
    Vec3 v; v.x = 1.5f; v.y = -2.5f; v.z = 3.5f;
    Vec3 b = mountInverse(i, mountApply(i, v));
    CHECK(b.x == v.x && b.y == v.y && b.z == v.z);
    Vec3 r = mountRemap(i, 0, v), r2 = mountRemap(0, i, r);
    CHECK(r2.x == v.x && r2.y == v.y && r2.z == v.z);
  }
  int same = 0;
  for (int i = 0; i < MOUNT_COUNT; i++) for (int j = i + 1; j < MOUNT_COUNT; j++) if (memcmp(M[i], M[j], sizeof M[i]) == 0) same++;
  CHECK(same == 0);
  CHECK(mountValid(0) && mountValid(23) && !mountValid(24) && !mountValid(-1));
  Vec3 id; id.x = 1; id.y = 2; id.z = 3;
  Vec3 o = mountApply(0, id);
  CHECK(o.x == 1 && o.y == 2 && o.z == 3);

  // data generated in the board frame: with the right index the heading comes out right, with another it does not
  int wrongOk = 0;
  for (int idx = 0; idx < MOUNT_COUNT; idx++) {
    ImuSim s(77, 15, -20);
    Ahrs f, wrong; double t = 0;
    const double dt = 1.0 / 80.0;
    for (int n = 0; n < 480; n++) {
      double w[3] = {0, 0, 0};
      s.step(w, dt); t += dt;
      Vec3 g, a, m; s.sample(w, g, a, m);
      // device -> board, as the chips would report it with this mounting
      Vec3 gb = mountInverse(idx, g), ab = mountInverse(idx, a), mb = mountInverse(idx, m);
      uint32_t ms = (uint32_t)llround(t * 1000);
      f.update(mountApply(idx, gb), mountApply(idx, ab), mountApply(idx, mb), ms);
      wrong.update(gb, ab, mb, ms);
    }
    CHECK(f.valid());
    CHECK(angErr(f.heading(), (float)s.truthHeading()) < 0.5f);
    // a mounting that keeps the y axis (a flip of x and z only) gives the same heading: nothing to tell apart
    Vec3 ey; ey.y = 1; Vec3 my = mountInverse(idx, ey);
    bool keepsY = my.x == 0 && my.y == 1 && my.z == 0;
    if (!keepsY && wrong.valid() && angErr(wrong.heading(), (float)s.truthHeading()) < 5.0f) wrongOk++;
  }
  CHECK(wrongOk == 0);
}
