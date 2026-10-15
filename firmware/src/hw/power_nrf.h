#pragma once
#include <Arduino.h>
#include "app/ports.h"
#include "hw/laptop_ble.h"
#include "hw/panel_sharp.h"
#include "hw/tracer_ble.h"

// System OFF: blank the panel, stop radios and VCOM, wake on the user switch (a wake is a reset).
class PowerNrf : public IPower {
 public:
  PowerNrf(SharpPanel& panel, LaptopBle& laptop, TracerBle& tracer, uint8_t buttonPin)
      : panel_(panel), laptop_(laptop), tracer_(tracer), pin_(buttonPin) {}
  void shutdown() override;

  // At boot: if we woke from System OFF, continue only when the switch is held long enough,
  // otherwise go straight back to sleep.
  static void gateOnWake(uint8_t buttonPin, uint32_t holdMs);

 private:
  static void sleepNow(uint8_t buttonPin);
  SharpPanel& panel_;
  LaptopBle& laptop_;
  TracerBle& tracer_;
  uint8_t pin_;
};
