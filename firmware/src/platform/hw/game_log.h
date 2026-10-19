#pragma once
// The once-a-second game status line ("gm ...", platform/game_probe.h) on the serial port, only while a terminal is attached.
// Wiring: setProbe(&gameLog) on the host (it forwards clear / present to an optional vario probe), call tick() after every
// host.tick() with its duration. Times come from the cycle counter (fastMicros), which, unlike micros(), is monotonic.
#include "platform/game_probe.h"
#include "platform/hw/panel_sharp.h"

class GameLog : public GameProbe {
 public:
  GameLog(AppHost& host, SharpPanel& panel, ClockUs clock, IFrameProbe* next, char* scratch, size_t scratchCap)
      : GameProbe(clock, next), host_(host), panel_(panel), buf_(scratch), cap_(scratchCap) {}
  void present(bool begin) override {
    GameProbe::present(begin);
    if (!begin) addWait(panel_.takeWaitUs());
  }
  void tick(uint32_t nowMs, uint32_t tickUs);
 private:
  AppHost& host_;
  SharpPanel& panel_;
  char* buf_;                // shared text buffer (the loop stack is small, a private one costs heap)
  size_t cap_;
};
