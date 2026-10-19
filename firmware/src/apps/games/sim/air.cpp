#include "apps/games/sim/air.h"

namespace sim {

namespace {
constexpr float kSunX = -0.30f, kSunY = -0.954f;     // horizontal direction toward the sun (south, a little west)
constexpr float kMaxTilt = 2600.0f;                  // m, the most a column leans from its trigger
constexpr int kReach = 2;
struct LevelParams { float strength, density, cloud, sink; };
constexpr LevelParams kLevel[Air::kLevels] = {
    {0.80f, 0.70f, -350.0f, 0.7f},       // weak
    {1.00f, 1.00f, 0.0f, 1.0f},          // medium: the tuned good day
    {1.25f, 1.15f, 250.0f, 1.3f},        // strong
    {1.50f, 1.25f, 450.0f, 1.6f},        // epic
};                            // cells searched around the query: 5 x 5
}  // namespace

void Air::setSeed(uint32_t seed) {
  seed_ = seed;
  cloudBase_ = 2200.0f + 600.0f * unit(hash3(seed, 0xC10D, 1)) + kLevel[level_].cloud;
  baseSink_ = (0.3f + 0.3f * unit(hash3(seed, 0xBA5E, 2))) * kLevel[level_].sink;
  nHouse_ = 0;
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

void Air::timing(uint32_t tk, bool house, float* period, float* offset) const {
  *period = house ? 500.0f + 200.0f * unit(hash32(tk ^ 0xA1)) : 600.0f + 400.0f * unit(hash32(tk ^ 0xA1));
  *offset = *period * unit(hash32(tk ^ 0xB2));
}

bool Air::thermalOf(const Tile& tl, uint32_t tk, int number, Th* out) const {
  float period, offset;
  timing(tk, tl.house, &period, &offset);
  const uint32_t h = hash3(tk, (uint32_t)number, 0x7117);
  if (!tl.house && unit(h) >= fminf(0.98f, (0.35f + 0.6f * tl.score) * kLevel[level_].density)) return false;
  const float u1 = unit(hash32(h ^ 1)), u2 = unit(hash32(h ^ 2)), u3 = unit(hash32(h ^ 3));
  out->x0 = tl.x; out->y0 = tl.y; out->z0 = tl.z;
  out->t0 = offset + number * period;
  out->life = (tl.house ? 1100.0f : 900.0f) + (tl.house ? 400.0f : 600.0f) * u3;     // 15 - 25 min, longer than the period: they overlap
  out->S = kLevel[level_].strength * (tl.house ? 2.6f + 1.8f * fpow(u1, 1.2f) : 2.0f + 2.6f * fpow(u1, 1.2f) * (0.5f + 0.5f * tl.score));
  out->R = 80.0f + 120.0f * u2;
  out->id = h;
  return true;
}

void Air::setHome(float hx, float hy) {
  nHouse_ = 0;
  float gh;
  if (!field_ || !field_->height(hx, hy, &gh)) return;
  float wx, wy;
  wind(gh, &wx, &wy);
  const float wsp = sqrtf(wx * wx + wy * wy);
  Tile best[kHouses];
  float bestV[kHouses], bestA[kHouses] = {0, 0, 0};
  // A spot below the launch whose bubble, leaning downwind as it climbs, passes over the launch area 150 - 800 m from the
  // launch point at launch height, with a glide to it that clears the terrain: that is how a real site's trigger spur
  // works. A site whose terrain has no such spot gets the best of a relaxed search (any spot 100 - 1100 m away).
  for (int pass = 0; pass < kHouses; pass++) {
    bestV[pass] = -1e9f;
    for (int strict = 1; strict >= 0 && bestV[pass] < -1e8f; strict--)
      for (int ring = 0; ring < 7; ring++)
        for (int b = 0; b < 12; b++) {
          const uint32_t idx = (uint32_t)(ring * 12 + b);
          const float d = 250.0f + 250.0f * ring + 60.0f * (unit(hash3(seed_, 0x4011, idx)) - 0.5f);
          const float a = (b + unit(hash3(seed_, 0x4012, idx))) * (kPi / 6.0f);
          const float x = hx + d * sinf(a), y = hy + d * cosf(a);
          float z;
          if (!field_->height(x, y, &z)) continue;
          const float drop = gh + 50.0f - z;                              // the bubble's climb to launch height
          if (strict ? (drop < 30.0f || drop > 500.0f) : (drop < 0.0f || drop > 700.0f)) continue;
          const float lean = fminf(drop * wsp / (0.8f * 3.0f), kMaxTilt);
          const float cx = x + wx / (wsp + 1e-3f) * lean - hx, cy = y + wy / (wsp + 1e-3f) * lean - hy;
          const float dc = sqrtf(cx * cx + cy * cy);
          if (strict ? (dc < 150.0f || dc > 800.0f) : (dc < 100.0f || dc > 1100.0f)) continue;
          if (strict) {                                                   // a glide from the launch (100 m up, 4:1 over the ground) must clear the terrain
            bool clear = true;
            for (int k = 1; k <= 8 && clear; k++) {
              float zz;
              const float f = k / 8.0f;
              clear = field_->height(hx + cx * f, hy + cy * f, &zz) && zz + 40.0f < gh + 100.0f - f * dc * 0.25f;
            }
            if (!clear) continue;
          }
          const float s = triggerScore(x, y);
          if (s < 0) continue;
          bool apart = true;                                              // the next ones: other directions
          for (int q = 0; q < pass; q++) apart = apart && fabsf(wrapPi(a - bestA[q])) > 0.9f;
          if (!apart) continue;
          const float v = s + 0.3f * (1.0f - fabsf(dc - 450.0f) / 450.0f) + 0.3f * unit(hash3(seed_, 0x4013, idx + (uint32_t)pass * 100u));
          if (v > bestV[pass]) {
            bestV[pass] = v; bestA[pass] = a;
            best[pass].x = x; best[pass].y = y; best[pass].z = z; best[pass].score = s;
          }
        }
  }
  for (int pass = 0; pass < kHouses; pass++)
    if (bestV[pass] > -1e8f) {
      house_[nHouse_] = best[pass];
      house_[nHouse_].house = true;
      house_[nHouse_].valid = true;
      nHouse_++;
    }
}

float Air::envelope(const Th& th, float t) const {
  const float a = t - th.t0;
  if (a < 0 || a > th.life) return 0;
  const float up = smoothstep(a / (0.25f * th.life));
  const float down = 1.0f - smoothstep((a - 0.65f * th.life) / (0.35f * th.life));
  return up * down;
}

float Air::cloudFactor(const Th& th, float t) {
  const float a = t - th.t0;
  if (a <= 0.2f * th.life || a >= 0.95f * th.life) return 0;
  return smoothstep((a - 0.2f * th.life) / (0.2f * th.life)) * (1.0f - smoothstep((a - 0.65f * th.life) / (0.3f * th.life)));
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
  auto add = [&](const Tile& tl, uint32_t tk) {
    float period, offset;
    timing(tk, tl.house, &period, &offset);
    const int nNow = (int)floorf((t - offset) / period);
    for (int k = nNow - 3; k <= nNow; k++) {                    // lives are up to 3 periods (houses)
      Th th;
      if (!thermalOf(tl, tk, k, &th)) continue;
      const float env = envelope(th, t);
      if (env <= 0) continue;
      Active a;
      a.th = th;
      a.env = env;
      columnCentre(th, z, t, &a.cx, &a.cy);
      a.R = th.R * (1.0f + 0.6f * fmaxf(z - th.z0, 0.0f) / 1000.0f);   // a widening top
      const float ddx = x - a.cx, ddy = y - a.cy;
      const float reach = range + 3.0f * a.R;
      if (ddx * ddx + ddy * ddy > reach * reach) continue;
      if (n < cap) out[n++] = a;
    }
  };
  const int cx = (int)floorf(x / kTile), cy = (int)floorf(y / kTile);
  for (int dy = -kReach; dy <= kReach; dy++)
    for (int dx = -kReach; dx <= kReach; dx++) {
      const int tx = cx + dx, ty = cy + dy;
      const Tile* tl = tile(tx, ty);
      if (tl) add(*tl, hash3(seed_, (uint32_t)tx, (uint32_t)ty));
    }
  for (int i = 0; i < nHouse_; i++) add(house_[i], hash3(seed_, 0x4005E + (uint32_t)i, 0x4011));
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
  if (!field_->gradient(x, y, 300.0f, &gx, &gy)) return 0;
  const float uHere = wx * gx + wy * gy;          // wind . slope: > 0 when the terrain rises in the wind direction (windward)
  float uUp = uHere;
  const float back = clampf(0.8f * agl, 100.0f, 500.0f);
  if (field_->gradient(x - wx / sp * back, y - wy / sp * back, 300.0f, &gx, &gy)) uUp = wx * gx + wy * gy;
  const float lift = fminf(fmaxf(fmaxf(uHere, uUp), 0.0f), 5.5f);
  const float lee = fmaxf(fminf(uHere, 0.0f), -4.0f);
  return 1.0f * lift * fexp(-agl / 260.0f) + lee * fexp(-agl / 380.0f);
}

void Air::sample(float x, float y, float z, float t, AirSample* out) const {
  wind(z, &out->wx, &out->wy);
  Active* act = scratch_;                      // a member: the loop task has 4 KB of stack
  const int n = collect(x, y, z, t, 0.0f, act, kMaxActive);
  float th = 0, core = 0, gxs = 0, gys = 0;
  for (int i = 0; i < n; i++) {
    const Active& a = act[i];
    const float dx = x - a.cx, dy = y - a.cy;
    const float r2 = (dx * dx + dy * dy) / (a.R * a.R);
    const float e1 = fexp(-r2), e2 = fexp(-r2 / 4.84f);
    const float dzr = z - a.th.z0;
    const float fh = smoothstep((dzr + 100.0f) / 250.0f);
    const float fc = 1.0f - smoothstep((z - (cloudBase_ - 250.0f)) / 250.0f);
    const float S = a.th.S * a.env * fh * fc;
    th += S * (1.25f * e1 - 0.25f * e2);
    const float k = -2.0f * S / (a.R * a.R) * (1.25f * e1 - 0.25f * e2 / 4.84f);       // d/dr2 of the profile, times 2 dx / R2
    gxs += k * dx;
    gys += k * dy;
    if (e1 * a.env > core) core = e1 * a.env;
  }
  out->thermal = th;
  out->gx = gxs;
  out->gy = gys;
  out->ridge = ridgeLift(x, y, z);
  // The general sink fades where the slope lifts the air (the lift is the net) and inside a core.
  out->base = -baseSink_ * (1.0f - clampf(core, 0.0f, 1.0f)) * (1.0f - clampf(out->ridge / 1.2f, 0.0f, 1.0f));
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

namespace sim {

int Air::columns(float x, float y, float t, float range, ColumnInfo* out, int cap) const {
  Active* act = scratch_;
  const int n = collect(x, y, cloudBase_, t, range, act, kMaxActive);
  int m = 0;
  float bwx, bwy;
  wind(cloudBase_, &bwx, &bwy);
  for (int i = 0; i < n && m < cap; i++) {
    const Th& th = act[i].th;
    ColumnInfo& o = out[m];
    o.id = th.id; o.x0 = th.x0; o.y0 = th.y0; o.z0 = th.z0;
    o.S = th.S; o.R0 = th.R; o.strength = th.S * act[i].env;
    o.t0 = th.t0; o.life = th.life;
    o.cloud = cloudFactor(th, t);
    o.cx = act[i].cx; o.cy = act[i].cy;
    const float a = t - th.t0;
    const float drift = 0.10f * fmaxf(a - 0.2f * th.life, 0.0f);          // the cumulus leaves its column's top downwind
    o.cloudX = o.cx + bwx * drift;
    o.cloudY = o.cy + bwy * drift;
    o.width = (450.0f + 140.0f * th.S) * (0.3f + 0.7f * o.cloud);
    m++;
  }
  return m;
}

void Air::columnPoint(const ColumnInfo& c, float z, float t, float* x, float* y) const {
  Th th;
  th.x0 = c.x0; th.y0 = c.y0; th.z0 = c.z0; th.t0 = c.t0; th.life = c.life; th.S = c.S; th.R = c.R0; th.id = c.id;
  columnCentre(th, z, t, x, y);
}

}  // namespace sim
