#pragma once
#include "platform/ports.h"
#include "platform/sensors/bmp280.h"

// BMP280 (I2C 0x77, or 0x76) on the Feather Sense, no library: forced mode, one measurement per call, then it sleeps.
class BaroBmp : public IBaro {
 public:
  bool begin();                                     // after Wire is started; false if the sensor is missing
  bool readNow(float& hPa) override { float t; return readNowT(hPa, t); }
  bool readNowT(float& hPa, float& tempC) override;
  bool present() const { return have_; }

 private:
  bool have_ = false;
  uint8_t addr_ = 0x77;
  Bmp280Cal cal_;
};
