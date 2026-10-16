#pragma once
#include "platform/ports.h"

// LiPo voltage from the Feather's VBAT divider.
class BatteryAdc : public IBattery {
 public:
  void update(uint32_t nowMs) override;
  int percent() override { return pct_; }
 private:
  uint32_t last_ = 0;
  int pct_ = 0;
};
