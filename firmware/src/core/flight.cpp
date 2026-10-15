#include "core/flight.h"
#include "core/wind.h"
#include <string.h>

static const double R2D = 57.29577951308232;

static float wrap180(float a) {   // safe for any input: NaN stays NaN, no loops
  a = fmodf(a, 360.0f);
  if (a > 180) a -= 360;
  if (a < -180) a += 360;
  return a;
}

void FlightModel::reset() { *this = FlightModel(); }

bool FlightModel::feed(const Sample& s) {
  cur_ = s;
  have_ = true;
  int key = s.hh * 3600 + s.mm * 60 + s.ss;
  if (key == lastKey_) return false;
  if (lastKey_ >= 0) {
    int dt = (key - lastKey_ + 86400) % 86400;
    clock_ += (uint32_t)(dt > 5 ? 5 : dt) * 1000u;    // clamp gaps and midnight wrap
  }
  lastKey_ = key;
  tick();
  return true;
}

void FlightModel::tick() {
  TrackPoint p = {cur_.fix, cur_.lat, cur_.lon, cur_.alt, cur_.vario, cur_.speedKmh, cur_.course};
  if (n_ == HIST) { memmove(h_, h_ + 1, sizeof(TrackPoint) * (HIST - 1)); n_--; }
  h_[n_++] = p;
  uint32_t now = clock_;

  // takeoff / landing from ground speed
  float sp = (cur_.fix && isfinite(cur_.speedKmh)) ? cur_.speedKmh : 0;
  if (sp > 10) { if (!fastSince_) fastSince_ = now; slowSince_ = 0; }
  else { fastSince_ = 0; if (!slowSince_) slowSince_ = now; }
  if (!flying_ && fastSince_ && now - fastSince_ > 5000) { flying_ = true; flightStart_ = fastSince_; }
  if (flying_ && slowSince_ && now - slowSince_ > 60000) flying_ = false;

  // circling: mean absolute turn rate over the last 8 s
  if (n_ >= 9 && cur_.fix) {
    float sum = 0;
    for (int i = n_ - 8; i < n_; i++) sum += fabsf(wrap180(h_[i].course - h_[i - 1].course));
    if (isfinite(sum) && sum / 8.0f > 6.0f && sp > 8) { circling_ = true; lastTurn_ = now; }
  }
  if (circling_ && now - lastTurn_ > 16000) circling_ = false;    // straight again for 16 s

  updateWind();
}

void FlightModel::updateWind() {
  if (n_ < WIND_S) return;
  WindFit f = fitWind(h_ + n_ - WIND_S, WIND_S);
  if (!f.ok) return;
  if (!windValid_ || clock_ - windAt_ > 120000) { wx_ = f.east; wy_ = f.north; }
  else { wx_ = 0.88 * wx_ + 0.12 * f.east; wy_ = 0.88 * wy_ + 0.12 * f.north; }   // smooth over ~8 s
  windAt_ = clock_;
  windValid_ = true;
}

Wind FlightModel::wind() const {
  Wind w;
  if (!windValid_) return w;
  w.valid = true;
  w.speedKmh = (float)(hypot(wx_, wy_) * 3.6);
  w.toDeg = (float)fmod(atan2(wx_, wy_) * R2D + 360.0, 360.0);
  w.ageS = (clock_ - windAt_) / 1000;
  return w;
}

float FlightModel::avgVario() const {
  int n = n_ < AVG_S ? n_ : AVG_S;
  if (!n) return isfinite(cur_.vario) ? cur_.vario : 0;
  float s = 0;
  int c = 0;
  for (int i = n_ - n; i < n_; i++) if (isfinite(h_[i].vario)) { s += h_[i].vario; c++; }
  return c ? s / c : 0;
}

float FlightModel::glide() const {
  int n = n_ < 20 ? n_ : 20;
  if (n < 5) return NAN;
  float sv = 0, ss = 0;
  int c = 0;
  for (int i = n_ - n; i < n_; i++) {
    if (!h_[i].fix || !isfinite(h_[i].vario) || !isfinite(h_[i].speedKmh)) continue;
    sv += h_[i].vario; ss += h_[i].speedKmh / 3.6f; c++;
  }
  if (c < 5) return NAN;
  float v = sv / c, sp = ss / c;
  if (v > -0.2f) return NAN;
  float g = sp / -v;
  return g > 99 ? 99 : g;
}

uint32_t FlightModel::flightSec() const { return flying_ ? (clock_ - flightStart_) / 1000 : 0; }
