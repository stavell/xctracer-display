#pragma once
#include "platform/ports.h"

// LiPo voltage from the Feather's VBAT divider.
class BatteryAdc : public IBattery {
 public:
  void update(uint32_t nowMs) override;
  int percent() override { return pct_; }
  int millivolts() const { return mv_; }            // the last reading (the status line); 0 before the first
 private:
  uint32_t last_ = 0;
  int pct_ = 0, mv_ = 0;
};
