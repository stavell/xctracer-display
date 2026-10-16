#include "platform/sensors/compass.h"

static const float R2D = 57.29577951308232f;

void Compass::reset() {
  calibrating_ = false;
  seeded_ = valid_ = false;
  heading_ = field_ = 0;
  a_ = Vec3();
  m_ = Vec3();
}

void Compass::startCalibration() {
  for (int i = 0; i < 3; i++) { mn_[i] = 1e9f; mx_[i] = -1e9f; }
  for (int i = 0; i < 4; i++) { atb_[i] = 0; for (int j = 0; j < 4; j++) ata_[i][j] = 0; }
  nCal_ = 0;
  calibrating_ = true;
}

void Compass::calibrationSpan(float out[3]) const {
  for (int i = 0; i < 3; i++) out[i] = mx_[i] > mn_[i] ? mx_[i] - mn_[i] : 0;
}

// Fit |m - c|^2 = r^2 as  2 c.m + (r^2 - |c|^2) = |m|^2, linear in (c, r^2 - |c|^2).
static bool solve4(double a[4][4], double b[4], double x[4]) {
  for (int col = 0; col < 4; col++) {
    int piv = col;
    for (int r = col + 1; r < 4; r++) if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
    if (fabs(a[piv][col]) < 1e-9) return false;
    if (piv != col) { for (int k = 0; k < 4; k++) { double t = a[col][k]; a[col][k] = a[piv][k]; a[piv][k] = t; } double t = b[col]; b[col] = b[piv]; b[piv] = t; }
    for (int r = col + 1; r < 4; r++) {
      double f = a[r][col] / a[col][col];
      for (int k = col; k < 4; k++) a[r][k] -= f * a[col][k];
      b[r] -= f * b[col];
    }
  }
  for (int r = 3; r >= 0; r--) {
    double s = b[r];
    for (int k = r + 1; k < 4; k++) s -= a[r][k] * x[k];
    x[r] = s / a[r][r];
  }
  return true;
}

bool Compass::finishCalibration(bool keep) {
  calibrating_ = false;
  if (!keep) return false;
  float span = 0;
  for (int i = 0; i < 3; i++) span += mx_[i] - mn_[i];
  if (nCal_ < 100 || !(span >= 30.0f)) return false;      // barely moved: keep the old calibration
  double a[4][4], b[4], x[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; i++) { b[i] = atb_[i]; for (int j = 0; j < 4; j++) a[i][j] = ata_[i][j]; }
  if (!solve4(a, b, x)) return false;
  double r2 = x[3] + x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
  if (!(r2 > 0)) return false;
  double r = sqrt(r2);
  if (r < 10.0 || r > 100.0) return false;                // not an Earth-sized field: do not trust it
  cal_.valid = true;
  for (int i = 0; i < 3; i++) cal_.off[i] = (float)x[i];
  cal_.radius = (float)r;
  seeded_ = false;                                         // restart the smoothing with the new offsets
  return true;
}

bool compassBasis(const Vec3& a, const Vec3& m, Vec3& e, Vec3& n, Vec3& u) {
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (!(g > 1e-3f)) return false;
  u.x = a.x / g; u.y = a.y / g; u.z = a.z / g;            // unit vector pointing up (else east and north differ in scale)
  // east = m x up, north = up x east
  e.x = m.y * u.z - m.z * u.y;
  e.y = m.z * u.x - m.x * u.z;
  e.z = m.x * u.y - m.y * u.x;
  float el = sqrtf(e.x * e.x + e.y * e.y + e.z * e.z);
  if (!(el > 1e-3f)) return false;                        // field parallel to gravity (or none)
  e.x /= el; e.y /= el; e.z /= el;
  n.x = u.y * e.z - u.z * e.y;
  n.y = u.z * e.x - u.x * e.z;
  n.z = u.x * e.y - u.y * e.x;
  return true;
}

Vec3 Compass::corrected(const Vec3& m) const {
  Vec3 c = m;
  if (cal_.valid) { c.x -= cal_.off[0]; c.y -= cal_.off[1]; c.z -= cal_.off[2]; }
  return c;
}

void Compass::update(const Vec3& a, const Vec3& m, uint32_t nowMs) {
  if (!isfinite(a.x) || !isfinite(a.y) || !isfinite(a.z) || !isfinite(m.x) || !isfinite(m.y) || !isfinite(m.z)) return;
  float raw[3] = {m.x, m.y, m.z};
  if (calibrating_) {
    double row[4] = {2.0 * m.x, 2.0 * m.y, 2.0 * m.z, 1.0};
    double rhs = (double)m.x * m.x + (double)m.y * m.y + (double)m.z * m.z;
    for (int i = 0; i < 4; i++) { atb_[i] += row[i] * rhs; for (int j = 0; j < 4; j++) ata_[i][j] += row[i] * row[j]; }
    nCal_++;
    for (int i = 0; i < 3; i++) {
      if (raw[i] < mn_[i]) mn_[i] = raw[i];
      if (raw[i] > mx_[i]) mx_[i] = raw[i];
    }
  }
  Vec3 c = corrected(m);                                   // hard-iron offset
  if (!seeded_) { a_ = a; m_ = c; seeded_ = true; lastMs_ = nowMs; }
  else {
    // exponential low-pass by elapsed time, so the response does not depend on the sensor rate
    float dt = (float)(uint32_t)(nowMs - lastMs_);       // unsigned: correct across the millis() wrap
    lastMs_ = nowMs;
    if (dt > MAX_DT_MS) dt = MAX_DT_MS;
    const float ka = 1.0f - expf(-dt / ACCEL_TAU_MS), km = 1.0f - expf(-dt / MAG_TAU_MS);
    a_.x += ka * (a.x - a_.x); a_.y += ka * (a.y - a_.y); a_.z += ka * (a.z - a_.z);
    m_.x += km * (c.x - m_.x); m_.y += km * (c.y - m_.y); m_.z += km * (c.z - m_.z);
  }
  field_ = sqrtf(m_.x * m_.x + m_.y * m_.y + m_.z * m_.z);
  float g = sqrtf(a_.x * a_.x + a_.y * a_.y + a_.z * a_.z);
  // not a usable gravity reading (freefall or hard acceleration), or no field at all
  if (!(g > 6.0f && g < 14.0f) || !(field_ > 5.0f)) { valid_ = false; return; }

  Vec3 e, n, u;
  if (!compassBasis(a_, m_, e, n, u)) { valid_ = false; return; }
  // the top of the screen (y) has azimuth atan2(east_y, north_y)
  float h = atan2f(e.y, n.y) * R2D;
  if (h < 0) h += 360;
  heading_ = h;
  valid_ = true;
}
