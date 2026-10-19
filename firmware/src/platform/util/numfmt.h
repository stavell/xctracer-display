#pragma once
// Float to fixed-point text without snprintf("%.Nf"): newlib's float printf costs thousands of cycles per number
// (soft double inside). Gives exactly the string snprintf gives for "%.Nf" / "%+.Nf" (N = 0..3): the float is
// rounded as its exact binary value, ties to even ("%.0f" of 2.5 is "2", of 0.5 is "0"), a negative value that
// rounds to zero keeps its sign ("-0", "-0.0"), and so does -0.0. Non-finite values and magnitudes of 1e9 and
// more are not handled here (the caller prints "--" or uses snprintf): fmtFixed() returns false for them.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline bool fmtFixed(char* out, size_t cap, float v, int decimals, bool plus) {
  if (cap == 0) return true;
  if (!isfinite(v) || fabsf(v) >= 1e9f || decimals < 0 || decimals > 3) return false;
  uint32_t bits;
  memcpy(&bits, &v, 4);
  const bool neg = (bits >> 31) != 0;
  const int be = (int)((bits >> 23) & 0xFF);
  uint64_t mant = bits & 0x7FFFFF;
  int e;                                           // value = mant * 2^e
  if (be == 0) e = -149;
  else { mant |= 0x800000; e = be - 150; }
  static const uint32_t p10[4] = {1, 10, 100, 1000};
  const uint32_t scale = p10[decimals];
  uint64_t q;                                      // round(|v| * 10^decimals), ties to even
  if (e >= 0) {
    q = (mant << e) * scale;                       // |v| < 1e9 here, so mant << e < 2^30
  } else {
    const uint64_t scaled = mant * scale;          // < 2^24 * 1000 < 2^34
    const int sh = -e;
    if (sh >= 63) {
      q = 0;                                       // scaled < 2^(sh-1): below one half
    } else {
      q = scaled >> sh;
      const uint64_t rem = scaled & ((1ull << sh) - 1), half = 1ull << (sh - 1);
      if (rem > half || (rem == half && (q & 1))) q++;
    }
  }
  char tmp[24];
  int n = 0;
  if (neg) tmp[n++] = '-';
  else if (plus) tmp[n++] = '+';
  char dig[16];
  int nd = 0;
  uint64_t ip = q / scale, fp = q % scale;
  do { dig[nd++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
  while (nd) tmp[n++] = dig[--nd];
  if (decimals) {
    tmp[n++] = '.';
    for (uint32_t d = scale / 10; d; d /= 10) { tmp[n++] = (char)('0' + (fp / d) % 10); }
  }
  tmp[n] = 0;
  size_t i = 0;
  for (; i + 1 < cap && tmp[i]; i++) out[i] = tmp[i];   // truncate like snprintf
  out[i] = 0;
  return true;
}

// Parses "%.Nf", "%+.Nf" (the only forms the views use); false for anything else.
inline bool fmtFixedSpec(const char* fmt, int* decimals, bool* plus) {
  const char* f = fmt;
  if (*f++ != '%') return false;
  *plus = false;
  if (*f == '+') { *plus = true; f++; }
  if (*f++ != '.') return false;
  if (*f < '0' || *f > '3') return false;
  *decimals = *f++ - '0';
  return f[0] == 'f' && f[1] == 0;
}
