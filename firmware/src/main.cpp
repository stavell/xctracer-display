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
#include "core/wind.h"
#include "core/xctrc.h"
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
// Timing of the numerically heavy pieces on this chip (diagnostics only).
static uint32_t benchParseUs = 0, benchWindUs = 0, benchVmUs = 0;
static void runBench() {
  static TrackPoint pts[30];
  static bool init = false;
  if (!init) {                                           // a 30 s circle in a 12 km/h wind
    for (int i = 0; i < 30; i++) {
      double hdg = (40 + 18 * i) * 0.017453292519943295;
      double gx = 10.0 * sin(hdg) - 3.33, gy = 10.0 * cos(hdg);
      pts[i] = {true, 43.0, 26.0, 1400, 1.5f, (float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) * 57.29577951308232 + 360, 360)};
    }
    init = true;
  }
  uint32_t t0 = micros();
  Sample s;
  bool ok = xctrcParse("$XCTRC,2026,8,16,10,38,24,0,43.252650,26.927967,1387.00,34.13,12.5,-1.30,,,,967.62,85*7c", s);
  uint32_t t1 = micros();
  WindFit f = fitWind(pts, 30);
  uint32_t t2 = micros();
  (void)ok; (void)f;
  benchParseUs = t1 - t0;
  benchWindUs = t2 - t1;
}

static uint32_t worstTickUs = 0;   // slowest app.tick() since the last status line

static void debugLine(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 2000) return;
  last = now;
  if (!Serial || Serial.availableForWrite() < 120) return;
  runBench();
  uint32_t v0 = micros();
  const ViewModel& vm = app.buildViewModel(now);
  benchVmUs = micros() - v0;
  Serial.printf("st ok=%lu bad=%lu fresh=%d fix=%d alt=%.0f spd=%.0f vs=%+.1f wind=%d/%.0f sim=%d link=%d pg=%d marks=%d trail=%d tick=%luus refresh=%luus parse=%luus windfit=%luus viewmodel=%luus\n",
                (unsigned long)app.linesOk(), (unsigned long)app.linesBad(), vm.fresh, vm.s.fix,
                vm.s.alt, vm.s.speedKmh, vm.s.vario, vm.wind.valid, vm.wind.speedKmh, vm.sim, (int)vm.link,
                (int)vm.page, vm.markCount, vm.trailCount, (unsigned long)worstTickUs, (unsigned long)panel.refreshUs(),
                (unsigned long)benchParseUs, (unsigned long)benchWindUs, (unsigned long)benchVmUs);
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
