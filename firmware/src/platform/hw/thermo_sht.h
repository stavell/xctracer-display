#pragma once
#include "platform/ports.h"

// SHT30 (I2C 0x44) air temperature on the Feather Sense. One reading every 2 s, always on.
class ThermoSht : public IThermo {
 public:
  bool begin();                                     // after Wire is started; false if the sensor is missing
  bool read(float& tempC, float& rhPct) override;
  bool readNow(float& tempC, float& rhPct) override;
  bool present() const { return have_; }

 private:
  bool have_ = false;
  uint32_t last_ = 0;
};
