#pragma once
// "Power off" with the clock: the box sleeps with only the panel on (it holds its picture at microwatts, VCOM keeps
// toggling) and shows the time. This is the pure logic of that sleep, driven by a small loop on the device
// (platform/hw/power_nrf.cpp) and by the PC box (emu/box.h). No hardware, no time source of its own.
//
// The loop asks step() each time it is up, with the facts of the moment, and does what step() says:
//   SLEEP_DRAW  draw the clock screen now (platform/power/clock_view.h) and ask again
//   SLEEP_WAIT  sleep until wakeAtMs (or until the switch moves), then ask again
//   SLEEP_WAKE  the switch was held long enough: leave the sleep and restart the box's apps
//   SLEEP_OFF   the battery is nearly empty, or the power-off hold went on long enough: go to System OFF
// The power-off is two steps: the 3 s hold starts this sleep; if the switch is KEPT down, a bar fills under the clock
// (SleepStep::bar of kBarSteps) and after kOffHoldMs more the box goes to System OFF. Releasing before that keeps the
// clock; the continued hold never wakes the box.
// A hold shorter than kHoldMs (a pocket press) only redraws. The screen is redrawn once per minute, when the local
// minute changes; with the time unknown ("--:--") only on a tap.
#include <stdint.h>

enum SleepAction : uint8_t { SLEEP_WAIT, SLEEP_DRAW, SLEEP_WAKE, SLEEP_OFF };

struct SleepIn {
  uint32_t nowMs = 0;
  bool down = false;            // the user switch is pressed
  int battPct = -1;             // 0..100, < 0 unknown
  int localMinute = -1;         // minutes of the local day, -1 = unknown
  long msToMinute = -1;         // until the local minute changes, -1 = unknown
};

struct SleepStep {
  SleepAction act = SLEEP_WAIT;
  uint32_t wakeAtMs = 0;        // SLEEP_WAIT: absolute time of the next step()
  int bar = -1;                 // SLEEP_DRAW: countdown bar fill 0..kBarSteps-1, -1 = no bar
};

class ClockSleep {
 public:
  static const uint32_t kOffHoldMs = 3000;     // holding on after the sleep began: System OFF
  static const int kBarSteps = 12;             // the bar redraws this often during the countdown
  static const uint32_t kHoldMs = 1000;        // same anti-pocket hold as the wake from System OFF (PowerNrf::gateOnWake)
  static const uint32_t kPollMs = 20;          // while the switch is down
  static const int kLowPct = 2;                // at or below this on two readings in a row: System OFF
  static const uint32_t kMaxSleepMs = 60000;   // the longest wait; the battery is looked at that often (and on a tap)

  void begin() { *this = ClockSleep(); }

  SleepStep step(const SleepIn& in) {
    SleepStep r;
    if (in.down && !armed_) {                                                 // still the hold that powered off
      if (!carry_) { carry_ = true; startAt_ = in.nowMs; }
      const uint32_t el = in.nowMs - startAt_;
      if (el >= kOffHoldMs) { r.act = SLEEP_OFF; return r; }
      const int bar = (int)(el * kBarSteps / kOffHoldMs);
      if (first_ || bar != bar_) {
        first_ = false; bar_ = bar; minute_ = in.localMinute;
        r.act = SLEEP_DRAW; r.bar = bar;
      }
      r.wakeAtMs = in.nowMs + kPollMs;
      return r;
    }
    if (!armed_) { armed_ = true; if (carry_) redraw_ = true; }                // released early: the clock without the bar
    if (in.down) {
      if (!down_) { down_ = true; pressAt_ = in.nowMs; }
      if (in.nowMs - pressAt_ >= kHoldMs) { r.act = SLEEP_WAKE; return r; }
      r.wakeAtMs = in.nowMs + kPollMs;
      return r;
    }
    if (down_) { down_ = false; redraw_ = true; checkBatt_ = true; }          // a tap: look again
    if (!checkBatt_ && in.nowMs - battAt_ >= kMaxSleepMs) checkBatt_ = true;
    if (checkBatt_ && in.battPct >= 0) {
      checkBatt_ = false;
      battAt_ = in.nowMs;
      if (in.battPct > kLowPct) low_ = 0;
      else if (++low_ >= 2) { r.act = SLEEP_OFF; return r; }
      else { checkBatt_ = true; r.wakeAtMs = in.nowMs + 2000; }               // confirm a low reading soon
    }
    if (first_ || redraw_ || in.localMinute != minute_) {
      first_ = false;
      redraw_ = false;
      minute_ = in.localMinute;
      r.act = SLEEP_DRAW;
      return r;
    }
    if (r.wakeAtMs) return r;                                                 // the 2 s battery re-check
    // Sleep until the minute changes; with an unknown time (or a clock that just went stale) look once a minute.
    uint32_t d = in.localMinute >= 0 && in.msToMinute > 0 ? (uint32_t)in.msToMinute + 50 : kMaxSleepMs;
    if (d > kMaxSleepMs) d = kMaxSleepMs;
    r.wakeAtMs = in.nowMs + d;
    return r;
  }

 private:
  bool armed_ = false, carry_ = false, first_ = true, redraw_ = false, down_ = false, checkBatt_ = true;
  int minute_ = -2, bar_ = -1;
  int low_ = 0;
  uint32_t startAt_ = 0, pressAt_ = 0, battAt_ = 0;
};
