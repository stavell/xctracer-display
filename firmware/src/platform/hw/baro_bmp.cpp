#include "platform/hw/baro_bmp.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

static bool readRegs(uint8_t addr, uint8_t reg, uint8_t* out, int n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, n) != n) return false;
  for (int i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}
static bool writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool BaroBmp::begin() {
  static const uint8_t addrs[2] = {0x77, 0x76};
  for (uint8_t a : addrs) {
    uint8_t id = 0, cal[24];
    if (readRegs(a, 0xD0, &id, 1) && (id == 0x58 || id == 0x56 || id == 0x57) && readRegs(a, 0x88, cal, 24)) {
      addr_ = a;
      cal_.parse(cal);
      have_ = true;
      writeReg(a, 0xF5, 0x00);                      // no standby, filter off (forced mode needs neither)
      break;
    }
  }
  return have_;
}

// Forced mode: temperature x1, pressure x4, one conversion (about 10 ms), then back to sleep (about 0.1 uA).
bool BaroBmp::readNowT(float& hPa, float& tempC) {
  if (!have_) return false;
  if (!writeReg(addr_, 0xF4, (1 << 5) | (3 << 2) | 1)) return false;
  uint8_t st = 0;
  for (int i = 0; i < 30; i++) {                    // the measuring bit (3) clears when done
    delay(2);
    if (readRegs(addr_, 0xF3, &st, 1) && !(st & 0x08)) break;
  }
  uint8_t d[6];
  if (!readRegs(addr_, 0xF7, d, 6)) return false;
  const int32_t p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
  const int32_t t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
  if (p == 0x80000 || t == 0x80000) return false;   // not measured
  const uint32_t pa = cal_.pressurePa(t, p);
  if (pa == 0) return false;
  hPa = pa / 100.0f;
  tempC = cal_.tempC(t);
  return true;
}
