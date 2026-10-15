#include "core/linediff.h"
#include <string.h>

int changedLines(const uint8_t* cur, const uint8_t* snap, uint8_t* out) {
  int n = 0;
  for (int y = 0; y < LD_LINES; y++) {
    size_t o = (size_t)y * LD_BYTES_PER_LINE;
    if (memcmp(cur + o, snap + o, LD_BYTES_PER_LINE) != 0) out[n++] = (uint8_t)y;
  }
  return n;
}
