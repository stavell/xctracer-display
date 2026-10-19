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
#include "apps/vario/app/vario_box.h"
#include "platform/hw/cyccnt.h"
#include "platform/hw/battery_adc.h"
#include "platform/hw/compass_lis.h"
#include "platform/hw/game_log.h"
#ifdef SIM_BENCH
#include "platform/hw/mbench.h"
#endif
#include "apps/vario/hw/config_store.h"
#include "apps/vario/hw/diagnostics.h"
#include "platform/hw/input_button.h"
#include "platform/hw/key_store.h"
#include "platform/hw/keymap.h"
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

// The apps that are not the start-up app share ONE RAM block (see platform/lazy_app.h): each is built when it is opened
// and destroyed when it is left. Statically they cost 93 + 44 kB of .bss and left the image with 2 kB of heap, too little
// for FreeRTOS to start any task (no USB, blank screen). The block is as big as the biggest of them.
static constexpr size_t kArenaBytes = maxSize(sizeof(sim::SimBox), maxSize(sizeof(VarioBox), maxSize(sizeof(CompassTool), sizeof(TetrisApp))));
static AppArenaStore<kArenaBytes> arena;

// The map pack lives in the QSPI flash (platform/hw/pack_service.h). The vario is an arena app like the others: the App
// (flight model, wind, thermals, trails), its adapter and its terrain kit (48 kB: tile cache, map page cache, vector
// buffers) are ONE object (VarioBox), built when the vario is opened and destroyed when it is left. Costs: the flight
// state (trail, wind, thermals) and the map cache start afresh on each return, and the vario comes back on the compass page.
// What outlives it is static: the radios and their queues (TracerBle, LaptopBle), settings, wall clock, saved tracer.
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

static VarioBox* makeVario(void* mem, void*) {
  return new (mem) VarioBox(panel, battery, tracer, laptop, boxSettings, compassHw, thermo, power, appConfig(false), &wallClock,
                            pack.reader());
}
static LazyApp<VarioBox> varioApp(arena, "vario", makeVario);

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
// With only the user switch (no front buttons) it steers by the tilt of the board and runs on short / long presses; the
// accelerometer is switched on in begin() and off in end().
static sim::SimBox* makeSim(void* mem, void*) {
  sim::SimBox* s = new (mem) sim::SimBox(pack.reader());
  s->setClock(fastMicros);                    // times its parts for the "gm" line (game_log.h)
  if (KEY_PIN_COUNT == 0) s->useAccel(compassHw, boxSettings);
#ifdef SIM_BENCH
  s->showDayScreen(false);                    // bench build: fly at once, assist on (see the loop)
  s->setAssist(true);
#endif
  return s;
}
static LazyApp<sim::SimBox> simApp(arena, "flightsim", makeSim);
static LauncherApp launcher(battery, power, registry, boxSettings, wallClock);
static SettingsApp settingsApp(battery, compassHw, keys, boxSettings, registry, FW_VERSION);

static VarioProbe probe;
static AppHost host(panel, button);
static char* scratchBuf() { size_t c; return diagnosticsScratch(&c); }
static GameLog gameLog(host, panel, fastMicros, &probe, scratchBuf(), 560);    // the "gm ..." line while a game runs; forwards clear / present to the vario probe

// Boot, and the way out of the clock sleep: straight into the start-up app (its begin() restarts what it needs).
static void startStartupApp() {
#ifdef SIM_BENCH
  const int boot = APP_SIM;                   // bench build: straight into the sim, no operator needed
#else
  const int boot = registry.startupApp(keys);
#endif
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
  launcher.setPack(&boxTerrain);
  settingsApp.setPack(&boxTerrain);
  diagnosticsSetPack(&pack);

  diagnosticsBegin(nullptr, panel, compassHw);   // the vario's App exists only while it runs: the loop hands it over
  diagnosticsBench();                         // DIAGNOSTICS only

  host.add(varioApp);
  host.add(tetrisApp);
  host.add(compassApp);
  host.add(settingsApp);
  host.add(launcher);
  host.add(simApp);
  host.setLauncher(APP_LAUNCHER);
  host.setProbe(&gameLog);
  host.setRotation(DISPLAY_ROTATION);
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
#ifdef SIM_BENCH
  {
    static bool mbDone = false;
    static uint32_t mbPrint = 0;
    if (!mbDone && now > 1500) { mbench::run(); mbDone = true; }
    if (mbDone && now - mbPrint > 2000 && Serial) { mbPrint = now; Serial.println(mbench::line); }
  }
  {  // bench build: the sim flies itself; level = how the board lies, the view alternates every 20 s, a landing restarts
    static uint32_t lastSwitch = 0;
    static bool zeroed = false;
    if (host.active() == &simApp && simApp.alive()) {
      sim::SimBox* s = simApp.get();
      if (!zeroed && now > 3000) { s->onInput(KEY_MAIN, BTN_LONG); zeroed = true; }   // the zero page: as it lies = level, fly
      if (s->mode() == sim::SimApp::Mode::ENDED) s->startFlight();
      if (now - lastSwitch > 20000) {
        lastSwitch = now;
        s->zeroTilt();
        s->setView(s->view() == sim::SimApp::View::TOP ? sim::SimApp::View::CHASE : sim::SimApp::View::TOP);
        Serial.printf("bench view=%s\n", s->view() == sim::SimApp::View::TOP ? "map" : "3d");
      }
    }
  }
#endif
  pack.service(now);
  uint32_t t0 = fastMicros();
  host.tick(now);
  if (power.takeResumed()) startStartupApp();   // woke from the clock sleep: no reset, the wall clock survived
  uint32_t dt = fastMicros() - t0;   // not micros(): it once gave -67 s here (a 4.2e9 us 'worst tick')
  if (dt > worstTickUs) worstTickUs = dt;
  gameLog.tick(now, dt);
  diagnosticsSetApp(varioApp.alive() ? &varioApp->core() : nullptr);   // a destroyed App is never read
  diagnosticsTick(now, worstTickUs);
  delay(pack.loading() ? 0 : host.loopDelayMs());                  // 10 ms; 1 ms while a game runs its fixed frame clock
}
