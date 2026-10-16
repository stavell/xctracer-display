#pragma once
// Work counting for the draw path: a stand-in for device time, which the PC cannot measure.
// CountingSharp is the device canvas (FastSharp) with every virtual Adafruit_GFX entry counted; with
// -DGFX_COUNT the Bitmap1 / gray primitives count themselves (gfx/count.h). The draw profiler's clock
// is pointed at one counter at a time, so every profiler section reports its own work.
#include "gfx/count.h"
#include "hw/fast_sharp.h"
#include "view/prof.h"

static uint32_t gCanvasPx = 0, gCanvasSpan = 0;

class CountingSharp : public FastSharp {
 public:
  CountingSharp() : FastSharp(nullptr, 0, 400, 240, 0) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override { gCanvasPx++; FastSharp::drawPixel(x, y, c); }
  void writePixel(int16_t x, int16_t y, uint16_t c) override { gCanvasPx++; FastSharp::writePixel(x, y, c); }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { gCanvasSpan++; FastSharp::writeFastHLine(x, y, w, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { gCanvasSpan++; FastSharp::drawFastHLine(x, y, w, c); }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { gCanvasSpan++; FastSharp::writeFastVLine(x, y, h, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { gCanvasSpan++; FastSharp::drawFastVLine(x, y, h, c); }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { gCanvasSpan++; FastSharp::writeFillRect(x, y, w, h, c); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { gCanvasSpan++; FastSharp::fillRect(x, y, w, h, c); }
};

enum CountMetric { CM_CANVAS_PX, CM_CANVAS_SPAN, CM_BM_PIXEL, CM_BM_SPAN, CM_BM_ROWS, CM_BAYER, CM_N };
static const char* const kMetricName[CM_N] = {"gfxPx", "gfxSpan", "bmPx", "bmSpan", "rows", "bayer"};
static uint32_t cm0() { return gCanvasPx; }
static uint32_t cm1() { return gCanvasSpan; }
#ifdef GFX_COUNT
static uint32_t cm2() { return gGfxCnt.pixel; }
static uint32_t cm3() { return gGfxCnt.span; }
static uint32_t cm4() { return gGfxCnt.rows; }
static uint32_t cm5() { return gGfxCnt.bayer; }
#else
static uint32_t cm2() { return 0; }
static uint32_t cm3() { return 0; }
static uint32_t cm4() { return 0; }
static uint32_t cm5() { return 0; }
#endif
static uint32_t (*const kMetricClock[CM_N])() = {cm0, cm1, cm2, cm3, cm4, cm5};

// Draw once per metric with the profiler clock on that counter and print one table (sections x metrics).
template <class DrawFn>
static void countReport(const char* name, CountingSharp& disp, DrawFn draw) {
  static uint32_t v[CM_N][P_COUNT];
  static bool seen[P_COUNT];
  draw();   // warm-up: builds the glyph sheets, which is not per-frame work
  for (int m = 0; m < CM_N; m++) {
    profSetClock(kMetricClock[m]);
    profReset();
    disp.clearDisplay();
    draw();
    for (int i = 0; i < P_COUNT; i++) { v[m][i] = gProf[i].count ? gProf[i].last : 0; seen[i] = gProf[i].count != 0; }
  }
  profSetClock(nullptr);
  printf("== %s\n  %-9s", name, "section");
  for (int m = 0; m < CM_N; m++) printf(" %9s", kMetricName[m]);
  printf("\n");
  for (int i = 0; i < P_COUNT; i++) {
    if (!seen[i]) continue;
    printf("  %-9s", gProf[i].name);
    for (int m = 0; m < CM_N; m++) printf(" %9u", (unsigned)v[m][i]);
    printf("\n");
  }
}
