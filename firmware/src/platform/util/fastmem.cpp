#include "platform/util/hot.h"
HOT_FILE_O3
#include "platform/util/fastmem.h"

// The loops below are what memset / memcpy are, so the compiler must not turn them back into calls to them.
#if defined(__GNUC__) && !defined(__clang__)
#define NO_IDIOM __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define NO_IDIOM
#endif

namespace {
typedef uint32_t __attribute__((may_alias, aligned(1))) U32;     // Cortex-M4 loads and stores words at any address
}

NO_IDIOM void fastFill(void* dst, uint8_t value, size_t n) {
  uint8_t* d = (uint8_t*)dst;
  while (n && ((uintptr_t)d & 3)) { *d++ = value; n--; }
  const uint32_t w = value * 0x01010101u;
  while (n >= 16) {
    ((U32*)d)[0] = w; ((U32*)d)[1] = w; ((U32*)d)[2] = w; ((U32*)d)[3] = w;
    d += 16; n -= 16;
  }
  while (n >= 4) { *(U32*)d = w; d += 4; n -= 4; }
  while (n) { *d++ = value; n--; }
}

NO_IDIOM void fastCopy(void* dst, const void* src, size_t n) {
  uint8_t* d = (uint8_t*)dst;
  const uint8_t* s = (const uint8_t*)src;
  while (n >= 16) {
    const uint32_t a = ((const U32*)s)[0], b = ((const U32*)s)[1], c = ((const U32*)s)[2], e = ((const U32*)s)[3];
    ((U32*)d)[0] = a; ((U32*)d)[1] = b; ((U32*)d)[2] = c; ((U32*)d)[3] = e;
    d += 16; s += 16; n -= 16;
  }
  while (n >= 4) { *(U32*)d = *(const U32*)s; d += 4; s += 4; n -= 4; }
  while (n) { *d++ = *s++; n--; }
}

NO_IDIOM bool fastEqual(const void* a, const void* b, size_t n) {
  const uint8_t* p = (const uint8_t*)a;
  const uint8_t* q = (const uint8_t*)b;
  while (n >= 4) {
    if (*(const U32*)p != *(const U32*)q) return false;
    p += 4; q += 4; n -= 4;
  }
  while (n) { if (*p++ != *q++) return false; n--; }
  return true;
}
