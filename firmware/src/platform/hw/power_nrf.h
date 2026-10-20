#pragma once
#include <Arduino.h>
#include "platform/ports.h"
#include "platform/hw/panel_sharp.h"
#include "platform/power/clock_sleep.h"
#include "platform/power/weather.h"

class BoxSettings;
class WallClock;
class PackService;

// What an app has to quiet before the board powers down (BLE links, scans, advertising).
class IRadios {
 public:
  virtual ~IRadios() {}
  virtual void disconnect() = 0;   // drop the links
  virtual void stop() = 0;         // stop advertising and scanning, after the links had time to close
  virtual void resume() {}         // after a clock sleep: advertising to the laptop again (the apps restart the rest)
};

// What the clock sleep needs besides the panel and the radios (box image only; all pointers must outlive PowerNrf).
struct ClockSleepDeps {
  BoxSettings* settings;      // power-off mode and time zone
  const WallClock* clock;     // the tracer's GPS time, kept across the sleep
  IBattery* battery;
  ICompass* compass;          // switched off for the sleep
  PackService* pack;          // its QSPI flash goes to deep power-down (not while a map load runs)
  uint8_t rotation;           // panel rotation of the clock screen
  WeatherLog* weather;        // the 24 h pressure log and last temperature / humidity, drawn on the clock screen
  IThermo* thermo;            // single-shot reads, once per redraw
  IBaro* baro;
};

// Power off. System OFF: blank the panel, stop radios and VCOM, wake on the user switch (a wake is a reset).
class PowerNrf : public IPower {
 public:
  PowerNrf(SharpPanel& panel, IRadios& radios, uint8_t buttonPin)
      : panel_(panel), radios_(radios), pin_(buttonPin) {}
  void shutdown() override;
  void powerOff() override;                     // System OFF straight away (also in the box image)

  // Box image: shutdown() sleeps in System ON with the clock on the panel instead of
  // System OFF (platform/power/clock_sleep.h) and RETURNS once the switch has been held; the loop then restarts the
  // start-up app (takeResumed()). Without this call (the vario-only image) shutdown() is always System OFF.
  void enableClockSleep(const ClockSleepDeps& d) { deps_ = d; clockSleep_ = true; }
  bool takeResumed() { const bool r = resumed_; resumed_ = false; return r; }

  // At boot: if we woke from System OFF, continue only when the switch is held long enough,
  // otherwise go straight back to sleep.
  static void gateOnWake(uint8_t buttonPin, uint32_t holdMs);

 private:
  static void sleepNow(uint8_t buttonPin);
  void fullOff();
  void clockSleep();
  ClockSleepDeps deps_ = {};
  bool clockSleep_ = false, resumed_ = false;
  SharpPanel& panel_;
  IRadios& radios_;
  uint8_t pin_;
};
