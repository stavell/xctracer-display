#include "platform/hw/thermo_sht.h"
#include <Arduino.h>
#include <math.h>
#include <Adafruit_SHT31.h>

static Adafruit_SHT31 sht;
static const uint32_t PERIOD_MS = 2000;

bool ThermoSht::begin() {
  have_ = sht.begin(0x44);
  last_ = millis();
  return have_;
}

// A single-shot measurement blocks for about 15 ms; every 2 s that is cheap enough for the loop.
bool ThermoSht::read(float& tempC, float& rhPct) {
  if (!have_) return false;
  uint32_t now = millis();
  if (now - last_ < PERIOD_MS) return false;
  last_ = now;
  float t = NAN, rh = NAN;
  if (!sht.readBoth(&t, &rh)) return false;       // one measurement gives both values
  if (!isfinite(t) || !isfinite(rh)) return false;
  tempC = t;
  rhPct = rh;
  return true;
}
