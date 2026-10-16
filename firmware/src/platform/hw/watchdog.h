#pragma once
#include <stdint.h>

// nRF52 hardware watchdog. A flight instrument must not freeze silently: the memory LCD keeps showing
// the last frame (stale numbers that look live) if the main loop hangs, so a hang resets the board.
// Once started the WDT cannot be stopped, and it survives soft resets; the Adafruit bootloader feeds
// it while waiting for and during a firmware upload.
namespace watchdog {
void start(uint32_t timeoutMs);   // paused while a debugger halts the CPU, keeps running in sleep
void feed();                      // call at least once per timeout, also inside long blocking waits
bool causedLastReset();           // the previous reset came from the watchdog
}
