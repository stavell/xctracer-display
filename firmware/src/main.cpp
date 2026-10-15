// Wiring only: builds the hardware adapters and hands them to the App.
#include <Arduino.h>
#include <bluefruit.h>
#include "app/app.h"
#include "hw/battery_adc.h"
#include "hw/compass_lis.h"
#include "hw/config_store.h"
#include "hw/diagnostics.h"
#include "hw/input_button.h"
#include "hw/laptop_ble.h"
#include "hw/panel_sharp.h"
#include "hw/power_nrf.h"
#include "hw/thermo_sht.h"
#include "hw/tracer_ble.h"
#include "hw/watchdog.h"
#include "pins.h"
#include "protocol.h"

static ConfigStore config;
static SharpPanel panel;
static ButtonGpio button(PIN_BUTTON1);
static BatteryAdc battery;
static CompassLis compassHw;
static ThermoSht thermo;
static TracerBle tracer(config);
static LaptopBle laptop(panel);
static PowerNrf power(panel, laptop, tracer, PIN_BUTTON1);

static AppConfig appConfig() {
  AppConfig c;
  c.rotation = DISPLAY_ROTATION;
  return c;
}
static App app(panel, button, battery, tracer, laptop, config, compassHw, thermo, power, appConfig());

void setup() {
  PowerNrf::gateOnWake(PIN_BUTTON1, 1000);   // before anything else: a quick tap goes back to sleep
  // Watchdog disabled: a running WDT survives the jump into the bootloader and reset it in the middle of
  // a serial DFU upload (seen 2026-10-15). Re-enable only together with a fix for uploads (see NOTES.md).
  // watchdog::start(8000);
  Serial.begin(115200);
  button.begin();
  config.begin();
  panel.begin();
  compassHw.begin();                          // starts Wire
  thermo.begin();                             // SHT30 on the same bus

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);                      // one link to the laptop, one to the vario
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);
  tracer.begin();
  laptop.begin();

  diagnosticsBegin(app, panel, compassHw);
  app.begin(millis());
}

static uint32_t worstTickUs = 0;   // slowest app.tick() since the last status line

void loop() {
  watchdog::feed();
  uint32_t now = millis();
  uint32_t t0 = micros();
  app.tick(now);
  uint32_t dt = micros() - t0;
  if (dt > worstTickUs) worstTickUs = dt;
  diagnosticsTick(now, worstTickUs);
  delay(10);
}
