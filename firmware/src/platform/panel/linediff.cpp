#include "platform/util/hot.h"
HOT_FILE_O3
#include "platform/util/fastmem.h"
#include "platform/panel/linediff.h"
#include <string.h>

int changedLines(const uint8_t* cur, const uint8_t* snap, uint8_t* out) {
  int n = 0;
  for (int y = 0; y < LD_LINES; y++) {
    size_t o = (size_t)y * LD_BYTES_PER_LINE;
    if (!fastEqual(cur + o, snap + o, LD_BYTES_PER_LINE)) out[n++] = (uint8_t)y;
  }
  return n;
}
