#include "platform/hw/pwr_log_nrf.h"
#include <Arduino.h>
#include "platform/hw/retention_nrf.h"
#include "platform/power/pwr_log.h"

static pwrlog::Ring gRing RETAINED_NOINIT;      // after retained::Block, in the same page

namespace pwrlog_hw {

void begin(uint32_t rr, uint32_t nowMs) {
  gRing.init();
  pwrlog::ring() = &gRing;
  pwrlog::note(nowMs, pwrlog::BOOT, (int32_t)rr);
}

// Four lines per call (the USB buffer is small and the loop must not block); true when everything is printed.
bool dumpStep() {
  static uint32_t i = 0xFFFFFFFFu;
  const uint32_t n = gRing.n, from = n > (uint32_t)pwrlog::Ring::kN ? n - pwrlog::Ring::kN : 0;
  if (i == 0xFFFFFFFFu) { Serial.printf("pwr n=%lu\n", (unsigned long)n); i = from; }
  for (int k = 0; k < 4 && i < n; k++, i++) {
    const pwrlog::Event& x = gRing.e[i % pwrlog::Ring::kN];
    Serial.printf("pwr #%lu ms=%lu code=%d a=%ld b=%ld\n", (unsigned long)i, (unsigned long)x.ms, x.code, (long)x.a, (long)x.b);
  }
  return i >= n;
}

}  // namespace pwrlog_hw
