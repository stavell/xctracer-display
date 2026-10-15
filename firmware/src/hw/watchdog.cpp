#include "hw/watchdog.h"
#include <Arduino.h>

namespace watchdog {

void start(uint32_t timeoutMs) {
  if (NRF_WDT->RUNSTATUS) return;                       // already running (survives soft resets)
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) | (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = (uint32_t)((uint64_t)timeoutMs * 32768u / 1000u);
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}

void feed() {
  if (NRF_WDT->RUNSTATUS) NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

bool causedLastReset() { return (readResetReason() & POWER_RESETREAS_DOG_Msk) != 0; }

}  // namespace watchdog
