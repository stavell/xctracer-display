#include "apps/vario/core/windekf.h"
#include <math.h>

static const float Q_W = 0.0004f, Q_VA = 0.01f, R_MEAS = 1.0f;

void WindEkf::reset() {
  crsN_ = crsHead_ = 0;
  rejects_ = 0;
  init(0, 0, 25);
}

void WindEkf::init(float wx, float wy, float p) {     // keeps the course history: a seed in mid-turn keeps learning
  x_[0] = wx; x_[1] = wy; x_[2] = 10;
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) P_[i][j] = 0;
  P_[0][0] = P_[1][1] = p;
  P_[2][2] = 9;
}

float WindEkf::sigmaW() const {
  float s = P_[0][0] + P_[1][1];
  return s > 0 ? sqrtf(s) : 0;
}

static float wrap180(float a) {
  a = fmodf(a, 360.0f);
  if (a > 180) a -= 360;
  if (a < -180) a += 360;
  return a;
}

// Largest course difference between the new course and the last TURN_WIN_S usable ones.
float WindEkf::turnSpread(float courseDeg) const {
  float m = 0;
  for (int i = 0; i < crsN_; i++) {
    float d = fabsf(wrap180(crs_[i] - courseDeg));
    if (d > m) m = d;
  }
  return m;
}

bool WindEkf::step(float dtS, bool fix, float speedKmh, float courseDeg) {
  if (!(dtS >= 1)) dtS = 1;                     // also catches NaN
  if (dtS > 5) dtS = 5;
  P_[0][0] += Q_W * dtS; P_[1][1] += Q_W * dtS; P_[2][2] += Q_VA * dtS;

  if (!fix || !isfinite(speedKmh) || !isfinite(courseDeg)) return false;
  float sp = speedKmh / 3.6f;
  if (sp < 3.0f) return false;
  float spread = turnSpread(courseDeg);
  crs_[crsHead_] = courseDeg;
  crsHead_ = (crsHead_ + 1) % TURN_WIN_S;
  if (crsN_ < TURN_WIN_S) crsN_++;
  if (!(spread >= TURN_MIN_DEG)) return false;     // straight-ish: only the covariance grows (see the header)
  float c = courseDeg * 0.017453292f;
  float dx = sp * sinf(c) - x_[0], dy = sp * cosf(c) - x_[1];
  float r = sqrtf(dx * dx + dy * dy);
  if (r < 0.5f) return false;

  float H[3] = {-dx / r, -dy / r, -1};
  float y = -(r - x_[2]);
  float PH[3];
  for (int i = 0; i < 3; i++) PH[i] = P_[i][0] * H[0] + P_[i][1] * H[1] + P_[i][2] * H[2];
  float S = H[0] * PH[0] + H[1] * PH[1] + H[2] * PH[2] + R_MEAS;
  if (!(S > 0)) return false;
  if (y * y > 9 * S) {                                // 3 sigma outlier
    // One bad second is skipped. A run of them in turning flight means the STATE is wrong, not the data: a
    // filter that sits on a wrong state with a small covariance rejects every measurement and never recovers
    // (seen with noisy airspeed, and when the pilot changes speed between glide and thermal). First reopen only
    // the airspeed (the usual cause: a new speed regime), after a longer run the wind as well, so the next
    // turns can pull the whole state back; the wind is then not confident (and not shown from the EKF) meanwhile.
    ++rejects_;
    if (rejects_ == REJECTS_VA) {
      P_[0][2] = P_[2][0] = P_[1][2] = P_[2][1] = 0;
      P_[2][2] = 9;
    } else if (rejects_ >= REJECTS_ALL) {
      rejects_ = 0;
      for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) P_[i][j] = 0;
      P_[0][0] = P_[1][1] = 25;
      P_[2][2] = 9;
    }
    return false;
  }
  if (rejects_ > 0) rejects_--;                       // leaky: accepted seconds in between do not hide a wrong state

  float K[3];
  for (int i = 0; i < 3; i++) K[i] = PH[i] / S;
  for (int i = 0; i < 3; i++) x_[i] += K[i] * y;
  float HP[3];                                  // H P with the pre-update P
  for (int j = 0; j < 3; j++) HP[j] = H[0] * P_[0][j] + H[1] * P_[1][j] + H[2] * P_[2][j];
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) P_[i][j] -= K[i] * HP[j];
  for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) {
    float m = 0.5f * (P_[i][j] + P_[j][i]);
    P_[i][j] = P_[j][i] = m;
  }
  for (int i = 0; i < 3; i++) if (!(P_[i][i] >= 1e-6f)) P_[i][i] = 1e-6f;

  if (x_[2] < 5) x_[2] = 5;
  if (x_[2] > 25) x_[2] = 25;
  float w = sqrtf(x_[0] * x_[0] + x_[1] * x_[1]);
  if (w > 25) { x_[0] *= 25 / w; x_[1] *= 25 / w; }
  return true;
}
