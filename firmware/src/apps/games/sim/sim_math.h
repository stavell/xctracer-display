#pragma once
// Small float helpers of the flight simulator (pure, header only). Units are SI everywhere in the sim core:
// metres, seconds, radians, m/s; km/h and degrees appear only at the edges (tables, HUD).
#include <math.h>
#include <stdint.h>

namespace sim {

constexpr float kPi = 3.14159265f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
constexpr float kKmh2Ms = 1.0f / 3.6f;
constexpr float kG = 9.81f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
// First-order lag: the factor by which a value moves toward its target in dt with time constant tau.
inline float lagFactor(float dt, float tau) { return tau <= 0 ? 1.0f : 1.0f - expf(-dt / tau); }
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

}  // namespace sim
