#pragma once
// A small event ring that explains how the box got where it is: every switch event, page change, app switch, sheet action and power
// entry is noted with its time and two numbers. The device keeps the ring in .noinit RAM (platform/hw/pwr_log_nrf.cpp), so it
// survives a reset or the wake from System OFF and is printed at the next boot as `pwr ...` lines. Pure code: note() is a no-op until a
// ring is registered (the emulator registers one in tests).
#include <stdint.h>

namespace pwrlog {

enum Code : uint8_t {
  BOOT = 1,    // a = reset reason
  SWITCH,      // a = ButtonEvent, b = held ms
  SHEET_OPEN,  // a = start row
  SHEET_ACT,   // a = SystemSheet::Act
  PAGE,        // a = new page, b = 0 switch / 1 laptop
  APP,         // a = app index
  POWER_OFF,   // a = 0 System OFF now, 1 clock sleep requested, 2 System OFF from the sleep, 3 sleep entered
  CMD,         // a bench command (1 APP, 2 REBOOT)
  WAKE,        // a = 1 the clock sleep ended
};

struct Event { uint32_t ms; uint8_t code; int32_t a, b; };

struct Ring {
  static constexpr uint32_t kMagic = 0x5057524Cu;     // "PWRL"
  static constexpr int kN = 48;
  uint32_t magic, n;                                  // n counts all events ever noted; the newest kN are kept
  Event e[kN];
  void init() { if (magic != kMagic) { magic = kMagic; n = 0; } }
  void add(uint32_t ms, uint8_t code, int32_t a, int32_t b) { Event& x = e[n % kN]; x.ms = ms; x.code = code; x.a = a; x.b = b; n++; }
};

inline Ring*& ring() { static Ring* r = nullptr; return r; }
inline uint32_t& clockNow() { static uint32_t t = 0; return t; }       // the last time any caller passed; for call sites without one
inline void note(uint32_t ms, uint8_t code, int32_t a = 0, int32_t b = 0) {
  clockNow() = ms;
  if (ring()) ring()->add(ms, code, a, b);
}

}  // namespace pwrlog
