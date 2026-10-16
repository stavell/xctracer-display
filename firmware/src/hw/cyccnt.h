#pragma once
// DWT cycle counter (64 MHz core clock, one tick = 15.6 ns). micros() on this core only advances in
// ~977 us steps, so timings that need better use this.
#include <Arduino.h>

static const uint32_t CYC_PER_US = 64;

inline void cycEnable() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

inline uint32_t cycNow() { return DWT->CYCCNT; }

// Microseconds from the cycle counter as a 32-bit clock that wraps at 2^32 us like micros(), so
// unsigned subtraction works across the wrap. CYCCNT alone wraps every 67 s, so the cycles since the
// last call are accumulated; call at least once per 67 s. Loop task only (not interrupt safe).
inline uint32_t fastMicros() {
  static uint32_t lastCyc = 0, us = 0, rem = 0;
  uint32_t c = cycNow();
  rem += c - lastCyc;
  lastCyc = c;
  us += rem / CYC_PER_US;
  rem %= CYC_PER_US;
  return us;
}
