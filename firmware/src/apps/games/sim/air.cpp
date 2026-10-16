#include "apps/games/sim/air.h"

namespace sim {

namespace {
constexpr float kSunX = -0.30f, kSunY = -0.954f;     // horizontal direction toward the sun (south, a little west)
constexpr float kMaxTilt = 2600.0f;                  // m, the most a column leans from its trigger
constexpr int kReach = 2;                            // cells searched around the query: 5 x 5
}  // namespace

void Air::setSeed(uint32_t seed) {
  seed_ = seed;
  cloudBase_ = 2000.0f + 600.0f * unit(hash3(seed, 0xC10D, 1));
  baseSink_ = 0.5f + 0.5f * unit(hash3(seed, 0xBA5E, 2));
  clearTiles();
}

void Air::setWind(float fromDeg, float speed, float refAlt) {
  const float to = (fromDeg + 180.0f) * kDeg2Rad;
  windToX_ = sinf(to);
  windToY_ = cosf(to);
  windSpeed_ = speed < 0 ? 0 : speed;
  windRefAlt_ = refAlt;
}

void Air::wind(float z, float* wx, float* wy) const {
  const float k = clampf(1.0f + 0.4f * (z - windRefAlt_) / 1000.0f, 0.6f, 2.2f);
  *wx = windToX_ * windSpeed_ * k;
  *wy = windToY_ * windSpeed_ * k;
}

float Air::triggerScore(float x, float y) const {
  if (!field_) return -1;
  float gx, gy, h, a, b, c, d;
  if (!field_->gradient(x, y, 150.0f, &gx, &gy)) return -1;
  if (!field_->height(x, y, &h) || !field_->height(x + 600, y, &a) || !field_->height(x - 600, y, &b) ||
      !field_->height(x, y + 600, &c) || !field_->height(x, y - 600, &d))
    return 0;
  const float slope = sqrtf(gx * gx + gy * gy);
  float facing = 0;
  if (slope > 1e-4f) facing = fmaxf(0.0f, (-gx * kSunX - gy * kSunY) / slope) * smoothstep(slope / 0.35f);
  const float ridge = clampf((h - 0.25f * (a + b + c + d)) / 150.0f, 0.0f, 1.0f);
  return clampf(0.15f + 0.6f * facing + 0.4f * ridge, 0.0f, 1.0f);
}

const Air::Tile* Air::tile(int tx, int ty) const {
  const uint32_t key = hash3(0x71E5, (uint32_t)tx, (uint32_t)ty);
  const int base = (int)(key % kSlots);
  int free = -1;
  for (int p = 0; p < 4; p++) {
    Tile& s = tiles_[(base + p) % kSlots];
    if (s.valid && s.tx == tx && s.ty == ty) return &s;
    if (!s.valid && free < 0) free = (base + p) % kSlots;
  }
  // Compute: of 9 jittered candidates the best-scoring one.
  Tile t;
  t.tx = tx; t.ty = ty;
  float best = -1;
  for (int j = 0; j < 3; j++)
    for (int i = 0; i < 3; i++) {
      const uint32_t h = hash3(seed_ ^ 0x5EED, (uint32_t)(tx * 3 + i), (uint32_t)(ty * 3 + j));
      const float x = (tx + (i + 0.5f + 0.6f * (unit(h) - 0.5f)) / 3.0f) * kTile;
      const float y = (ty + (j + 0.5f + 0.6f * (unit(hash32(h)) - 0.5f)) / 3.0f) * kTile;
      float g;
      if (!field_ || !field_->height(x, y, &g)) return nullptr;      // the terrain is not loaded here: no thermals yet
      const float s = triggerScore(x, y);
      if (s < 0) return nullptr;
      if (s > best) { best = s; t.x = x; t.y = y; t.z = g; t.score = s; }
    }
  t.valid = true;
  Tile& slot = tiles_[free >= 0 ? free : (base + (int)(hash32(key) & 3)) % kSlots];
  slot = t;
  return &slot;
}

bool Air::thermalOf(const Tile& tl, int tx, int ty, int number, Th* out) const {
  const uint32_t tk = hash3(seed_, (uint32_t)tx, (uint32_t)ty);
  const float period = 900.0f + 600.0f * unit(hash32(tk ^ 0xA1));
  const float offset = period * unit(hash32(tk ^ 0xB2));
  const uint32_t h = hash3(tk, (uint32_t)number, 0x7117);
  if (unit(h) >= 0.25f + 0.65f * tl.score) return false;
  const float u1 = unit(hash32(h ^ 1)), u2 = unit(hash32(h ^ 2)), u3 = unit(hash32(h ^ 3));
  out->x0 = tl.x; out->y0 = tl.y; out->z0 = tl.z;
  out->t0 = offset + number * period;
  out->life = 600.0f + 600.0f * u3;
  out->S = 1.0f + 4.0f * powf(u1, 1.5f) * (0.35f + 0.65f * tl.score);
  out->R = 50.0f + 100.0f * u2;
  out->id = h;
  return true;
}

float Air::envelope(const Th& th, float t) const {
  const float a = t - th.t0;
  if (a < 0 || a > th.life) return 0;
  const float up = smoothstep(a / (0.25f * th.life));
  const float down = 1.0f - smoothstep((a - 0.65f * th.life) / (0.35f * th.life));
  return up * down;
}

void Air::columnCentre(const Th& th, float z, float t, float* cx, float* cy) const {
  const float dz = clampf(z - th.z0, 0.0f, 1500.0f);
  float wx, wy;
  wind(th.z0 + 0.5f * dz, &wx, &wy);
  float lean = dz / (0.8f * th.S);
  const float len = sqrtf(wx * wx + wy * wy) * lean;
  if (len > kMaxTilt) lean *= kMaxTilt / len;
  float sx, sy;
  wind(th.z0, &sx, &sy);
  const float age = clampf(t - th.t0, 0.0f, th.life);
  *cx = th.x0 + wx * lean + 0.05f * sx * age;
  *cy = th.y0 + wy * lean + 0.05f * sy * age;
}

int Air::collect(float x, float y, float z, float t, float range, Active* out, int cap) const {
  int n = 0;
  const int cx = (int)floorf(x / kTile), cy = (int)floorf(y / kTile);
  for (int dy = -kReach; dy <= kReach; dy++)
    for (int dx = -kReach; dx <= kReach; dx++) {
      const int tx = cx + dx, ty = cy + dy;
      const Tile* tl = tile(tx, ty);
      if (!tl) continue;
      const uint32_t tk = hash3(seed_, (uint32_t)tx, (uint32_t)ty);
      const float period = 900.0f + 600.0f * unit(hash32(tk ^ 0xA1));
      const float offset = period * unit(hash32(tk ^ 0xB2));
      const int nNow = (int)floorf((t - offset) / period);
      for (int k = nNow - 1; k <= nNow; k++) {
        Th th;
        if (!thermalOf(*tl, tx, ty, k, &th)) continue;
        const float env = envelope(th, t);
        if (env <= 0) continue;
        Active a;
        a.th = th;
        a.env = env;
        columnCentre(th, z, t, &a.cx, &a.cy);
        a.R = th.R * (1.0f + 0.4f * fmaxf(z - th.z0, 0.0f) / 1000.0f);
        const float ddx = x - a.cx, ddy = y - a.cy;
        const float reach = range + 3.0f * a.R;
        if (ddx * ddx + ddy * ddy > reach * reach) continue;
        if (n < cap) out[n++] = a;
      }
    }
  return n;
}

float Air::ridgeLift(float x, float y, float z) const {
  if (!field_) return 0;
  float g0;
  if (!field_->height(x, y, &g0)) return 0;
  float wx, wy;
  wind(z, &wx, &wy);
  const float sp = sqrtf(wx * wx + wy * wy);
  if (sp < 0.5f) return 0;
  const float agl = fmaxf(z - g0, 0.0f);
  float gx, gy;
  if (!field_->gradient(x, y, 150.0f, &gx, &gy)) return 0;
  const float uHere = wx * gx + wy * gy;          // wind . slope: > 0 when the terrain rises in the wind direction (windward)
  float uUp = uHere;
  const float back = clampf(0.8f * agl, 100.0f, 500.0f);
  if (field_->gradient(x - wx / sp * back, y - wy / sp * back, 150.0f, &gx, &gy)) uUp = wx * gx + wy * gy;
  const float lift = fminf(fmaxf(fmaxf(uHere, uUp), 0.0f), 5.5f);
  const float lee = fmaxf(fminf(uHere, 0.0f), -4.0f);
  return 0.9f * lift * expf(-agl / 220.0f) + lee * expf(-agl / 380.0f);
}

void Air::sample(float x, float y, float z, float t, AirSample* out) const {
  wind(z, &out->wx, &out->wy);
  Active* act = scratch_;                      // a member: the loop task has 4 KB of stack
  const int n = collect(x, y, z, t, 0.0f, act, kMaxActive);
  float th = 0, core = 0;
  for (int i = 0; i < n; i++) {
    const Active& a = act[i];
    const float dx = x - a.cx, dy = y - a.cy;
    const float r2 = (dx * dx + dy * dy) / (a.R * a.R);
    const float e1 = expf(-r2), e2 = expf(-r2 / 4.84f);
    const float dzr = z - a.th.z0;
    const float fh = smoothstep((dzr + 100.0f) / 250.0f);
    const float fc = 1.0f - smoothstep((z - (cloudBase_ - 250.0f)) / 250.0f);
    const float S = a.th.S * a.env * fh * fc;
    th += S * (1.25f * e1 - 0.25f * e2);
    if (e1 * a.env > core) core = e1 * a.env;
  }
  out->thermal = th;
  out->ridge = ridgeLift(x, y, z);
  out->base = -baseSink_ * (1.0f - clampf(core, 0.0f, 1.0f));
  out->wz = clampf(out->thermal + out->ridge + out->base, -10.0f, 10.0f);
}

int Air::nearby(float x, float y, float z, float t, float range, ThermalInfo* out, int cap) const {
  Active* act = scratch_;
  const int n = collect(x, y, z, t, range, act, kMaxActive);
  int m = 0;
  for (int i = 0; i < n && m < cap; i++) {
    const float dx = act[i].cx - x, dy = act[i].cy - y;
    if (dx * dx + dy * dy > range * range) continue;
    out[m].x = act[i].cx;
    out[m].y = act[i].cy;
    out[m].radius = act[i].R;
    out[m].strength = act[i].th.S * act[i].env;
    out[m].id = act[i].th.id;
    m++;
  }
  return m;
}

}  // namespace sim
