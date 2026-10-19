#include "platform/util/hot.h"
HOT_FILE_O3
#include "platform/util/fastmem.h"
#include "platform/panel/sharp_frame.h"
#include <string.h>

size_t sharpPack(const uint8_t* frame, const uint8_t* idx, int n, uint8_t cmd, uint8_t* out) {
  size_t k = 0;
  out[k++] = cmd;
  for (int i = 0; i < n; i++) {
    out[k++] = (uint8_t)(idx[i] + 1);
    fastCopy(out + k, frame + (size_t)idx[i] * SHARP_LINE_BYTES, SHARP_LINE_BYTES);
    k += SHARP_LINE_BYTES;
    out[k++] = 0x00;
  }
  out[k++] = 0x00;
  return k;
}
