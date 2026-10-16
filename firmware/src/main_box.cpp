// The box image: launcher + vario + Tetris + compass & weather tool on one platform. Wiring only.
// Boot: straight into the start-up app (default: the vario; the launcher's "Start-up" row changes it).
// The launcher opens only on a deliberate gesture from inside an app (see README, "Box").
#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include "apps/games/tetris/tetris_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/vario/app/app.h"
#include "apps/vario/app/null_ports.h"
#include "apps/vario/app/vario_app.h"
#include "platform/hw/battery_adc.h"
#include "platform/hw/compass_lis.h"
#include "apps/vario/hw/config_store.h"
#include "apps/vario/hw/diagnostics.h"
#include "platform/hw/input_button.h"
#include "platform/hw/key_store.h"
#include "apps/vario/hw/laptop_ble.h"
#include "platform/hw/panel_sharp.h"
#include "platform/hw/power_nrf.h"
#include "platform/hw/thermo_sht.h"
#include "apps/vario/hw/tracer_ble.h"
#include "apps/vario/hw/vario_radios.h"
#include "platform/hw/watchdog.h"
#include "platform/hw/pins.h"
#include "platform/version.h"
#include "apps/vario/hw/protocol.h"

static ConfigStore config;
static FlashKeyStore keys;
static SharpPanel panel;
static ButtonGpio button(PIN_BUTTON1);
static BatteryAdc battery;
static CompassLis compassHw;
static ThermoSht thermo;
static TracerBle tracer(config);
static LaptopBle laptop(panel);
static VarioRadios radios(laptop, tracer);
static PowerNrf power(panel, radios, PIN_BUTTON1);

static AppConfig appConfig(bool tool) {
  AppConfig c;
  c.rotation = DISPLAY_ROTATION;
  c.standalone = tool;
  return c;
}
// App indices in the host; the launcher lists the first three.
enum { APP_VARIO, APP_TETRIS, APP_COMPASS, APP_LAUNCHER };

static App app(panel, battery, tracer, laptop, config, compassHw, thermo, power, appConfig(false));
static VarioApp varioApp(app);
// The compass & weather tool: the same App without a tracer or laptop, always on the compass page.
static NullTracer noTracer;
static NullLaptop noLaptop;
static App toolApp(panel, battery, noTracer, noLaptop, config, compassHw, thermo, power, appConfig(true));
static VarioApp compassApp(toolApp, "compass", false);

struct TetrisScores : IScoreStore {
  uint32_t load() override { return keys.get(SLOT_TETRIS_BEST); }
  void save(uint32_t score) override { keys.set(SLOT_TETRIS_BEST, score); }
};
static TetrisScores scores;
static TetrisApp tetrisApp;
static LauncherApp launcher(battery, power, keys, FW_VERSION);

static VarioProbe probe;
static AppHost host(panel, button);

void setup() {
  PowerNrf::gateOnWake(PIN_BUTTON1, 1000);   // before anything else: a quick tap goes back to sleep
  Serial.begin(115200);
  button.begin();
  config.begin();                             // mounts the filesystem
  keys.begin();
  panel.begin();
  compassHw.begin();                          // starts Wire
  thermo.begin();                             // SHT30 on the same bus
  Wire.setClock(400000);

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);                      // one link to the laptop, one to the vario
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);
  tracer.begin();
  laptop.begin();

  diagnosticsBegin(app, panel, compassHw);
  diagnosticsBench();                         // DIAGNOSTICS only

  tetrisApp.setScoreStore(&scores);
  host.add(varioApp);
  host.add(tetrisApp);
  host.add(compassApp);
  host.add(launcher);
  host.setLauncher(APP_LAUNCHER);
  host.setProbe(&probe);
  host.setRotation(DISPLAY_ROTATION);
  launcher.addEntry("Vario", "flight display", APP_VARIO, ICON_VARIO);
  launcher.addEntry("Tetris", "falling blocks", APP_TETRIS, ICON_TETRIS);
  launcher.addEntry("Compass", "compass, weather", APP_COMPASS, ICON_COMPASS);
  const int boot = launcher.startupApp();
  if (boot != APP_VARIO) tracer.pause();      // the vario is not the first app: do not scan until it is opened
  host.start(boot, millis());
}

static uint32_t worstTickUs = 0;   // slowest host.tick() since the last status line

void loop() {
  watchdog::feed();
  uint32_t now = millis();
  uint32_t t0 = micros();
  host.tick(now);
  uint32_t dt = micros() - t0;
  if (dt > worstTickUs) worstTickUs = dt;
  diagnosticsTick(now, worstTickUs);
  delay(10);
}
