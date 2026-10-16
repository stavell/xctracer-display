#pragma once
// Work counters for the emulator (compiled in with -DGFX_COUNT, absent from the device build): they
// stand in for device time, which the PC cannot measure. The device pays roughly 1.1 us per virtual
// Adafruit_GFX pixel call and a few hundred cycles per dither pattern evaluation.
#include <stdint.h>

#ifdef GFX_COUNT
struct GfxCounters {
  uint32_t pixel;    // Bitmap1::pixel calls (single pixels written straight into the buffer)
  uint32_t span;     // Bitmap1 / gray rect and span calls
  uint32_t rows;     // native rows read-modified-written by those calls (a rotated hline is one per pixel)
  uint32_t bayer;    // dither threshold evaluations (bayer8 calls)
};
extern GfxCounters gGfxCnt;
#define GFX_TALLY(field, n) (gGfxCnt.field += (uint32_t)(n))
#else
#define GFX_TALLY(field, n) ((void)0)
#endif
