#include "apps/vario/core/sim.h"

static const double D2R = 0.017453292519943295;

static float wrap180(float a) {
  a = fmodf(a, 360.0f);
  if (a > 180) a -= 360;
  if (a < -180) a += 360;
  return a;
}

void FlightSim::reset(uint32_t nowMs) {
  t0_ = last_ = nowMs;
  x_ = y_ = 0;
  alt_ = 1500;
  course_ = 90;
}

bool FlightSim::step(uint32_t now, Sample& out) {
  if (now - last_ < 330) return false;
  float dt = (now - last_) / 1000.0f;
  last_ = now;
  float t = fmodf((now - t0_) / 1000.0f, 150.0f);
  const float tcx = 900, tcy = 0;            // thermal centre 900 m east of the start
  float spd, turn, v;
  if (t < 40 || t >= 110) {                  // glide
    spd = 38; turn = 0; v = -1.1f;
    if (t >= 110) turn = wrap180(90 - course_) * 0.3f;
  } else {                                   // circle in the thermal
    spd = 30; turn = 18;
    float dx = (float)(x_ - tcx), dy = (float)(y_ - tcy);
    float d = sqrtf(dx * dx + dy * dy);
    v = 2.6f * expf(-(d / 55.0f) * (d / 55.0f)) - 0.5f;
  }
  course_ = fmodf(course_ + turn * dt + 360, 360);
  float ms = spd / 3.6f;
  const float windE = -3.33f, windN = 0;     // 12 km/h toward 270
  float gx = ms * sinf((float)(course_ * D2R)) + windE;
  float gy = ms * cosf((float)(course_ * D2R)) + windN;
  x_ += gx * dt;
  y_ += gy * dt;
  alt_ += v * dt;

  Sample s;
  s.fix = true;
  s.lat = 42.0 + y_ / 111320.0;
  s.lon = 23.0 + x_ / (111320.0 * cos(42.0 * D2R));
  s.alt = (float)alt_;
  s.speedKmh = sqrtf(gx * gx + gy * gy) * 3.6f;
  s.course = fmodf(atan2f(gx, gy) / (float)D2R + 360, 360);
  s.vario = v + 0.08f * sinf(now / 300.0f);
  s.pressure = 1013.25f * powf(1 - (float)alt_ / 44330.0f, 5.255f);
  s.battPct = 80;
  uint32_t secs = 12 * 3600 + (now - t0_) / 1000;
  s.hh = (secs / 3600) % 24; s.mm = (secs / 60) % 60; s.ss = secs % 60;
  out = s;
  return true;
}
