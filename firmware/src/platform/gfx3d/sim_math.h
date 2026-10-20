#pragma once
// Small float helpers of the flight simulator (pure, header only). Units are SI everywhere in the sim core:
// metres, seconds, radians, m/s; km/h and degrees appear only at the edges (tables, HUD).
#include <math.h>
#include <stdint.h>

namespace gfx3d {

constexpr float kPi = 3.14159265f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
constexpr float kKmh2Ms = 1.0f / 3.6f;
constexpr float kG = 9.81f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
// exp / log / pow in single precision, 1e-7 relative error. Why not expf / logf / powf: on the nRF52840 newlib's take 1300 / 1260 /
// 2540 cycles (measured, 20 - 40 us each; sinf is 80, sqrtf 17), and the air model called them about 60 times per sample.
inline float fexp2(float x) {                       // 2^x
  x = clampf(x, -125.0f, 125.0f);
  const float r = (float)(int)(x + (x < 0 ? -0.5f : 0.5f));        // nearest integer
  const float f = x - r;                                          // [-0.5, 0.5]
  float p = 1.535336188319500e-4f;
  p = p * f + 1.339887440266574e-3f;
  p = p * f + 9.618437357674640e-3f;
  p = p * f + 5.550332471162809e-2f;
  p = p * f + 2.402264791363012e-1f;
  p = p * f + 6.931472028550421e-1f;
  p = p * f + 1.0f;
  union { uint32_t u; float f; } s;
  s.u = (uint32_t)((int)r + 127) << 23;
  return p * s.f;
}
inline float fexp(float x) { return fexp2(x * 1.44269504089f); }
inline float flog2(float x) {                       // x > 0 and finite
  union { uint32_t u; float f; } v;
  v.f = x;
  int e = (int)((v.u >> 23) & 255) - 127;
  v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;                        // mantissa in [1, 2)
  float m = v.f;
  if (m > 1.41421356f) { m *= 0.5f; e++; }
  const float t = m - 1.0f;                                       // ln(1 + t), t in [-0.29, 0.41]
  const float z = t * t;
  float y = 7.0376836292e-2f;
  y = y * t - 1.1514610310e-1f;
  y = y * t + 1.1676998740e-1f;
  y = y * t - 1.2420140846e-1f;
  y = y * t + 1.4249322787e-1f;
  y = y * t - 1.6668057665e-1f;
  y = y * t + 2.0000714765e-1f;
  y = y * t - 2.4999993993e-1f;
  y = y * t + 3.3333331174e-1f;
  y = y * t * z - 0.5f * z + t;
  return (float)e + y * 1.44269504089f;
}
inline float fpow(float x, float p) { return x > 0 ? fexp2(p * flog2(x)) : 0.0f; }   // x >= 0
// First-order lag: the factor by which a value moves toward its target in dt with time constant tau.
inline float lagFactor(float dt, float tau) { return tau <= 0 ? 1.0f : 1.0f - fexp(-dt / tau); }
inline float wrapPi(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}

// Deterministic integer hash (lowbias32 variant); the air model draws all its randomness from it, so a seed
// gives the same day on every machine and every run.
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) { return hash32(a * 0x9E3779B1U ^ hash32(b + 0x85EBCA6BU) ^ hash32(c * 0xC2B2AE35U + 7U)); }
inline float unit(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }   // [0, 1)

}  // namespace gfx3d
