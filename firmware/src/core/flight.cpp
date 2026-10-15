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

void FlightModel::reset() {
  // member by member: a temporary FlightModel would put the whole history on the stack
  cur_ = Sample();
  have_ = false;
  n_ = 0;
  clock_ = 1000;
  lastKey_ = -1;
  flying_ = false;
  flightStart_ = fastSince_ = slowSince_ = 0;
  circling_ = false;
  lastTurn_ = 0;
  windValid_ = false;
  wx_ = wy_ = 0;
  windAt_ = 0;
  mem_.reset();
  profile_.reset();
  home_ = HomePoint();
  homeFixed_ = false;
}

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

  // home: the first fix is a provisional guess; takeoff makes it final. Only the first takeoff counts
  // (slow flying for a minute also clears flying_, and must not move home in mid-air).
  if (cur_.fix && isfinite(cur_.lat) && isfinite(cur_.lon)) {
    if (!home_.valid) {
      home_.valid = true; home_.provisional = true;
      home_.lat = cur_.lat; home_.lon = cur_.lon; home_.alt = cur_.alt;
    }
    if (flying_ && !homeFixed_) {
      homeFixed_ = true;
      home_.provisional = false;
      home_.lat = cur_.lat; home_.lon = cur_.lon; home_.alt = cur_.alt;
    }
  }

  // circling: mean absolute turn rate over the last 8 s
  if (n_ >= 9 && cur_.fix) {
    float sum = 0;
    for (int i = n_ - 8; i < n_; i++) sum += fabsf(wrap180(h_[i].course - h_[i - 1].course));
    if (isfinite(sum) && sum / 8.0f > 6.0f && sp > 8) { circling_ = true; lastTurn_ = now; }
  }
  if (circling_ && now - lastTurn_ > 16000) circling_ = false;    // straight again for 16 s

  updateWind();
  updateProfile();
  updateThermals();
}

// One entry per fresh wind estimate (the fit ran this second), at the altitude it was flown at.
void FlightModel::updateProfile() {
  Wind w = wind();
  if (!w.valid || w.ageS != 0 || !isfinite(cur_.alt)) return;
  double th = w.toDeg / R2D, ms = w.speedKmh / 3.6;
  profile_.add(cur_.alt, (float)(ms * sin(th)), (float)(ms * cos(th)), clock_);
}

void FlightModel::updateThermals() {
  mem_.expire(clock_);
  if (n_ < 20 || !h_[n_ - 1].fix) return;
  int cnt = airTrail(scratch_, HIST);
  Thermal found;
  if (!findThermal(scratch_, cnt, found)) return;
  mem_.update(h_[n_ - 1].lat, h_[n_ - 1].lon, found, clock_, windValid_ ? wx_ : 0, windValid_ ? wy_ : 0);
}

int FlightModel::thermals(ThermalRel* out, int maxN) const {
  if (n_ < 1 || !h_[n_ - 1].fix) return 0;
  return mem_.relative(out, maxN, h_[n_ - 1].lat, h_[n_ - 1].lon, clock_, windValid_ ? wx_ : 0, windValid_ ? wy_ : 0);
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

int FlightModel::airTrail(AirPoint* out, int maxN) const {
  if (n_ < 2 || !h_[n_ - 1].fix) return 0;
  const double D2R = 0.017453292519943295;
  double lat0 = h_[n_ - 1].lat, lon0 = h_[n_ - 1].lon;
  double kx = 111320.0 * cos(lat0 * D2R), ky = 111320.0;
  double wx = windValid_ ? wx_ : 0, wy = windValid_ ? wy_ : 0;
  int cnt = 0;
  for (int i = (n_ > maxN ? n_ - maxN : 0); i < n_; i++) {
    if (!h_[i].fix) continue;
    double age = n_ - 1 - i;                  // seconds before now: the air has moved age * wind since
    out[cnt].x = (float)((h_[i].lon - lon0) * kx + wx * age);
    out[cnt].y = (float)((h_[i].lat - lat0) * ky + wy * age);
    out[cnt].vario = isfinite(h_[i].vario) ? h_[i].vario : 0;
    out[cnt].turn = 0;
    if (i > 0 && h_[i].fix && h_[i - 1].fix) {
      float dc = fabsf(wrap180(h_[i].course - h_[i - 1].course));
      if (isfinite(dc)) out[cnt].turn = dc;
    }
    cnt++;
  }
  return cnt;
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
