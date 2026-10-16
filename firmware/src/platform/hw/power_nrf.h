#pragma once
#include <Arduino.h>
#include "platform/ports.h"
#include "platform/hw/panel_sharp.h"

// What an app has to quiet before the board powers down (BLE links, scans, advertising).
class IRadios {
 public:
  virtual ~IRadios() {}
  virtual void disconnect() = 0;   // drop the links
  virtual void stop() = 0;         // stop advertising and scanning, after the links had time to close
};

// System OFF: blank the panel, stop radios and VCOM, wake on the user switch (a wake is a reset).
class PowerNrf : public IPower {
 public:
  PowerNrf(SharpPanel& panel, IRadios& radios, uint8_t buttonPin)
      : panel_(panel), radios_(radios), pin_(buttonPin) {}
  void shutdown() override;

  // At boot: if we woke from System OFF, continue only when the switch is held long enough,
  // otherwise go straight back to sleep.
  static void gateOnWake(uint8_t buttonPin, uint32_t holdMs);

 private:
  static void sleepNow(uint8_t buttonPin);
  SharpPanel& panel_;
  IRadios& radios_;
  uint8_t pin_;
};
