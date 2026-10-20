// The box image: home screen (launcher) + vario + Tetris + compass & weather + Settings on one platform.
// Wiring only. Boot: straight into the start-up app (default: the vario; Settings changes it).
// The home screen opens only on a deliberate gesture from inside an app (see README, "Box").
#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include "apps/compass/compass_app.h"
#ifdef WITH_EXAMPLES
#include "apps/examples/level/level_app.h"
#endif
#include "apps/games/sim/sim_box.h"
#include "apps/intro/intro_app.h"
#include "apps/games/tetris/tetris_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/pet/pet_app.h"
#include "apps/settings/settings_app.h"
#include "apps/vario/app/app.h"
#include "apps/vario/app/terrain_kit.h"
#include "apps/vario/app/vario_app.h"
#include "apps/vario/app/vario_box.h"
#include "apps/vario/core/flight_log.h"
#include "platform/hw/cyccnt.h"
#include "platform/hw/battery_adc.h"
#include "platform/hw/compass_lis.h"
#include "platform/hw/game_log.h"
#include "platform/pet/pet_feed.h"
#include "platform/pet/pet_status.h"
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
#include "platform/hw/baro_bmp.h"
#include "platform/power/retained.h"
#include "platform/power/weather.h"
#include "platform/hw/retention_nrf.h"
#include "platform/hw/pwr_log_nrf.h"
#include "platform/power/pwr_log.h"
#include "platform/hw/wth_status.h"
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
static BaroBmp baro;                          // the board barometer: only the sleeping clock reads it
static WeatherLog weather;                    // 24 h pressure history (288 B), sampled awake and asleep
// A copy of the history and the clock in RAM that start-up does not clear and System OFF keeps powered (platform/power/retained.h):
// they survive a reset and the power-off. 312 B in the retained page at the top of RAM (platformio.ini).
static retained::Block retainedBlock RETAINED_NOINIT;
static retained::Retention retention(retainedBlock);
static_assert(sizeof(retained::Block) + sizeof(pwrlog::Ring) + 64 <= 4096, "the retained RAM page (platformio.ini: .retained at 0x2003f000) is 4 kB");
static TracerBle tracer(config);
static LaptopLink laptopLink;
static LaptopBle laptop(panel, laptopLink);
// The pet snapshot from the laptop daemon (opcode 0x30): the BLE callback copies it into the mailbox, the Agents app reads it. Static: it outlives the app.
static pet::Feed petFeed;
static pet::Reporter petReporter;
static VarioRadios radios(laptop, tracer);
static PowerNrf power(panel, radios, PIN_BUTTON1);

// Games need no laptop: off while one runs (platform/ports.h ILinkGate), advertising again on the way out.
struct LaptopGate : ILinkGate {
  void setLinksOn(bool on) override {
    if (on) { laptop.startAdvertising(); return; }
    laptop.stopAdvertising();
    laptop.disconnect();
  }
};
static LaptopGate laptopGate;

static AppConfig appConfig() {
  AppConfig c;
  c.rotation = DISPLAY_ROTATION;
  return c;
}
// App indices in the host; the home screen lists all but itself.
enum { APP_VARIO, APP_TETRIS, APP_COMPASS, APP_SETTINGS, APP_LAUNCHER, APP_SIM, APP_INTRO
#ifndef WITH_EXAMPLES
       , APP_PET                                // Agents (apps/pet); the examples image is full at 8 apps (AppHost::MAX_APPS) and leaves it out
#else
       , APP_LEVEL                              // the example app of docs/PLATFORM.md, index = the order of host.add()
#endif
};

// The apps that are not the start-up app share ONE RAM block (see platform/lazy_app.h): each is built when it is opened
// and destroyed when it is left. Statically they cost 93 + 44 kB of .bss and left the image with 2 kB of heap, too little
// for FreeRTOS to start any task (no USB, blank screen). The block is as big as the biggest of them.
static constexpr size_t kArenaBytes = maxSize(sizeof(intro::IntroApp), maxSize(sizeof(sim::SimBox), maxSize(sizeof(VarioBox), maxSize(sizeof(compass::CompassApp), sizeof(TetrisApp)))));
static AppArenaStore<kArenaBytes> arena;

// The map pack lives in the QSPI flash (platform/hw/pack_service.h). The vario is an arena app like the others: the App
// (flight model, wind, thermals, trails), its adapter and its terrain kit (48 kB: tile cache, map page cache, vector
// buffers) are ONE object (VarioBox), built when the vario is opened and destroyed when it is left. Costs: the flight
// state (trail, wind, thermals) and the map cache start afresh on each return, and the vario comes back on the compass page.
// What outlives it is static: the radios and their queues (TracerBle, LaptopBle), settings, wall clock, saved tracer.
// The home screen and Settings > About ask about the pack without the vario running: the answers are copied from the
// kit once at boot (the pack cannot change without a reboot).
static PackService pack;
// "TIME epoch tz" from the PC (host/settime.py, and load_pack.py / logdl.py on every connect): the wall clock and the time zone.
static struct PcTime : storage::ITimeSink {
  bool setTime(int64_t epoch, int tz) override {
    if (!wallClock.setEpoch(epoch, millis())) return false;
    boxSettings.setTzMinutes(tz);
    return true;
  }
  int localMinutes() override { return wallClock.localMinutes(boxSettings.tzMinutes(), millis()); }
} pcTime;
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

// Validation recorder (README "Flight log"): tracer lines and the gyro turn rate go to the last 256 kB of the QSPI flash.
static flightlog::FlightLogger flightLog(pack.logSink());
static VarioBox* makeVario(void* mem, void*) {
  VarioBox* v = new (mem) VarioBox(panel, battery, tracer, laptop, boxSettings, compassHw, thermo, power, appConfig(), &wallClock,
                                   pack.reader());
  v->core().setLog(&flightLog);
  return v;
}
static LazyApp<VarioBox> varioApp(arena, "vario", makeVario);

// The compass & weather app (apps/compass): the motion service + thermometer + the pressure log, no vario App behind it.
static compass::CompassApp* makeCompass(void* mem, void*) {
  compass::CompassApp* t = new (mem) compass::CompassApp(battery, compassHw, thermo, boxSettings);
  t->setWeather(&weather);
  return t;
}
static LazyApp<compass::CompassApp> compassApp(arena, "compass", makeCompass);

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
// The start-up animation (apps/intro): an arena app like the others, gone before the start-up app is built. It reads the pack by itself.
static int introNext = APP_VARIO;
static intro::IntroApp* makeIntro(void* mem, void*) {
  intro::IntroApp* i = new (mem) intro::IntroApp(pack.reader());
  i->setNext(introNext);
  i->setClock(fastMicros);                    // begin() timings in the "gm" line
  return i;
}
static LazyApp<intro::IntroApp> introApp(arena, "intro", makeIntro);
#ifdef WITH_EXAMPLES                            // env feather_sense_box_examples: the Level example app, same arena (it is far smaller than the sim)
static examples::LevelApp* makeLevel(void* mem, void*) { return new (mem) examples::LevelApp(battery, compassHw, boxSettings); }
static LazyApp<examples::LevelApp> levelApp(arena, "level", makeLevel);
#endif
#ifndef WITH_EXAMPLES
// Agents: the Claude Code pets (docs/claude_pet.md). About 0.5 kB of state in the arena; the snapshots live in the static petFeed.
static pet_app::PetApp* makePet(void* mem, void*) { return new (mem) pet_app::PetApp(petFeed); }
static LazyApp<pet_app::PetApp> petApp(arena, "agents", makePet);
#endif
static LauncherApp launcher(battery, power, registry, boxSettings, wallClock);
static SettingsApp settingsApp(battery, compassHw, keys, boxSettings, registry, FW_VERSION);

static VarioProbe probe;
static SystemSheet systemSheet;                // the overlay of the 3 s hold / A + B (the vario-only image has none)
static AppHost host(panel, button);
// USB bench command "APP <name>" / "APP?" (platform/storage/log_shell.h): enters an app through the host's normal switch, the same
// request the launcher makes. It is a bench tool, not a gesture: the flight-safety rule (the box boots into the start-up app, the
// launcher opens only from inside an app) is about the pilot's hands; this needs a PC on the USB cable. Documented in CLAUDE.md.
static struct ShellApps : storage::IAppSink {
  bool openApp(const char* name) override {
    const int i = host.indexOf(name);
    if (i < 0) return false;
    host.openApp(i);
    return true;
  }
  const char* activeApp() override { return host.active() ? host.active()->name() : "none"; }
  bool reboot() override;
} shellApps;
bool ShellApps::reboot() {
  pwrlog::note(millis(), pwrlog::CMD, 2);
  retention.save(millis(), weather, wallClock);   // the history and the clock survive a plain reset
  Serial.flush();
  delay(100);
  NVIC_SystemReset();
  return true;
}
static char* scratchBuf() { size_t c; return diagnosticsScratch(&c); }
static GameLog gameLog(host, panel, fastMicros, &probe, scratchBuf(), 560);    // the "gm ..." line while a game runs; forwards clear / present to the vario probe

// Boot, and the way out of the clock sleep: straight into the start-up app (its begin() restarts what it needs).
// withIntro: a power-on, a wake from System OFF or the way out of the clock sleep (Settings > Intro Off disables all). The animation hands over
// to the start-up app when it ends or on any press; the vario's tracer scan keeps running meanwhile (it started in setup()).
static void startStartupApp(bool withIntro = false) {
#ifdef SIM_BENCH
  const int boot = APP_SIM;                   // bench build: straight into the sim, no operator needed
  withIntro = false;
#else
  const int boot = registry.startupApp(keys);
#endif
  if (boot != APP_VARIO) tracer.pause();      // the vario is not the first app: do not scan until it is opened
  if (withIntro && boxSettings.introEnabled()) {
    introNext = boot;
    host.start(APP_INTRO, millis());          // ends the active app (the vario pauses the tracer) ...
    if (boot == APP_VARIO) tracer.resume();   // ... but the scan must run during the animation (no-op at boot)
    return;
  }
  host.start(boot, millis());
}

void setup() {
  PowerNrf::gateOnWake(PIN_BUTTON1, 1000);   // before anything else: a quick tap goes back to sleep
  Serial.begin(115200);
  button.begin();
  TerrainMap::setClock(fastMicros);           // the map picture job spends a time budget per loop pass
  config.begin();                             // mounts the filesystem
  keys.begin();
  config.migrateTo(boxSettings);              // first start after the update: calibration, mounting, time zone move over
  retention.boot(weather, wallClock, retention_hw::clockSurvived(), millis());   // the history and (after a reset) the clock come back
  retention_hw::keepRam(&retainedBlock, sizeof retainedBlock);                   // and stay powered in System OFF
  pwrlog_hw::begin(readResetReason(), millis());                                 // the event ring of the last run is printed first
  panel.begin();
  compassHw.begin();                          // starts Wire
  thermo.begin();                             // SHT30 on the same bus
  baro.begin();                               // BMP280 too
  Wire.setClock(400000);

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);                      // one link to the laptop, one to the vario
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);
  tracer.begin();
  laptop.begin();                             // registers the vario's opcodes with the link
  laptopLink.add(pet::OP_SNAPSHOT, pet::OP_SNAPSHOT, petFeed);
  laptopLink.begin();                         // after every sink is registered

  pack.begin();
  pack.setTimeSink(&pcTime);
  pack.setAppSink(&shellApps);
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
  host.add(introApp);
#ifdef WITH_EXAMPLES
  host.add(levelApp);
#else
  host.add(petApp);
#endif
  host.setLauncher(APP_LAUNCHER);
  host.setProbe(&gameLog);
  host.setLinkGate(&laptopGate);
  host.setSystemSheet(&power, &systemSheet);                // the 3 s hold / A + B: Back, Home, Sleep, Power off
  host.setRotation(DISPLAY_ROTATION);
  registry.add("Vario", "flying", "flight display, tracer link", APP_VARIO, ICON_VARIO);
  registry.add("Compass", "& weather", "compass and weather, no tracer", APP_COMPASS, ICON_COMPASS);
  registry.add("Sim", "game", "paraglider on real terrain", APP_SIM, ICON_SIM);
  registry.add("Tetris", "game", "falling blocks", APP_TETRIS, ICON_TETRIS);
#ifdef WITH_EXAMPLES
  registry.add("Level", "example", "bubble level from the tilt sensor", APP_LEVEL, ICON_GENERIC, false);
#endif
  registry.add("Settings", "setup", "compass, mounting, time", APP_SETTINGS, ICON_SETTINGS, false);
#ifndef WITH_EXAMPLES
  registry.add("Agents", "Claude Code", "pets for your Claude Code sessions", APP_PET, ICON_GENERIC);
#endif
  power.enableClockSleep({&boxSettings, &wallClock, &battery, &compassHw, &pack, DISPLAY_ROTATION, &weather, &thermo, &baro, &retention});
  startStartupApp(true);
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
      static int rs = 0;
      if (zeroed && rs < 2 && now > 8000u + 20000u * rs) {   // timed restarts (the landing restart stall)
        rs++;
        const uint32_t t0 = fastMicros(), d0 = s->terrain().tileDecodes();
        s->startFlight();
        Serial.printf("bench restart %lu us, %lu decodes (startFlight returns)\n", (unsigned long)(fastMicros() - t0), (unsigned long)(s->terrain().tileDecodes() - d0));
      }
      if (s->mode() == sim::SimApp::Mode::ENDED) s->startFlight();
      if (zeroed && s->mode() == sim::SimApp::Mode::FLYING) {   // a turn pattern on the buttons: 5 s straight, 5 right, 5 straight, 5 left
        if (s->control() != sim::SimApp::Control::BUTTONS) s->setControl(sim::SimApp::Control::BUTTONS);
        const uint32_t ph = ((now - lastSwitch) / 5000) % 4;
        s->onKey(KEY_RIGHT, ph == 1, now);
        s->onKey(KEY_LEFT, ph == 3, now);
      }
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
  { static bool pwrDone = false; if (!pwrDone && Serial && Serial.availableForWrite() >= 200) pwrDone = pwrlog_hw::dumpStep(); }   // `pwr` lines, once per terminal
  retention.tick(now, weather, wallClock);         // bridges the restored history when the time is known; saves the block
  {
    uint32_t epoch = 0;
    if (!wallClock.epoch(now, &epoch)) epoch = 0;
    weather.sample(now, baro, epoch);              // one pressure sample per 10 min (a 10 ms read), so the history exists when the sleep starts
  }
  wth::tick(now, baro, thermo, battery, weather, retained::name(retention.result()));   // the "wth" status line (a terminal attached)
  uint32_t t0 = fastMicros();
  host.tick(now);
  if (power.takeResumed()) { pwrlog::note(now, pwrlog::WAKE, 1); button.flush(); startStartupApp(true); }   // woke from the clock sleep: no reset, the wall clock survived; the intro plays too
  {  // the box reports back to the laptop (notify characteristic D15A0003): on change and every 30 s while connected
    pet::Status st;
    st.shownValid = petFeed.anyShown();
    st.shownSeq = petFeed.shownSeq();
    st.appId = (uint8_t)(host.activeIndex() < 0 ? 255 : host.activeIndex());
    st.petActive = host.active() && strcmp(host.active()->name(), "agents") == 0;
    st.battPct = battery.percent();
    st.battMv = battery.millivolts();
    if (petReporter.due(now, laptopLink.connected(), st)) {
      uint8_t buf[pet::STATUS_BYTES];
      pet::encode(st, buf);
      laptopLink.reportStatus(buf, sizeof buf);
    }
  }
  uint32_t dt = fastMicros() - t0;   // not micros(): it once gave -67 s here (a 4.2e9 us 'worst tick')
  if (dt > worstTickUs) worstTickUs = dt;
  gameLog.tick(now, dt);
  diagnosticsSetApp(varioApp.alive() ? &varioApp->core() : nullptr);   // a destroyed App is never read
  diagnosticsTick(now, worstTickUs);
  delay(pack.loading() ? 0 : host.loopDelayMs());                  // 10 ms; 1 ms while a game runs its fixed frame clock
}
