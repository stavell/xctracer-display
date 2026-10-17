#pragma once
// Playability analysis of the flight sim's air, shared by the tool (sim_reach.cpp) and the tests: how many thermals a
// pilot can glide to from a takeoff, and a scripted "auto-pilot" (fly to the nearest working thermal, circle in it,
// leave when it dies or the cloud base is near) that stands for a pilot who knows where the lift is (assist on).
#include <math.h>
#include <stdio.h>
#include "apps/games/sim/world.h"

namespace simreach {
using namespace sim;

constexpr float kUsable = 1.5f;        // m/s: a thermal this strong (life cycle applied) is worth turning in

struct Reach {
  int working = 0;                     // usable thermals within the search range
  int reachable = 0;                   // ... of those, reached by a straight trim glide with >= 30 m left above the ground
  int withMargin = 0;                  // ... also reached when the air sinks 0.5 m/s more than it does
  float nearest = -1;                  // distance (m) to the nearest usable thermal, -1 if none
  float nearestReach = -1;             // distance of the nearest one that is reachable
};

inline float steerTo(float cur, float want) { return clampf(wrapPi(want - cur) * 2.5f, -1.0f, 1.0f); }

// Straight trim glide (no bar, no brake) toward thermal `id`, re-aimed at its centre every step. Returns the height above the
// ground on arrival (inside its core radius), or -1 when it is not reached (landed, thermal gone, 12 minutes).
inline float glideTo(const World& w, uint32_t id, float t0, float extraSink = 0.0f) {
  Glider g = w.glider();
  const Air& air = w.air();
  const float dt = 0.1f;
  float t = t0;
  for (int i = 0; i < 7200; i++, t += dt) {
    static ThermalInfo th[Air::kMaxInfo];
    const int n = air.nearby(g.x, g.y, g.z, t, 3600.0f, th, Air::kMaxInfo);
    int k = -1;
    for (int j = 0; j < n; j++) if (th[j].id == id) k = j;
    if (k < 0) return -1;
    const float dx = th[k].x - g.x, dy = th[k].y - g.y;
    if (dx * dx + dy * dy < th[k].radius * th[k].radius) {
      float gh;
      return w.cache().height(g.x, g.y, &gh) ? g.z - gh : -1;
    }
    Controls c;
    c.bank = steerTo(g.heading, atan2f(dx, dy));
    AirSample s;
    air.sample(g.x, g.y, g.z, t, &s);
    g.step(c, s.wx, s.wy, s.wz - extraSink, dt);
    float gh;
    if (!w.cache().height(g.x, g.y, &gh) || g.z < gh + 20.0f) return -1;
  }
  return -1;
}

// Reachability at the world's current state (glider at the launch point, time = w.time()).
inline Reach analyse(const World& w, float range = 3600.0f) {
  Reach r;
  static ThermalInfo th[Air::kMaxInfo];
  const Glider& g = w.glider();
  const int n = w.air().nearby(g.x, g.y, g.z, w.time(), range, th, Air::kMaxInfo);
  for (int i = 0; i < n; i++) {
    if (th[i].strength < kUsable) continue;
    const float d = hypotf(th[i].x - g.x, th[i].y - g.y);
    r.working++;
    if (r.nearest < 0 || d < r.nearest) r.nearest = d;
    const float agl = glideTo(w, th[i].id, w.time());
    if (agl >= 30.0f) {
      r.reachable++;
      if (r.nearestReach < 0 || d < r.nearestReach) r.nearestReach = d;
      if (glideTo(w, th[i].id, w.time(), 0.5f) >= 30.0f) r.withMargin++;
    }
  }
  return r;
}

// The scripted pilot: a pilot who knows where the lift is (assist on). He goes to the nearest working thermal he can glide to
// (with a margin for sink), circles in it until the cloud base or the end of its life, and when none is within reach soars
// the slope in front of him (along the contour, a little into the hill) to get height first.
struct Autopilot {
  uint32_t target = 0, done = 0;       // done: a thermal already used up to the cloud base
  bool have = false, circling = false;
  float orbit = 40.0f;
  int thermalsUsed = 0;
  int soarSide = 1;
  float lastLookT = -100.0f, lowSince = 0;
  int soarSteps = 0;

  Controls soar(const World& w) {
    const Glider& g = w.glider();
    float gx = 0, gy = 0;
    Controls c;
    if (!w.cache().gradient(g.x, g.y, 300.0f, &gx, &gy)) return c;
    const float s = sqrtf(gx * gx + gy * gy);
    if (s < 1e-3f) return c;
    if (w.vario() < -0.4f) lowSince += 0.1f; else lowSince = 0;
    if (lowSince > 6.0f) { soarSide = -soarSide; lowSince = 0; }
    // ground track along the contour (perpendicular to the gradient, which points uphill), leaning toward the hill when high
    // and away from it when low; the heading is the crab angle that makes that track in the wind.
    const float uphill = atan2f(gx, gy);
    const float lean = clampf((w.agl() - 90.0f) / 250.0f, -0.6f, 0.35f);
    const float trk = uphill + soarSide * (0.5f * kPi - lean);
    float wx, wy;
    w.windAt(g.z, &wx, &wy);
    const float tx = sinf(trk), ty = cosf(trk);
    const float wt = wx * tx + wy * ty, V = g.airspeed;
    const float disc = wt * wt - (wx * wx + wy * wy) + V * V;
    const float kg = disc > 0 ? wt + sqrtf(disc) : V;
    c.bank = steerTo(g.heading, atan2f(kg * tx - wx, kg * ty - wy));
    soarSteps++;
    return c;
  }

  Controls step(const World& w) {
    const Glider& g = w.glider();
    static ThermalInfo th[Air::kMaxInfo];
    const int n = w.air().nearby(g.x, g.y, g.z, w.time(), 3600.0f, th, Air::kMaxInfo);
    const bool nearCloud = g.z > w.air().cloudBase() - 100.0f;
    int k = -1;
    if (have) for (int j = 0; j < n; j++) if (th[j].id == target && th[j].strength > 0.7f) k = j;
    if (k >= 0 && nearCloud && circling) { done = target; k = -1; }
    if (k < 0 && have) have = circling = false;
    if (k < 0 && w.time() - lastLookT >= 2.0f) {   // pick the nearest usable one that can be reached
      lastLookT = w.time();
      float bestD = 1e9f;
      for (int j = 0; j < n; j++) {
        if (th[j].strength < kUsable || (th[j].id == done && nearCloud)) continue;
        const float d = hypotf(th[j].x - g.x, th[j].y - g.y);
        if (d >= bestD) continue;
        if (glideTo(w, th[j].id, w.time(), 0.3f) < 40.0f) continue;
        bestD = d; k = j;
      }
      if (k >= 0) { have = true; target = th[k].id; thermalsUsed++; }
    }
    if (k < 0) return soar(w);
    const float dx = th[k].x - g.x, dy = th[k].y - g.y;
    const float d = hypotf(dx, dy);
    orbit = clampf(0.3f * th[k].radius, 20.0f, 50.0f);
    if (d < 1.1f * th[k].radius) circling = true;
    Controls c;
    if (!circling) { c.bank = steerTo(g.heading, atan2f(dx, dy)); return c; }
    // Clockwise orbit around the centre: tangent heading = bearing(centre -> glider) + 90 deg, pulled inward when outside.
    const float theta = atan2f(-dx, -dy);
    const float want = theta + 0.5f * kPi + clampf((d - orbit) / orbit, -1.0f, 1.0f) * 1.0f;
    c.bank = steerTo(g.heading, want);
    return c;
  }
};

struct Flight { float gain = 0, endAlt = 0; bool landed = false; int thermals = 0; float seconds = 0; };

// Fly the auto-pilot for `seconds` of sim time; the gain is the best altitude reached above the start.
inline Flight fly(World& w, float seconds) {
  Autopilot ap;
  Flight f;
  const float t0 = w.time(), z0 = w.glider().z;
  const float dt = 0.1f;
  while (w.status() == Status::FLYING && w.time() - t0 < seconds) w.step(ap.step(w), dt);
  f.gain = w.maxAltitude() - z0;
  f.endAlt = w.glider().z;
  f.landed = w.status() != Status::FLYING;
  f.thermals = ap.thermalsUsed;
  f.seconds = w.time() - t0;
  return f;
}

}  // namespace simreach
