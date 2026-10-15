// Wiring only: builds the hardware adapters and hands them to the App.
#include <Arduino.h>
#include <bluefruit.h>
#include "app/app.h"
#include "hw/battery_adc.h"
#include "hw/config_store.h"
#include "hw/input_button.h"
#include "hw/laptop_ble.h"
#include "hw/panel_sharp.h"
#include "hw/power_nrf.h"
#include "hw/tracer_ble.h"
#include "pins.h"
#include "protocol.h"

static ConfigStore config;
static SharpPanel panel;
static ButtonGpio button(PIN_BUTTON1);
static BatteryAdc battery;
static TracerBle tracer(config);
static LaptopBle laptop(panel);
static PowerNrf power(panel, laptop, tracer, PIN_BUTTON1);

static AppConfig appConfig() {
  AppConfig c;
  c.rotation = DISPLAY_ROTATION;
  return c;
}
static App app(panel, button, battery, tracer, laptop, config, power, appConfig());

void setup() {
  PowerNrf::gateOnWake(PIN_BUTTON1, 1000);   // before anything else: a quick tap goes back to sleep
  Serial.begin(115200);
  button.begin();
  config.begin();
  panel.begin();

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);                      // one link to the laptop, one to the vario
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);
  tracer.begin();
  laptop.begin();

  app.begin(millis());
}

// Status line over USB serial every 2 s, only when a terminal is attached (never blocks).
static uint32_t worstTickUs = 0;   // slowest app.tick() since the last status line

static void debugLine(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 2000) return;
  last = now;
  if (!Serial || Serial.availableForWrite() < 120) return;
  const ViewModel& vm = app.buildViewModel(now);
  Serial.printf("st ok=%lu bad=%lu fresh=%d fix=%d alt=%.0f spd=%.0f vs=%+.1f wind=%d/%.0f sim=%d link=%d pg=%d marks=%d trail=%d tick=%luus\n",
                (unsigned long)app.linesOk(), (unsigned long)app.linesBad(), vm.fresh, vm.s.fix,
                vm.s.alt, vm.s.speedKmh, vm.s.vario, vm.wind.valid, vm.wind.speedKmh, vm.sim, (int)vm.link,
                (int)vm.page, vm.markCount, vm.trailCount, (unsigned long)worstTickUs);
  worstTickUs = 0;
}

void loop() {
  uint32_t now = millis();
  uint32_t t0 = micros();
  app.tick(now);
  uint32_t dt = micros() - t0;
  if (dt > worstTickUs) worstTickUs = dt;
  debugLine(now);
  delay(10);
}
