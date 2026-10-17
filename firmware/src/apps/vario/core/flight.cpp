#include "apps/vario/core/flight.h"
#include "apps/vario/core/wind.h"
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
  ekf_.reset();
  ekfStarted_ = false;
  ekfAt_ = 0;
  ekfInformedAt_ = 0;
  disagree_ = 0;
  candValid_ = false;
  candLat_ = candLon_ = 0;
  candAlt_ = NAN;
  mem_.reset();
  col_.reset();
  profile_.reset();
  tempC_ = NAN;
  tempAt_ = 0;
  haveTemp_ = false;
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
  if (sp > 10) {
    if (!fastSince_) {                       // candidate takeoff: remember where it began
      fastSince_ = now;
      candValid_ = isfinite(cur_.lat) && isfinite(cur_.lon);
      candLat_ = cur_.lat; candLon_ = cur_.lon; candAlt_ = cur_.alt;
    }
    slowSince_ = 0;
  } else {
    fastSince_ = 0;                          // rejected (or not yet confirmed) takeoff: forget the candidate
    candValid_ = false;
    if (!slowSince_) slowSince_ = now;
  }
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
      // takeoff is detected 5 s after the speed threshold was passed; home is where it was passed
      homeFixed_ = true;
      home_.provisional = false;
      if (candValid_) { home_.lat = candLat_; home_.lon = candLon_; home_.alt = candAlt_; }
      else { home_.lat = cur_.lat; home_.lon = cur_.lon; home_.alt = cur_.alt; }
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
  if (!ekfStarted_) {
    ekfStarted_ = true;
    ekfAt_ = now - 1000;
  }
  bool updated = ekf_.step((now - ekfAt_) / 1000.0f, cur_.fix, cur_.speedKmh, cur_.course);
  if (updated && turnedRecently()) ekfInformedAt_ = now;
  ekfAt_ = now;
  updateProfile();
  updateTemperature();
  updateThermals();
  col_.push(clock_, cur_.fix, cur_.lat, cur_.lon, cur_.alt, cur_.vario);
}

const ThermalColumn& FlightModel::column() {
  double wx = 0, wy = 0;
  windXY(wx, wy);
  col_.analyze((float)wx, (float)wy);
  col_.setHead(have_ && cur_.fix, cur_.lat, cur_.lon, cur_.alt, cur_.vario);
  return col_;
}

// One entry per fresh circle-fit estimate (the fit ran in this very tick), at the altitude it was flown
// at. Fed from the circle fit, not wind(): wind() prefers the EKF, which adapts over minutes on purpose
// and would smear how the wind changes with altitude; the circle fit measures over a 30 s window.
void FlightModel::updateProfile() {
  if (!windValid_ || windAt_ != clock_ || !isfinite(cur_.alt)) return;
  Wind w = circleWind();
  double th = w.toDeg / R2D, ms = w.speedKmh / 3.6;
  profile_.add(cur_.alt, (float)(ms * sin(th)), (float)(ms * cos(th)), clock_);
}

void FlightModel::feedTemperature(float tempC) {
  if (!isfinite(tempC)) return;
  tempC_ = tempC;
  tempAt_ = clock_;
  haveTemp_ = true;
}

// Once per data second: the latest reading, if it is fresh (the thermometer reports every 2 s), goes
// into the band at the current altitude.
void FlightModel::updateTemperature() {
  if (!haveTemp_ || clock_ - tempAt_ >= 5000 || !isfinite(cur_.alt)) return;
  profile_.addTemp(cur_.alt, tempC_, clock_);
}

void FlightModel::updateThermals() {
  mem_.expire(clock_);
  if (n_ < 20 || !h_[n_ - 1].fix) return;
  int cnt = airTrail(scratch_, HIST);
  Thermal found;
  if (!findThermal(scratch_, cnt, found)) return;
  double wx = 0, wy = 0;
  windXY(wx, wy);
  mem_.update(h_[n_ - 1].lat, h_[n_ - 1].lon, found, clock_, wx, wy);
}

int FlightModel::thermals(ThermalRel* out, int maxN) const {
  if (n_ < 1 || !h_[n_ - 1].fix) return 0;
  double wx = 0, wy = 0;
  windXY(wx, wy);
  return mem_.relative(out, maxN, h_[n_ - 1].lat, h_[n_ - 1].lon, clock_, wx, wy);
}

// The EKF only learns about the wind while the heading changes (constant airspeed makes a straight
// glide uninformative), so an update counts for its age only if the course moved by 15 degrees or more
// within the last 10 s.
bool FlightModel::turnedRecently() const {
  if (n_ < 2 || !isfinite(cur_.course)) return false;
  for (int i = n_ > 10 ? n_ - 10 : 0; i < n_ - 1; i++) {
    if (!h_[i].fix) continue;
    float d = fabsf(wrap180(h_[i].course - cur_.course));
    if (isfinite(d) && d >= 15.0f) return true;
  }
  return false;
}

void FlightModel::updateWind() {
  if (n_ < WIND_S || !cur_.fix) return;      // the window needs a fix at its newest point too
  WindFit f = fitWind(h_ + n_ - WIND_S, WIND_S);
  if (!f.ok) return;
  if (!windValid_ || clock_ - windAt_ > 120000) {
    wx_ = f.east; wy_ = f.north;
    // First fit (or first after a long gap): the EKF has had no circle yet, so start it from the fit
    // instead of from zero wind. A confident EKF knows better than a single 30 s fit and is left alone
    // (but see the disagreement rule below).
    if (!ekf_.confident()) { ekf_.init((float)wx_, (float)wy_, 4); ekfInformedAt_ = clock_; }
    disagree_ = 0;
  }
  else { wx_ = 0.88 * wx_ + 0.12 * f.east; wy_ = 0.88 * wy_ + 0.12 * f.north; }   // smooth over ~8 s
  windAt_ = clock_;
  windValid_ = true;
  // A confident EKF that keeps disagreeing with the circle fit is wrong: the fit measures the wind directly
  // from a whole turn with strict gates, the EKF is an estimate that can be pulled off by a model it cannot
  // check (it was confident at 42 km/h in 12 km/h of wind). After DISAGREE_N accepted fits in a row that
  // are more than DISAGREE_MS apart, restart it from the smoothed fit.
  if (ekf_.confident() && hypot(ekf_.wx() - wx_, ekf_.wy() - wy_) > DISAGREE_MS) {
    if (++disagree_ >= DISAGREE_N) { ekf_.init((float)wx_, (float)wy_, 4); ekfInformedAt_ = clock_; disagree_ = 0; }
  } else disagree_ = 0;
}

bool FlightModel::windXY(double& wx, double& wy) const {
  if (ekf_.confident()) { wx = ekf_.wx(); wy = ekf_.wy(); return true; }
  if (!windValid_) return false;
  wx = wx_; wy = wy_;
  return true;
}

static Wind makeWind(double wx, double wy, uint32_t ageS) {
  Wind w;
  w.valid = true;
  w.speedKmh = (float)(hypot(wx, wy) * 3.6);
  w.toDeg = (float)fmod(atan2(wx, wy) * R2D + 360.0, 360.0);
  w.ageS = ageS;
  return w;
}

Wind FlightModel::wind() const {
  double wx, wy;
  if (!windXY(wx, wy)) return Wind();
  return makeWind(wx, wy, ekf_.confident() ? (clock_ - ekfInformedAt_) / 1000 : (clock_ - windAt_) / 1000);
}

Wind FlightModel::circleWind() const {
  if (!windValid_) return Wind();
  return makeWind(wx_, wy_, (clock_ - windAt_) / 1000);
}

int FlightModel::airTrail(AirPoint* out, int maxN) const {
  if (n_ < 2 || !h_[n_ - 1].fix) return 0;
  const double D2R = 0.017453292519943295;
  double lat0 = h_[n_ - 1].lat, lon0 = h_[n_ - 1].lon;
  double kx = 111320.0 * cos(lat0 * D2R), ky = 111320.0;
  double wx = 0, wy = 0;
  windXY(wx, wy);
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
