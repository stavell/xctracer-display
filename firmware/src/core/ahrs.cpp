#include "core/ahrs.h"
#include <math.h>
#include "core/compass.h"

static const float R2D = 57.29577951308232f;

static float len3(const double v[3]) { return (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

void Ahrs::reset() {
  seeded_ = valid_ = stuck_ = false;
  q_[0] = 1; q_[1] = q_[2] = q_[3] = 0;
  bias_ = Vec3();
  heading_ = 0;
  lastGyro_ = Vec3();
  sameGyro_ = 0;
  winN_ = 0;
  havePrev_ = false;
  stillWin_ = 0;
  biasWins_ = 0;
}

Vec3 Ahrs::up() const {
  float r[3][3];
  rotationMatrix(r);
  Vec3 u;
  u.x = r[2][0]; u.y = r[2][1]; u.z = r[2][2];
  return u;
}

float Ahrs::biasDps() const {
  return sqrtf(bias_.x * bias_.x + bias_.y * bias_.y + bias_.z * bias_.z) * R2D;
}

// World <- device rotation matrix: row i is world axis i (east, north, up) in device coordinates.
void Ahrs::rotationMatrix(float r[3][3]) const {
  const float w = q_[0], x = q_[1], y = q_[2], z = q_[3];
  r[0][0] = 1 - 2 * (y * y + z * z); r[0][1] = 2 * (x * y - w * z);     r[0][2] = 2 * (x * z + w * y);
  r[1][0] = 2 * (x * y + w * z);     r[1][1] = 1 - 2 * (x * x + z * z); r[1][2] = 2 * (y * z - w * x);
  r[2][0] = 2 * (x * z - w * y);     r[2][1] = 2 * (y * z + w * x);     r[2][2] = 1 - 2 * (x * x + y * y);
}

// Attitude straight from gravity and the field (the same basis as the plain compass): rows east, north, up.
bool Ahrs::seed(const Vec3& a, const Vec3& m, uint32_t nowMs) {
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  float mm = sqrtf(m.x * m.x + m.y * m.y + m.z * m.z);
  if (!(fabsf(g - G) < G_TOL * G) || !(mm > MIN_HORIZ_UT)) return false;
  Vec3 e, n, u;
  if (!compassBasis(a, m, e, n, u)) return false;
  const float r[3][3] = {{e.x, e.y, e.z}, {n.x, n.y, n.z}, {u.x, u.y, u.z}};
  const float tr = r[0][0] + r[1][1] + r[2][2];
  float w, x, y, z;
  if (tr > 0) {
    float s = 2.0f * sqrtf(tr + 1.0f);
    w = 0.25f * s; x = (r[2][1] - r[1][2]) / s; y = (r[0][2] - r[2][0]) / s; z = (r[1][0] - r[0][1]) / s;
  } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
    float s = 2.0f * sqrtf(1.0f + r[0][0] - r[1][1] - r[2][2]);
    w = (r[2][1] - r[1][2]) / s; x = 0.25f * s; y = (r[0][1] + r[1][0]) / s; z = (r[0][2] + r[2][0]) / s;
  } else if (r[1][1] > r[2][2]) {
    float s = 2.0f * sqrtf(1.0f + r[1][1] - r[0][0] - r[2][2]);
    w = (r[0][2] - r[2][0]) / s; x = (r[0][1] + r[1][0]) / s; y = 0.25f * s; z = (r[1][2] + r[2][1]) / s;
  } else {
    float s = 2.0f * sqrtf(1.0f + r[2][2] - r[0][0] - r[1][1]);
    w = (r[1][0] - r[0][1]) / s; x = (r[0][2] + r[2][0]) / s; y = (r[1][2] + r[2][1]) / s; z = 0.25f * s;
  }
  q_[0] = w; q_[1] = x; q_[2] = y; q_[3] = z;
  seeded_ = true;
  seedMs_ = boostMs_ = accMs_ = magMs_ = nowMs;
  magRejectMs_ = 0;
  magRef_ = mm;
  dipRef_ = (u.x * m.x + u.y * m.y + u.z * m.z) / mm;     // vertical share of the field
  winN_ = 0;
  havePrev_ = false;
  stillWin_ = 0;
  return true;
}

// Called once per still-detection window (0.5 s). A window is still when the gyro is quiet, gravity is
// usable, and gravity and field point where they did in the previous window. Two such windows in a row
// (1 s) and the gyro mean IS the bias: take most of it.
void Ahrs::windowEnd(uint32_t nowMs) {
  const int n = winN_;
  winN_ = 0;
  if (n < 10) { havePrev_ = false; stillWin_ = 0; return; }
  double mg[3], ma[3], mm[3];
  for (int i = 0; i < 3; i++) { mg[i] = sumG_[i] / n; ma[i] = sumA_[i]; mm[i] = sumM_[i]; }
  double var = sumG2_ / n - (mg[0] * mg[0] + mg[1] * mg[1] + mg[2] * mg[2]);
  float gstd = var > 0 ? (float)sqrt(var) : 0;
  float la = len3(ma), lm = len3(mm);
  bool ok = !winBad_ && la > 0 && lm > 0;
  Vec3 ua, um;
  if (ok) { ua.x = (float)(ma[0] / la); ua.y = (float)(ma[1] / la); ua.z = (float)(ma[2] / la);
            um.x = (float)(mm[0] / lm); um.y = (float)(mm[1] / lm); um.z = (float)(mm[2] / lm); }
  bool still = false;
  if (ok && havePrev_ && gstd < STILL_GYRO_STD) {
    float ca = ua.x * prevA_.x + ua.y * prevA_.y + ua.z * prevA_.z;
    float cm = um.x * prevM_.x + um.y * prevM_.y + um.z * prevM_.z;
    float da = acosf(fminf(1.0f, ca)) * R2D, dm = acosf(fminf(1.0f, cm)) * R2D;
    still = da < STILL_ACC_DEG && dm < STILL_MAG_DEG;
  }
  if (ok) { prevA_ = ua; prevM_ = um; havePrev_ = true; } else havePrev_ = false;
  stillWin_ = still ? stillWin_ + 1 : 0;
  if (still) {
    // fast at first (0.6, 0.3, 0.2), then a slow average (0.15) so the estimate is not noisier than the gyro
    const float rate = fmaxf(0.15f, 0.6f / (1.0f + (float)biasWins_));
    // The heading drifted by the old bias error until now: pull it back quickly (restart the boost) for
    // the first few estimates.
    if (biasWins_ < 3) boostMs_ = nowMs;
    if (biasWins_ < 100) biasWins_++;
    float* b = &bias_.x;
    for (int i = 0; i < 3; i++) {
      b[i] += rate * ((float)mg[i] - b[i]);
      if (b[i] > BIAS_MAX) b[i] = BIAS_MAX;
      if (b[i] < -BIAS_MAX) b[i] = -BIAS_MAX;
    }
  }
}

void Ahrs::update(const Vec3& gyro, const Vec3& a, const Vec3& m, uint32_t nowMs) {
  if (!isfinite(gyro.x) || !isfinite(gyro.y) || !isfinite(gyro.z) || !isfinite(a.x) || !isfinite(a.y) ||
      !isfinite(a.z) || !isfinite(m.x) || !isfinite(m.y) || !isfinite(m.z)) return;

  // A frozen gyro (bus error, chip stuck) repeats the same bits; a live one never does at this noise.
  if (gyro.x == lastGyro_.x && gyro.y == lastGyro_.y && gyro.z == lastGyro_.z) {
    if (++sameGyro_ >= STUCK_SAMPLES) stuck_ = true;
  } else {
    sameGyro_ = 0;
    if (stuck_) { stuck_ = false; seeded_ = false; }
  }
  lastGyro_ = gyro;
  if (stuck_) { valid_ = false; seeded_ = false; return; }

  const uint32_t dtMs = nowMs - lastMs_;               // unsigned: correct across the millis() wrap
  lastMs_ = nowMs;
  if (!seeded_ || dtMs > GAP_MS) {
    if (!seed(a, m, nowMs)) { seeded_ = false; valid_ = false; return; }
    valid_ = true;
    float r[3][3]; rotationMatrix(r);
    float h = atan2f(r[0][1], r[1][1]) * R2D; if (h < 0) h += 360.0f;
    heading_ = h;
    return;
  }
  if (dtMs == 0) return;
  float dt = dtMs * 0.001f;
  if (dt > MAX_DT) dt = MAX_DT;

  float r[3][3];
  rotationMatrix(r);
  const float up[3] = {r[2][0], r[2][1], r[2][2]};     // gravity direction as the attitude predicts it

  // Roll/pitch error: where gravity is measured versus predicted (rotation vector, device frame).
  const float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  const bool accOk = fabsf(g - G) < G_TOL * G;
  float ea[3] = {0, 0, 0};
  Vec3 ua;
  if (accOk) {
    ua.x = a.x / g; ua.y = a.y / g; ua.z = a.z / g;
    ea[0] = ua.y * up[2] - ua.z * up[1];
    ea[1] = ua.z * up[0] - ua.x * up[2];
    ea[2] = ua.x * up[1] - ua.y * up[0];
    accMs_ = nowMs;
  }

  // Heading error: angle of the horizontal field from north in the world frame, applied about the up axis
  // only. A disturbed field can therefore twist the heading but never tilt the attitude.
  float em[3] = {0, 0, 0};
  const float mm = sqrtf(m.x * m.x + m.y * m.y + m.z * m.z);
  const float mw0 = r[0][0] * m.x + r[0][1] * m.y + r[0][2] * m.z;
  const float mw1 = r[1][0] * m.x + r[1][1] * m.y + r[1][2] * m.z;
  const float mw2 = r[2][0] * m.x + r[2][1] * m.y + r[2][2] * m.z;
  const float hxy = sqrtf(mw0 * mw0 + mw1 * mw1);
  bool magOk = false;
  float trust = 0;                                     // weight of the field correction, 1 = clean, 0 = rejected
  if (mm > 0 && hxy > MIN_HORIZ_UT) {
    const float dip = mw2 / mm;
    // Graceful: full weight while magnitude and dip are within TRUST_FULL of the running reference, falling
    // linearly to zero at FIELD_TOL (where the old hard gate was).
    const float dev = fmaxf(fabsf(mm - magRef_) / magRef_, fabsf(dip - dipRef_));
    trust = fminf(1.0f, fmaxf(0.0f, (FIELD_TOL - dev) / (FIELD_TOL - TRUST_FULL)));
    magOk = dev < FIELD_TOL;
    if (magOk) {
      magRejectMs_ = 0;
      const float k = dt / 5.0f;                       // slow reference, 5 s
      magRef_ += k * (mm - magRef_);
      dipRef_ += k * (dip - dipRef_);
    } else if (magRejectMs_ == 0) {
      magRejectMs_ = nowMs ? nowMs : 1;
    } else if (nowMs - magRejectMs_ > 5000) {          // the "disturbance" is the new normal: adopt it
      magRef_ = mm; dipRef_ = dip; magRejectMs_ = 0;
    }
  }
  if (magOk) {
    const float phi = atan2f(mw0, mw1);                // >0: the field leans east, the estimate is turned too far west; rotate it by +phi about up
    for (int i = 0; i < 3; i++) em[i] = trust * phi * up[i];
    magMs_ = nowMs;
  }

  // Gains: high just after a (re)start, then the steady values.
  const float age = (float)(uint32_t)(nowMs - seedMs_);
  const float boostAge = (float)(uint32_t)(nowMs - boostMs_);
  const float fade = boostAge < BOOST_MS ? 1.0f - boostAge / BOOST_MS : 0.0f;
  const float boost = 1.0f + (BOOST_GAIN - 1.0f) * fade;
  const float boostMag = 1.0f + (BOOST_GAIN_MAG - 1.0f) * fade;
  float w[3] = {gyro.x, gyro.y, gyro.z};
  float* b = &bias_.x;
  for (int i = 0; i < 3; i++) {
    if (age >= BOOST_MS && stillWin_ == 0) {           // bias integral: not in the start-up transient, not while the still-learner owns the bias
      b[i] -= dt * (KI_ACC * ea[i] + KI_MAG * em[i]);
      if (b[i] > BIAS_MAX) b[i] = BIAS_MAX;
      if (b[i] < -BIAS_MAX) b[i] = -BIAS_MAX;
    }
    w[i] = w[i] - b[i] + boost * KP_ACC * ea[i] + boostMag * KP_MAG * em[i];
  }

  // q <- q * exp(w dt / 2): the rotation vector is in the device (body) frame
  const float tx = w[0] * dt, ty = w[1] * dt, tz = w[2] * dt;
  const float ang = sqrtf(tx * tx + ty * ty + tz * tz);
  float dw = 1.0f, dx = 0, dy = 0, dz = 0;
  if (ang > 1e-9f) {
    const float s = sinf(0.5f * ang) / ang;
    dw = cosf(0.5f * ang); dx = tx * s; dy = ty * s; dz = tz * s;
  }
  const float qw = q_[0], qx = q_[1], qy = q_[2], qz = q_[3];
  float nw = qw * dw - qx * dx - qy * dy - qz * dz;
  float nx = qw * dx + qx * dw + qy * dz - qz * dy;
  float ny = qw * dy - qx * dz + qy * dw + qz * dx;
  float nz = qw * dz + qx * dy - qy * dx + qz * dw;
  const float nn = 1.0f / sqrtf(nw * nw + nx * nx + ny * ny + nz * nz);
  q_[0] = nw * nn; q_[1] = nx * nn; q_[2] = ny * nn; q_[3] = nz * nn;

  // still detection runs on the raw gyro
  if (winN_ == 0) {
    winStart_ = nowMs; winBad_ = false; sumG2_ = 0;
    for (int i = 0; i < 3; i++) sumG_[i] = sumA_[i] = sumM_[i] = 0;
  }
  winN_++;
  sumG_[0] += gyro.x; sumG_[1] += gyro.y; sumG_[2] += gyro.z;
  sumG2_ += (double)gyro.x * gyro.x + (double)gyro.y * gyro.y + (double)gyro.z * gyro.z;
  if (accOk) { sumA_[0] += ua.x; sumA_[1] += ua.y; sumA_[2] += ua.z; } else winBad_ = true;
  if (mm > 0) { sumM_[0] += m.x / mm; sumM_[1] += m.y / mm; sumM_[2] += m.z / mm; } else winBad_ = true;
  if (!magOk) winBad_ = true;
  if (nowMs - winStart_ >= WINDOW_MS) windowEnd(nowMs);

  float rr[3][3];
  rotationMatrix(rr);
  float h = atan2f(rr[0][1], rr[1][1]) * R2D;
  if (h < 0) h += 360.0f;
  if (h >= 360.0f) h = 0;
  heading_ = h;
  valid_ = (uint32_t)(nowMs - accMs_) < ACC_LOST_MS && (uint32_t)(nowMs - magMs_) < MAG_LOST_MS;
}
