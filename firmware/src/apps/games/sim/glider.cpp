#include "apps/games/sim/glider.h"

namespace sim {

namespace {
struct Pt { float kmh, sink; };
const Pt kPolar[] = {
    {20.0f, 2.40f}, {26.0f, 1.55f}, {30.0f, 1.19f}, {33.0f, 1.10f}, {37.0f, 1.20f}, {40.0f, 1.38f},
    {44.0f, 1.62f}, {48.0f, 1.90f}, {52.0f, 2.20f}, {60.0f, 3.10f},
};
const int kN = sizeof kPolar / sizeof kPolar[0];
}  // namespace

float Polar::sinkKmh(float kmh) {
  if (kmh <= kPolar[0].kmh) return kPolar[0].sink;
  for (int i = 1; i < kN; i++) {
    if (kmh <= kPolar[i].kmh) {
      const float t = (kmh - kPolar[i - 1].kmh) / (kPolar[i].kmh - kPolar[i - 1].kmh);
      return lerpf(kPolar[i - 1].sink, kPolar[i].sink, t);
    }
  }
  return kPolar[kN - 1].sink;
}

float Polar::sink(float kmh, float n) {
  if (n < 1.0f) n = 1.0f;
  const float v = (kmh < 20.0f ? 20.0f : kmh) * kKmh2Ms;
  return sinkKmh(kmh) + (n * n - 1.0f) * kInduced / v;
}

float Polar::bestGlideKmh(float* ratio) {
  float best = 0, bestV = kTrimKmh;
  for (float v = 28.0f; v <= 60.0f; v += 0.25f) {
    const float r = (v / 3.6f) / sinkKmh(v);
    if (r > best) { best = r; bestV = v; }
  }
  if (ratio) *ratio = best;
  return bestV;
}

void Glider::reset(float x0, float y0, float z0, float heading0) {
  *this = Glider();
  x = x0; y = y0; z = z0; heading = heading0;
  gx = airspeed * sinf(heading);
  gy = airspeed * cosf(heading);
}

void Glider::step(const Controls& c, float wx, float wy, float wz, float dt) {
  if (!(dt > 0)) return;
  // Pilot inputs reach the wing smoothed: the bar and the brake are slow controls.
  const float kc = lagFactor(dt, 0.35f);
  barSmooth += (clampf(c.bar, 0, 1) - barSmooth) * kc;
  brakeSmooth += (clampf(c.brake, 0, 1) - brakeSmooth) * kc;

  // Bank: first-order lag to the commanded angle.
  const float bankCmd = clampf(c.bank, -1.0f, 1.0f) * kMaxBank;
  bank += (bankCmd - bank) * lagFactor(dt, kRollTau);
  load = 1.0f / cosf(bank);
  const float rootN = sqrtf(load);

  // Airspeed target from bar and brake.
  float targetKmh = Polar::kTrimKmh + barSmooth * (Polar::kBarKmh - Polar::kTrimKmh)
                    - brakeSmooth * (Polar::kTrimKmh - Polar::kSlowKmh);
  const float stallKmh = Polar::kStallKmh * rootN;
  if (stalled) targetKmh = brakeSmooth > 0.3f ? 20.0f : fmaxf(targetKmh, Polar::kTrimKmh);
  airspeed += (targetKmh * kKmh2Ms - airspeed) * lagFactor(dt, stalled ? 2.5f : kSpeedTau);

  // Stall with hysteresis.
  const float kmh = airspeed * 3.6f;
  if (!stalled && kmh < stallKmh) stalled = true;
  else if (stalled && kmh > stallKmh + 3.0f && brakeSmooth <= 0.3f) stalled = false;
  stallBlend += ((stalled ? 1.0f : 0.0f) - stallBlend) * lagFactor(dt, 0.5f);

  float s = Polar::sink(kmh, load);
  s = lerpf(s, kStallSink, stallBlend);
  sinkRate = s;

  // Coordinated turn.
  const float v = airspeed < 6.0f ? 6.0f : airspeed;
  turnRate = kG * tanf(bank) / v;
  heading = wrapPi(heading + turnRate * dt);
  if (heading < 0) heading += 2 * kPi;

  const float vh = airspeed * (1.0f - 0.15f * stallBlend);    // a stalled wing also loses forward speed
  gx = vh * sinf(heading) + wx;
  gy = vh * cosf(heading) + wy;
  vz = wz - s;
  x += gx * dt;
  y += gy * dt;
  z += vz * dt;
}

}  // namespace sim
