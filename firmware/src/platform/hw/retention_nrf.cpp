#include "platform/hw/retention_nrf.h"
#include <Arduino.h>
#include "nrf_soc.h"
#include "platform/power/retained.h"

namespace retention_hw {

static const void* gPtr = nullptr;
static size_t gLen = 0;

bool clockSurvived() {
  const uint32_t r = readResetReason();
  if (r & POWER_RESETREAS_OFF_Msk) return false;              // woke from System OFF: the time asleep is unknown
  return (r & (POWER_RESETREAS_RESETPIN_Msk | POWER_RESETREAS_DOG_Msk | POWER_RESETREAS_SREQ_Msk | POWER_RESETREAS_LOCKUP_Msk)) != 0;
}

void keepRam(const void* p, size_t n) { gPtr = p; gLen = n; }

void prepareSystemOff() {
  if (!gPtr || !gLen) return;
  retained::RamSection s[4];
  const int n = retained::ramSections((uint32_t)(uintptr_t)gPtr, (uint32_t)gLen, s, 4);
  uint8_t sd = 0;
  sd_softdevice_is_enabled(&sd);
  for (int i = 0; i < n; i++) {
    const uint32_t bit = 1u << (16 + s[i].section);           // S<n>RETENTION
    if (sd) sd_power_ram_power_set(s[i].block, bit);          // the SoftDevice owns POWER while it runs
    else NRF_POWER->RAM[s[i].block].POWERSET = bit;
  }
}

}  // namespace retention_hw
