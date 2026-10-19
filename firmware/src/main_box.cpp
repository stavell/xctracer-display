// The box image: home screen (launcher) + vario + Tetris + compass & weather tool + Settings on one platform.
// Wiring only. Boot: straight into the start-up app (default: the vario; Settings changes it).
// The home screen opens only on a deliberate gesture from inside an app (see README, "Box").
#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include "apps/games/sim/sim_box.h"
#include "apps/games/tetris/tetris_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/settings/settings_app.h"
#include "apps/vario/app/app.h"
#include "apps/vario/app/compass_tool.h"
#include "apps/vario/app/terrain_kit.h"
#include "apps/vario/app/vario_app.h"
#include "platform/hw/battery_adc.h"
#include "platform/hw/compass_lis.h"
#include "apps/vario/hw/config_store.h"
#include "apps/vario/hw/diagnostics.h"
#include "platform/hw/input_button.h"
#include "platform/hw/key_store.h"
#include "platform/hw/pack_service.h"
#include "apps/vario/hw/laptop_ble.h"
#include "platform/hw/panel_sharp.h"
#include "platform/hw/power_nrf.h"
#include "platform/hw/thermo_sht.h"
#include "apps/vario/hw/tracer_ble.h"
#include "apps/vario/hw/vario_radios.h"
#include "platform/hw/watchdog.h"
#include "platform/hw/pins.h"
#include "platform/app_registry.h"
#include "platform/lazy_app.h"
#include "platform/clock.h"
#include "platform/settings/box_settings.h"
#include "platform/version.h"
#include "apps/vario/hw/protocol.h"

static ConfigStore config;
static FlashKeyStore keys;
static BoxSettings boxSettings(keys);
static WallClock wallClock;
static AppRegistry registry;
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
// App indices in the host; the home screen lists all but itself.
enum { APP_VARIO, APP_TETRIS, APP_COMPASS, APP_SETTINGS, APP_LAUNCHER, APP_SIM };

static App app(panel, battery, tracer, laptop, boxSettings, compassHw, thermo, power, appConfig(false));
static VarioApp varioApp(app);

// The apps that are not the start-up app share ONE RAM block (see platform/lazy_app.h): each is built when it is opened
// and destroyed when it is left. Statically they cost 93 + 44 kB of .bss and left the image with 2 kB of heap, too little
// for FreeRTOS to start any task (no USB, blank screen). The block is as big as the biggest of them.
static constexpr size_t kArenaBytes = maxSize(sizeof(sim::SimBox), maxSize(sizeof(TerrainKit), maxSize(sizeof(CompassTool), sizeof(TetrisApp))));
static AppArenaStore<kArenaBytes> arena;

// The map pack lives in the QSPI flash (platform/hw/pack_service.h). The vario's terrain kit (65 kB: tile cache, map page
// cache, vector buffers) is built in the arena when the vario is opened and destroyed when it is left, so the other apps
// can use the same RAM. Costs: the map page's cache and ground track start afresh on each return to the vario, and the
// vario comes back on the compass page if it was left on the map page.
// The home screen and Settings > About ask about the pack without the vario running: the answers are copied from the
// kit once at boot (the pack cannot change without a reboot).
static PackService pack;
struct BoxTerrain : ITerrainProvider, terrain::IPackStatus {
  TerrainKit* kit = nullptr;
  bool ok = false;
  uint32_t bytes = 0;
  char area[48] = "";
  char credit[3][200] = {"", "", ""};

  TerrainKit* acquire() override {
    if (kit) return kit;
    if (!pack.reader()) return nullptr;
    void* mem = arena.claim(sizeof(TerrainKit), alignof(TerrainKit), this);
    if (!mem) return nullptr;
    kit = new (mem) TerrainKit();
    if (!kit->open(pack.reader())) { drop(); return nullptr; }
    pack.setTerrainOpen(true);
    return kit;
  }
  void release() override { drop(); }
  void drop() {
    if (!kit) return;
    kit->~TerrainKit();
    kit = nullptr;
    arena.release(this);
  }
  void cacheStatus() {
    TerrainKit* k = acquire();
    if (!k) return;
    ok = true;
    bytes = k->packBytes();
    k->packArea(area, sizeof area);
    for (int i = 0; i < 3; i++) k->packAttribution(i, credit[i], sizeof credit[i]);
    drop();
  }
  bool packOk() const override { return ok; }
  uint32_t packBytes() const override { return bytes; }
  void packArea(char* dst, size_t cap) const override { snprintf(dst, cap, "%s", ok ? area : ""); }
  bool packAttribution(int i, char* dst, size_t cap) const override {
    if (!ok || i < 0 || i > 2 || !cap) return false;
    snprintf(dst, cap, "%s", credit[i]);
    return true;
  }
};
static BoxTerrain boxTerrain;

// The compass & weather tool: the same App without a tracer or laptop, always on the compass page.
static CompassTool* makeCompass(void* mem, void*) {
  return new (mem) CompassTool(panel, battery, boxSettings, compassHw, thermo, power, appConfig(true));
}
static LazyApp<CompassTool> compassApp(arena, "compass", makeCompass);

struct TetrisScores : IScoreStore {
  uint32_t load() override { return keys.get(SLOT_TETRIS_BEST); }
  void save(uint32_t score) override { keys.set(SLOT_TETRIS_BEST, score); }
};
static TetrisScores scores;
static TetrisApp* makeTetris(void* mem, void*) {
  TetrisApp* t = new (mem) TetrisApp();
  t->setScoreStore(&scores);
  return t;
}
static LazyApp<TetrisApp> tetrisApp(arena, "tetris", makeTetris);
// The flight sim opens the pack itself with a 2-tile cache (SimBox); without a pack it says so and goes home.
static sim::SimBox* makeSim(void* mem, void*) { return new (mem) sim::SimBox(pack.reader()); }
static LazyApp<sim::SimBox> simApp(arena, "flightsim", makeSim);
static LauncherApp launcher(battery, power, registry, boxSettings, wallClock);
static SettingsApp settingsApp(battery, compassHw, keys, boxSettings, registry, FW_VERSION);

static VarioProbe probe;
static AppHost host(panel, button);

// Boot, and the way out of the clock sleep: straight into the start-up app (its begin() restarts what it needs).
static void startStartupApp() {
  const int boot = registry.startupApp(keys);
  if (boot != APP_VARIO) tracer.pause();      // the vario is not the first app: do not scan until it is opened
  host.start(boot, millis());
}

void setup() {
  PowerNrf::gateOnWake(PIN_BUTTON1, 1000);   // before anything else: a quick tap goes back to sleep
  Serial.begin(115200);
  button.begin();
  config.begin();                             // mounts the filesystem
  keys.begin();
  config.migrateTo(boxSettings);              // first start after the update: calibration, mounting, time zone move over
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

  pack.begin();
  boxTerrain.cacheStatus();
  varioApp.setTerrainProvider(&boxTerrain);
  launcher.setPack(&boxTerrain);
  settingsApp.setPack(&boxTerrain);
  diagnosticsSetPack(&pack);

  diagnosticsBegin(app, panel, compassHw);
  diagnosticsBench();                         // DIAGNOSTICS only

  host.add(varioApp);
  host.add(tetrisApp);
  host.add(compassApp);
  host.add(settingsApp);
  host.add(launcher);
  host.add(simApp);
  host.setLauncher(APP_LAUNCHER);
  host.setProbe(&probe);
  host.setRotation(DISPLAY_ROTATION);
  app.setClock(&wallClock);
  registry.add("Vario", "flying", "flight display, tracer link", APP_VARIO, ICON_VARIO);
  registry.add("Tetris", "game", "falling blocks", APP_TETRIS, ICON_TETRIS);
  registry.add("Compass", "& weather", "compass and weather, no tracer", APP_COMPASS, ICON_COMPASS);
  registry.add("Settings", "setup", "compass, mounting, time, about", APP_SETTINGS, ICON_SETTINGS, false);
  registry.add("Flight sim", "game", "paraglider on real terrain", APP_SIM, ICON_SIM);
  power.enableClockSleep({&boxSettings, &wallClock, &battery, &compassHw, &pack, DISPLAY_ROTATION});
  startStartupApp();
}

static uint32_t worstTickUs = 0;   // slowest host.tick() since the last status line

void loop() {
  watchdog::feed();
  uint32_t now = millis();
  pack.service(now);
  uint32_t t0 = micros();
  host.tick(now);
  if (power.takeResumed()) startStartupApp();   // woke from the clock sleep: no reset, the wall clock survived
  uint32_t dt = micros() - t0;
  if (dt > worstTickUs) worstTickUs = dt;
  diagnosticsTick(now, worstTickUs);
  delay(pack.loading() ? 0 : host.loopDelayMs());                  // 10 ms; 1 ms while a game runs its fixed frame clock
}
