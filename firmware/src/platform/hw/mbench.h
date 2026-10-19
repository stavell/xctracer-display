#pragma once
// Device micro-benchmark (SIM_BENCH builds only): what do plain loops cost on this chip? run() fills mbench::line with an
// "mb ..." status line that the loop prints, so the numbers can be read from the serial port. Cycles from the DWT counter.
#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "platform/hw/cyccnt.h"

namespace mbench {
static char line[700];
static uint32_t res[24];
static const char* nm[24];
static int n2;
static uint8_t buf[12000] __attribute__((aligned(4)));
static uint32_t cycPer(uint32_t c0, uint32_t n) { return (cycNow() - c0) * 10 / n; }   // tenths of a cycle per item
#define MB_FMT(v) (unsigned long)((v) / 10), (unsigned long)((v) % 10)

static void run() {
  cycEnable();
  uint32_t a, b, c, d;
  { uint32_t c0 = cycNow(); for (int i = 0; i < 20; i++) memset(buf, i, sizeof buf); a = cycPer(c0, 20 * sizeof buf); }
  { uint32_t c0 = cycNow(); for (int k = 0; k < 20; k++) { volatile uint32_t* p = (volatile uint32_t*)buf; for (int i = 0; i < 3000; i++) p[i] = 0; } b = cycPer(c0, 20 * 3000); }
  { uint32_t c0 = cycNow(); for (int k = 0; k < 20; k++) memcpy(buf, buf + 6000, 6000); c = cycPer(c0, 20 * 6000); }
  { volatile float x = 1; float y = x; uint32_t c0 = cycNow(); for (int i = 0; i < 4000; i++) y = y * 1.0001f + 0.5f; d = cycPer(c0, 4000); x = y; }
  n2 = 0;
#define MB(name, expr) { volatile float x = 0.7f; float y = x; uint32_t c0 = cycNow(); for (int i = 0; i < 200; i++) { y = (expr); y = y * 0.5f + 0.3f; } res[n2] = cycPer(c0, 200); nm[n2++] = name; x = y; }
  MB("empty", y)
  MB("sqrtf", sqrtf(y))
  MB("sinf", sinf(y))
  MB("cosf", cosf(y))
  MB("tanf", tanf(y))
  MB("atan2f", atan2f(y, 0.6f))
  MB("expf", expf(-y))
  MB("powf", powf(y, 1.2f))
  MB("logf", logf(y + 1))
  MB("floorf", floorf(y * 10))
  MB("lroundf", (float)lroundf(y * 10))
  MB("roundf", roundf(y * 10))
  MB("ceilf", ceilf(y * 10))
  MB("fmaxf", fmaxf(y, 0.4f))
  MB("fabsf", fabsf(y - 0.5f))
  MB("flt2int", (float)(int)(y * 10))
  MB("fmodf", fmodf(y * 10, 3.0f))
  // wall check: busy-wait 100 ms by millis(), count cycles (expect 6.4 M)
  uint32_t m0 = millis();
  while (millis() == m0) {}
  m0 = millis();
  uint32_t c0 = cycNow();
  while (millis() - m0 < 100) {}
  uint32_t cyc100 = cycNow() - c0;
  int n = snprintf(line, sizeof line, "mb memset=%lu.%lu cyc/B wordloop=%lu.%lu cyc/w memcpy=%lu.%lu cyc/B fmadd=%lu.%lu cyc100ms=%lu icache=%lu", MB_FMT(a), MB_FMT(b), MB_FMT(c), MB_FMT(d),
                   (unsigned long)cyc100, (unsigned long)NRF_NVMC->ICACHECNF);
  for (int i = 0; i < n2 && n < (int)sizeof line - 24; i++) n += snprintf(line + n, sizeof line - n, " %s=%lu", nm[i], (unsigned long)(res[i] / 10));
}
}  // namespace mbench
