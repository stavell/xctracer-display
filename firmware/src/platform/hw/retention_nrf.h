#pragma once
// The device side of platform/power/retained.h: where the retained block lives, which restarts keep the time, and which RAM stays
// powered in System OFF. Only the box image uses it (main_box.cpp): the other images never register a range and call nothing.
#include <stddef.h>
#include <stdint.h>

// A variable in this section is not cleared by the start-up code (the linker script has no .noinit: the section is an orphan placed
// after .bss, before the heap, outside the range Reset_Handler zeroes) and keeps its bytes across a reset. Not initialised: no constructor.
#define RETAINED_NOINIT __attribute__((section(".noinit")))

namespace retention_hw {

// True when this boot follows a restart that did not lose the clock's progress (a reset pin, the watchdog, a soft reset, a lockup):
// not a power-on and not the wake from System OFF (POWER->RESETREAS, as read by the core at start-up).
bool clockSurvived();
// Remember [p, p + n): prepareSystemOff() keeps its RAM sections powered. Call once at boot.
void keepRam(const void* p, size_t n);
// Just before System OFF: switch the retention bits on (through the SoftDevice while it runs). A no-op without a registered range.
void prepareSystemOff();

}  // namespace retention_hw
