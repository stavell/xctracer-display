#include "platform/terrain/tile_codec.h"

namespace terrain {

namespace {

constexpr int kEsc = 24;      // unary run that announces an escape: 24 ones, then the value as 16 raw bits

// MSB-first bit reader with a 32-bit window. Bits past the end read as zero (so a truncated stream cannot
// loop); `pad` counts those virtual bytes and overrun() tells whether the stream really needed them.
struct Bits {
  const uint8_t* p;
  const uint8_t* start;
  const uint8_t* end;
  uint32_t acc = 0;           // valid bits left-aligned, the rest zero
  int n = 0;                  // valid bits in acc
  uint32_t pad = 0;

  Bits(const uint8_t* s, const uint8_t* e) : p(s), start(s), end(e) {}
  void refill() {
    while (n <= 24) {
      uint32_t b = 0;
      if (p < end) b = *p++; else pad++;
      acc |= b << (24 - n);
      n += 8;
    }
  }
  void drop(int m) { acc <<= m; n -= m; }               // m in 1..24
  uint32_t take(int k) {                                // k in 0..16
    if (k == 0) return 0;
    refill();
    uint32_t v = acc >> (32 - k);
    drop(k);
    return v;
  }
  // Number of leading ones (stops at kEsc, in which case no terminating zero is consumed).
  template <bool CLZ> uint32_t unary() {
    refill();
    if (CLZ) {
      uint32_t inv = ~acc;
      int c = inv ? __builtin_clz(inv) : 32;
      if (c >= kEsc) { drop(kEsc); return kEsc; }
      drop(c + 1);
      return (uint32_t)c;
    }
    uint32_t q = 0;
    while (q < (uint32_t)kEsc && (acc >> 31)) { drop(1); q++; }
    if (q < (uint32_t)kEsc) drop(1);
    return q;
  }
  bool overrun() const { return (uint64_t)((p - start) + pad) * 8 - (uint32_t)n > (uint64_t)(end - start) * 8; }
};

inline int med(int a, int b, int c) {
  int mx = a > b ? a : b, mn = a < b ? a : b;
  if (c >= mx) return mn;
  if (c <= mn) return mx;
  return a + b - c;
}

inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

template <bool CLZ>
bool decode(const uint8_t* s, uint32_t len, int T, int16_t* out) {
  if (len < 3 || T < 1 || T > kMaxTileEdge) return false;
  const int mode = s[0] >> 4, k = s[0] & 15;
  const int n = T * T;
  if (mode == 1) {
    if (len < 1u + 2u * (uint32_t)n) return false;
    for (int i = 0; i < n; i++) out[i] = (int16_t)le16(s + 1 + 2 * i);
    return true;
  }
  const int16_t base = (int16_t)le16(s + 1);
  if (mode == 0) {
    for (int i = 0; i < n; i++) out[i] = base;
    return true;
  }
  if (mode != 2 && mode != 3) return false;
  Bits br(s + 3, s + len);
  out[0] = base;
  for (int i = 0; i < T; i++) {
    for (int j = (i == 0 ? 1 : 0); j < T; j++) {
      uint32_t q = br.unary<CLZ>();
      uint32_t u = (q == (uint32_t)kEsc) ? br.take(16) : (q << k) | br.take(k);
      int e = (u & 1) ? -(int)((u + 1) >> 1) : (int)(u >> 1);
      int p;
      if (i == 0) p = out[j - 1];
      else if (j == 0) p = out[(i - 1) * T];
      else {
        int a = out[i * T + j - 1];
        p = mode == 2 ? a : med(a, out[(i - 1) * T + j], out[(i - 1) * T + j - 1]);
      }
      out[i * T + j] = (int16_t)(p + e);
    }
  }
  return !br.overrun();
}

}  // namespace

bool decodeTile(const uint8_t* s, uint32_t len, int T, int16_t* out) { return decode<true>(s, len, T, out); }
bool decodeTileSimple(const uint8_t* s, uint32_t len, int T, int16_t* out) { return decode<false>(s, len, T, out); }

}  // namespace terrain
