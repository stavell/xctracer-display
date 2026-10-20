// Wiring only: builds the hardware adapters and hands them to the App.
#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include "apps/vario/app/app.h"
#include "apps/vario/app/terrain_kit.h"
#include "apps/vario/app/vario_app.h"
#include "apps/vario/core/flight_log.h"
#include "platform/hw/cyccnt.h"
#include "platform/hw/battery_adc.h"
#include "platform/hw/compass_lis.h"
#include "apps/vario/hw/config_store.h"
#include "apps/vario/hw/diagnostics.h"
#include "platform/hw/input_button.h"
#include "platform/hw/key_store.h"
#include "platform/hw/pack_service.h"
#include "platform/settings/box_settings.h"
#include "apps/vario/hw/laptop_ble.h"
#include "platform/hw/panel_sharp.h"
#include "platform/hw/power_nrf.h"
#include "platform/hw/thermo_sht.h"
#include "apps/vario/hw/tracer_ble.h"
#include "apps/vario/hw/vario_radios.h"
#include "platform/hw/watchdog.h"
#include "platform/hw/pins.h"
#include "apps/vario/hw/protocol.h"

static ConfigStore config;
static FlashKeyStore keys;                    // time zone, mounting, compass calibration (shared with the box image)
static BoxSettings boxSettings(keys);
static SharpPanel panel;
static ButtonGpio button(PIN_BUTTON1);
static BatteryAdc battery;
static CompassLis compassHw;
static ThermoSht thermo;
static TracerBle tracer(config);
static LaptopBle laptop(panel);
static VarioRadios radios(laptop, tracer);
static PowerNrf power(panel, radios, PIN_BUTTON1);

static AppConfig appConfig() {
  AppConfig c;
  c.rotation = DISPLAY_ROTATION;
  return c;
}
static App app(panel, battery, tracer, laptop, boxSettings, compassHw, thermo, power, appConfig());
static VarioApp varioApp(app);
static PackService pack;                      // map pack in the QSPI flash + the USB serial loader (host/load_pack.py)
static flightlog::FlightLogger flightLog(pack.logSink());   // validation recorder: last 256 kB of the QSPI flash (README "Flight log")
static TerrainKit kit;                       // ~65 kB; no pack: the vario shows no terrain, AGL or map page
static VarioProbe probe;
static AppHost host(panel, button);

void setup() {
  PowerNrf::gateOnWake(PIN_BUTTON1, 1000);   // before anything else: a quick tap goes back to sleep
  // Watchdog disabled: a running WDT survives the jump into the bootloader and reset it in the middle of
  // a serial DFU upload (seen 2026-10-15). Re-enable only together with a fix for uploads (see NOTES.md).
  // watchdog::start(8000);
  Serial.begin(115200);
  button.begin();
  TerrainMap::setClock(fastMicros);           // the map picture job spends a time budget per loop pass
  config.begin();                             // mounts the filesystem
  keys.begin();
  config.migrateTo(boxSettings);              // first start after the update: calibration, mounting, time zone move over
  panel.begin();
  compassHw.begin();                          // starts Wire
  thermo.begin();                             // SHT30 on the same bus
  // Every library begin() resets the bus to 100 kHz; at 80 Hz sensor reads that is ~22% of the loop.
  // All I2C chips on the board (LIS3MDL, LSM6DS33, SHT30) support 400 kHz.
  Wire.setClock(400000);

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);                      // one link to the laptop, one to the vario
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);
  tracer.begin();
  laptop.begin();

  pack.begin();
  app.setLog(&flightLog);
  if (pack.reader() && kit.open(pack.reader())) {
    app.setTerrain(&kit);
    pack.setTerrainOpen(true);
  }

  diagnosticsBegin(&app, panel, compassHw);
  diagnosticsSetPack(&pack);
  diagnosticsBench();                         // DIAGNOSTICS only
  host.add(varioApp);
  host.setProbe(&probe);
  host.setRotation(DISPLAY_ROTATION);
  host.start(0, millis());
}

static uint32_t worstTickUs = 0;   // slowest app.tick() since the last status line

void loop() {
  watchdog::feed();
  uint32_t now = millis();
  pack.service(now);
  uint32_t t0 = fastMicros();
  host.tick(now);
  uint32_t dt = fastMicros() - t0;   // not micros(): it once gave -67 s here (a 4.2e9 us 'worst tick')
  if (dt > worstTickUs) worstTickUs = dt;
  diagnosticsTick(now, worstTickUs);
  delay(pack.loading() ? 0 : 10);             // a load streams over USB: no sleeping between chunks
}
