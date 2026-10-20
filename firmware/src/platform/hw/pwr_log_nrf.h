#pragma once
#include <stdint.h>
// The pwrlog ring in .noinit RAM (survives resets, a watchdog and System OFF with the retained RAM; lost by a USB flash).
namespace pwrlog_hw {
void begin(uint32_t resetReason, uint32_t nowMs);   // register the ring, note BOOT
bool dumpStep();                                    // print `pwr ...` lines, a few per call (terminal attached); true when done
}
