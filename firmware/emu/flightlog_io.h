#pragma once
// PC side of the flight log: the decoder for the byte stream of apps/vario/core/flight_log.h (pages of platform/storage/flash_log.h)
// and the offline evaluator that replays a log through DeadReckoner. Header only; used by tests.cpp and `emu logeval`.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include "apps/vario/core/deadreckon.h"
#include "apps/vario/core/flight_log.h"
#include "platform/storage/flash_log.h"

namespace flightlog_io {

struct Line { int session; uint32_t rxMs; uint8_t hh, mm, ss; bool fix; double lat, lon; float speedKmh, courseDeg, vario; };
struct YawPt { int session; uint32_t ms; float dps; };
struct Log {
  std::vector<Line> lines;
  std::vector<YawPt> yaw;
  std::vector<uint32_t> sessions;
  int badPages = 0;
};

inline uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

// `data` = the raw pages as downloaded (page 1 of the region first).
inline Log decode(const uint8_t* data, size_t n) {
  Log L;
  int session = -1;
  for (size_t pg = 0; pg + storage::kLogPage <= n; pg += storage::kLogPage) {
    const uint8_t* p = data + pg;
    if (p[0] != storage::kLogPageMark) { L.badPages++; continue; }
    size_t i = 1;
    while (i < storage::kLogPage) {
      const uint8_t t = p[i];
      const size_t room = storage::kLogPage - i;
      if (t == flightlog::kRecPad) break;
      if (t == flightlog::kRecSession && room >= 7) {
        L.sessions.push_back(rd32(p + i + 1));
        session = (int)L.sessions.size() - 1;
        i += 7;
      } else if (t == flightlog::kRecLine && room >= 23) {
        Line l;
        l.session = session < 0 ? 0 : session;
        l.rxMs = rd32(p + i + 1);
        l.hh = p[i + 5]; l.mm = p[i + 6]; l.ss = p[i + 7];
        l.fix = p[i + 8] & 1;
        l.lat = (int32_t)rd32(p + i + 9) * 1e-7;
        l.lon = (int32_t)rd32(p + i + 13) * 1e-7;
        const uint16_t sp = rd16(p + i + 17), co = rd16(p + i + 19);
        const int16_t va = (int16_t)rd16(p + i + 21);
        l.speedKmh = sp == flightlog::kNoU16 ? NAN : sp * 0.1f;
        l.courseDeg = co == flightlog::kNoU16 ? NAN : co * 0.1f;
        l.vario = va == flightlog::kNoI16 ? NAN : va * 0.01f;
        L.lines.push_back(l);
        i += 23;
      } else if (t == flightlog::kRecYaw && room >= 8 && room >= 8u + p[i + 7]) {
        const int cnt = p[i + 7];
        int32_t v = (int16_t)rd16(p + i + 5);
        const uint32_t ms = rd32(p + i + 1);
        L.yaw.push_back({session < 0 ? 0 : session, ms, v * flightlog::kYawUnitDps});
        for (int k = 0; k < cnt; k++) {
          v += (int8_t)p[i + 8 + k] * flightlog::kYawStepUnits;
          L.yaw.push_back({session < 0 ? 0 : session, ms + flightlog::kYawGridMs * (k + 1), v * flightlog::kYawUnitDps});
        }
        i += 8 + cnt;
      } else {
        L.badPages++;
        break;
      }
    }
  }
  return L;
}

// ---- evaluator ----

constexpr double kMPerDeg = 111194.9266;
inline float wrap180(float d) { d = fmodf(d, 360.0f); if (d > 180) d -= 360; if (d < -180) d += 360; return d; }

struct Fix { uint32_t rx; double x, y; float speedKmh, course; };   // x, y: metres east / north of the session's first fix

struct Stats {
  std::vector<float> v;
  void add(float x) { v.push_back(x); }
  float mean() const { double s = 0; for (float x : v) s += x; return v.empty() ? NAN : (float)(s / v.size()); }
  float pct(float q) { if (v.empty()) return NAN; std::sort(v.begin(), v.end()); return v[(size_t)(q * (v.size() - 1))]; }
};

struct EvalOptions {
  float latencyMs = NAN;       // NaN: use the measured lag
  bool wind = false;           // feed the reckoner a drift wind (mean ground velocity of the last full circle)
  bool verbose = true;
};

struct EvalResult {
  int sessions = 0, fixes = 0, points = 0;
  float fixRateHz = NAN, lagMs = NAN, lagRmsDeg = NAN, lagRms0Deg = NAN, usedLatencyMs = NAN;
  int lagIntervals = 0;
  float holdPosMean = NAN, drPosMean = NAN, holdPosP90 = NAN, drPosP90 = NAN, holdCrsMean = NAN, drCrsMean = NAN, holdCrsP90 = NAN, drCrsP90 = NAN;
  float turnHoldPos = NAN, turnDrPos = NAN, turnHoldCrs = NAN, turnDrCrs = NAN;
  int turnPoints = 0;
};

// The cumulative turned angle (deg) of one session's yaw samples, interpolated.
struct Psi {
  std::vector<uint32_t> t;
  std::vector<double> a;
  void build(const std::vector<YawPt>& y, int session) {
    t.clear(); a.clear();
    double acc = 0;
    for (size_t i = 0; i < y.size(); i++) {
      if (y[i].session != session) continue;
      if (!t.empty()) acc += y[i].dps * (y[i].ms - t.back()) * 0.001;   // the rate holds for the time since the previous sample
      t.push_back(y[i].ms);
      a.push_back(acc);
    }
  }
  bool at(double ms, double& out) const {
    if (t.size() < 2 || ms < t.front() || ms > t.back()) return false;
    size_t k = std::upper_bound(t.begin(), t.end(), (uint32_t)ms) - t.begin();
    if (k >= t.size()) k = t.size() - 1;
    const double f = (ms - t[k - 1]) / std::max(1.0, (double)(t[k] - t[k - 1]));
    out = a[k - 1] + (a[k] - a[k - 1]) * f;
    return true;
  }
};

inline EvalResult evaluate(const Log& log, const EvalOptions& opt) {
  EvalResult R;
  R.sessions = (int)log.sessions.size();
  Stats hp, dp, hc, dc, thp, tdp, thc, tdc;
  double rateSum = 0; int rateN = 0;
  // pass 1: per session, distinct fixes and the gyro-vs-GPS lag
  struct Sess { std::vector<Fix> fixes; Psi psi; };
  std::vector<Sess> S(std::max<size_t>(log.sessions.size(), 1));
  double lat0 = NAN, lon0 = 0, cosl = 1;
  for (const Line& l : log.lines) {
    if (!l.fix || !isfinite(l.courseDeg) || !isfinite(l.speedKmh)) continue;
    if (!isfinite(lat0)) { lat0 = l.lat; lon0 = l.lon; cosl = cos(lat0 * M_PI / 180); }
    Fix f{l.rxMs, (l.lon - lon0) * kMPerDeg * cosl, (l.lat - lat0) * kMPerDeg, l.speedKmh, l.courseDeg};
    auto& v = S[l.session].fixes;
    if (!v.empty() && fabs(v.back().x - f.x) < 0.005 && fabs(v.back().y - f.y) < 0.005 && v.back().course == f.course) continue;   // the same fix again
    v.push_back(f);
  }
  for (size_t s = 0; s < S.size(); s++) S[s].psi.build(log.yaw, (int)s);
  // lag: GPS course change between fixes >= 2 s apart against the gyro turn over the same interval shifted by tau
  auto lagCost = [&](double tau, int* nOut) {
    double sum = 0; int n = 0;
    for (auto& se : S) {
      const auto& F = se.fixes;
      for (size_t i = 0, j = 1; i + 1 < F.size(); i++) {
        if (j <= i) j = i + 1;
        while (j < F.size() && F[j].rx - F[i].rx < 2000) j++;
        if (j >= F.size()) break;
        if (F[i].speedKmh < 2 || F[j].speedKmh < 2) continue;
        double a0, a1;
        if (!se.psi.at(F[i].rx - tau, a0) || !se.psi.at(F[j].rx - tau, a1)) continue;
        if (fabs(a1 - a0) < 10) continue;                       // not turning: no information
        const double d = wrap180(F[j].course - F[i].course) - (a1 - a0);
        sum += d * d; n++;
      }
    }
    *nOut = n;
    return n ? sqrt(sum / n) : NAN;
  };
  double best = INFINITY, bestTau = NAN; int bn = 0;
  for (double tau = -500; tau <= 3000; tau += 20) {
    int n; const double c = lagCost(tau, &n);
    if (n >= 3 && c < best) { best = c; bestTau = tau; bn = n; }
  }
  if (isfinite(bestTau)) {
    R.lagMs = (float)bestTau; R.lagRmsDeg = (float)best; R.lagIntervals = bn;
    int n; R.lagRms0Deg = (float)lagCost(0, &n);
  }
  R.usedLatencyMs = isfinite(opt.latencyMs) ? opt.latencyMs : (isfinite(R.lagMs) ? R.lagMs : 800.0f);

  // pass 2: reckon and compare against the next fixes
  for (size_t s = 0; s < S.size(); s++) {
    const auto& F = S[s].fixes;
    R.fixes += (int)F.size();
    if (F.size() >= 2) { rateSum += (F.size() - 1) * 1000.0 / std::max<double>(1, F.back().rx - F.front().rx) ; rateN++; }
    DeadReckoner dr;
    dr.setLatencyMs(R.usedLatencyMs);
    // the events of the session in time order
    std::vector<const YawPt*> Y;
    for (const YawPt& y : log.yaw) if (y.session == (int)s) Y.push_back(&y);
    size_t yi = 0;
    const double L = R.usedLatencyMs;
    for (size_t i = 0; i + 1 < F.size(); i++) {
      // gyro samples up to the fix's arrival, then the fix
      while (yi < Y.size() && Y[yi]->ms <= F[i].rx) dr.yaw(Y[yi]->ms, Y[yi]->dps), yi++;
      if (opt.wind) {                                             // drift over the last 25 s if they hold a full circle
        double a0, a1;
        if (S[s].psi.at(F[i].rx, a1) && S[s].psi.at(F[i].rx - 25000.0, a0) && fabs(a1 - a0) > 330) {
          size_t k = i;
          while (k > 0 && F[i].rx - F[k - 1].rx <= 25000) k--;
          if (k < i) {
            double vx = 0, vy = 0; int m = 0;
            for (size_t q = k; q <= i; q++) { vx += F[q].speedKmh / 3.6 * sin(F[q].course * M_PI / 180); vy += F[q].speedKmh / 3.6 * cos(F[q].course * M_PI / 180); m++; }
            dr.setWind(true, (float)(vx / m), (float)(vy / m));
          }
        }
      }
      dr.fix(F[i].rx, true, 0, 0, F[i].speedKmh, F[i].course);   // position kept in metres here (lat/lon 0); at() gives eastM / northM
      const uint32_t span = F[i + 1].rx - F[i].rx;
      if (span == 0 || span > 3000) continue;
      // while now moves through the interval, feed gyro samples as they arrive
      for (uint32_t dt = 100; dt < span; dt += 100) {
        const uint32_t now = F[i].rx + dt;
        while (yi < Y.size() && Y[yi]->ms <= now) dr.yaw(Y[yi]->ms, Y[yi]->dps), yi++;
        auto e = dr.at(now);
        if (!e.valid || !e.gyroLive) continue;
        // truth: the fixes interpolated at the instant the displayed state describes, now - L
        // fix k describes the instant rx_k - L; the truth at `now` lies between the two fixes around it (later ones, offline)
        size_t k = i;
        while (k + 1 < F.size() && (double)F[k + 1].rx - L <= now) k++;
        if (k + 1 >= F.size()) continue;
        const double t0 = (double)F[k].rx - L, t1 = (double)F[k + 1].rx - L;
        const double f = (now - t0) / (t1 - t0);
        if (t1 - t0 > 3000) continue;
        const double tx = F[k].x + (F[k + 1].x - F[k].x) * f, ty = F[k].y + (F[k + 1].y - F[k].y) * f;
        const float tc = F[k].course + wrap180(F[k + 1].course - F[k].course) * (float)f;
        const float hold = (float)hypot(F[i].x - tx, F[i].y - ty);
        const float drp = (float)hypot(F[i].x + e.eastM - tx, F[i].y + e.northM - ty);
        const float hc_ = fabsf(wrap180(F[i].course - tc)), dc_ = fabsf(wrap180(e.courseDeg - tc));
        hp.add(hold); dp.add(drp); hc.add(hc_); dc.add(dc_);
        R.points++;
        double a0, a1;
        if (S[s].psi.at(F[i].rx, a0) && S[s].psi.at(F[i + 1].rx, a1) && fabs(a1 - a0) / (span * 0.001) > 8.0) {   // turning faster than 8 deg/s
          thp.add(hold); tdp.add(drp); thc.add(hc_); tdc.add(dc_); R.turnPoints++;
        }
      }
      (void)L;
    }
  }
  R.fixRateHz = rateN ? (float)(rateSum / rateN) : NAN;
  R.holdPosMean = hp.mean(); R.drPosMean = dp.mean(); R.holdPosP90 = hp.pct(0.9f); R.drPosP90 = dp.pct(0.9f);
  R.holdCrsMean = hc.mean(); R.drCrsMean = dc.mean(); R.holdCrsP90 = hc.pct(0.9f); R.drCrsP90 = dc.pct(0.9f);
  R.turnHoldPos = thp.mean(); R.turnDrPos = tdp.mean(); R.turnHoldCrs = thc.mean(); R.turnDrCrs = tdc.mean();
  return R;
}

inline void print(const EvalResult& R) {
  printf("sessions %d, distinct fixes %d (%.2f per second)\n", R.sessions, R.fixes, R.fixRateHz);
  if (isfinite(R.lagMs))
    printf("gyro vs GPS course lag: %.0f ms (rms residual %.1f deg, %.1f deg with no lag; %d turning intervals)\n", R.lagMs, R.lagRmsDeg, R.lagRms0Deg, R.lagIntervals);
  else
    printf("gyro vs GPS course lag: not measurable (no turning intervals with gyro data)\n");
  printf("dead reckoning with latency %.0f ms, %d comparison points (%d while turning > 8 deg/s)\n", R.usedLatencyMs, R.points, R.turnPoints);
  printf("             hold last fix    dead reckoned\n");
  printf("position m   mean %6.2f p90 %6.2f   mean %6.2f p90 %6.2f\n", R.holdPosMean, R.holdPosP90, R.drPosMean, R.drPosP90);
  printf("course deg   mean %6.2f p90 %6.2f   mean %6.2f p90 %6.2f\n", R.holdCrsMean, R.holdCrsP90, R.drCrsMean, R.drCrsP90);
  printf("turning only position m %.2f -> %.2f, course deg %.2f -> %.2f\n", R.turnHoldPos, R.turnDrPos, R.turnHoldCrs, R.turnDrCrs);
}

}  // namespace flightlog_io
