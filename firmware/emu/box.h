#pragma once
// The whole box on the PC: platform host + home screen + vario + Tetris + compass tool + Settings over mock ports. The
// wiring mirrors firmware/src/main_box.cpp (that one over the real hardware, this one over mocks). Used by
// the tests and by the player (`build.sh play box`).
#include <memory>
#include "apps/games/sim/sim_box.h"
#include "apps/games/tetris/tetris_app.h"
#include "apps/intro/intro_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/map/map_app.h"
#include "apps/vario/app/tracer_fix.h"
#include "apps/settings/settings_app.h"
#include "apps/compass/compass_app.h"
#include "apps/vario/app/terrain_kit.h"
#include "apps/vario/app/vario_box.h"
#include "mocks.h"
#include "platform/app_registry.h"
#include "platform/clock.h"
#include "platform/lazy_app.h"
#include "platform/power/clock_view.h"
#include "platform/power/weather.h"

// Tilt of the box for the flight sim: the PC player drives it from the mouse, tests set it directly.
struct MockTilt : sim::ITilt {
  bool on = false;
  float roll = 0, pitch = 0;
  bool read(float* r, float* p) override { if (!on) return false; *r = roll; *p = pitch; return true; }
};

struct Box {
  enum { APP_VARIO, APP_TETRIS, APP_COMPASS, APP_SETTINGS, APP_LAUNCHER, APP_SIM, APP_INTRO, APP_MAP };

  Rig r;                              // panel, switch, battery, tracer, laptop, key store + settings, compass, thermo, power
  MockKeyStore& keys = r.keys;
  WallClock clock;
  AppRegistry registry;
  // Like main_box.cpp: the vario (with its map kit), the compass app, Tetris and the sim share one arena and exist only while they are the active app.
  static constexpr size_t kArenaBytes = maxSize(sizeof(mapapp::MapApp), maxSize(sizeof(intro::IntroApp), maxSize(sizeof(sim::SimBox), maxSize(sizeof(VarioBox), maxSize(sizeof(compass::CompassApp), sizeof(TetrisApp))))));
  AppArenaStore<kArenaBytes> arena;
  uint32_t seed;
  LazyApp<VarioBox> vario;
  LazyApp<compass::CompassApp> compassApp;
  LazyApp<TetrisApp> tetris;
  LazyApp<sim::SimBox> flightSim;
  LazyApp<intro::IntroApp> introApp;
  LazyApp<mapapp::MapApp> mapApp;
  TracerFix tracerFix{r.tracer};      // the map's GPS: the vario's tracer link
  int introNext = APP_VARIO;         // the app the start-up animation hands over to (set by boot())
  LauncherApp launcher;
  SettingsApp settings;
  MockTilt tilt;
  TerrainKit* kit = nullptr;
  VarioProbe probe;
  SystemSheet systemSheet;
  AppHost host;
  uint32_t score = 0;
  bool oneButton = false;             // the device today: only the switch (main_box.cpp: KEY_PIN_COUNT == 0); the PC player has the D-pad
  int seenRequests_ = 0, seenOffs_ = 0;

  struct Scores : IScoreStore {
    MockKeyStore& k;
    explicit Scores(MockKeyStore& s) : k(s) {}
    uint32_t load() override { return k.get(SLOT_TETRIS_BEST); }
    void save(uint32_t v) override { k.set(SLOT_TETRIS_BEST, v); }
  } scores;

  static VarioBox* makeVario(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    return new (mem) VarioBox(b.r.panel, b.r.battery, b.r.tracer, b.r.laptop, b.r.settings, b.r.compass, b.r.thermo, b.r.power,
                              cfg(), &b.clock, b.kit ? b.kit->terrain.reader() : nullptr);
  }
  App& varioCore() { return vario->core(); }                // the vario must be the active app
  static compass::CompassApp* makeCompass(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    compass::CompassApp* t = new (mem) compass::CompassApp(b.r.battery, b.r.compass, b.r.thermo, b.r.settings);
    t->setWeather(&b.weather);
    return t;
  }
  static TetrisApp* makeTetris(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    TetrisApp* t = new (mem) TetrisApp(b.seed);
    t->setScoreStore(&b.scores);
    return t;
  }
  // Like main_box.cpp: the sim opens the pack by itself (SimBox, a 2-tile cache), from the reader the vario's kit was opened on.
  static sim::SimBox* makeSim(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    sim::SimBox* s = new (mem) sim::SimBox(b.kit ? b.kit->terrain.reader() : nullptr);
    s->setTilt(&b.tilt);                                    // the device wires AccelTilt (SimBox::useAccel); tests give the angles
    s->setOneButton(b.oneButton);
    return s;
  }

  // Like main_box.cpp: the start-up animation reads the pack through the reader the vario's kit was opened on.
  static intro::IntroApp* makeIntro(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    intro::IntroApp* i = new (mem) intro::IntroApp(b.kit ? b.kit->terrain.reader() : nullptr);
    i->setNext(b.introNext);
    return i;
  }

  // Like main_box.cpp: the map opens the pack by itself and takes the position from the tracer link.
  static mapapp::MapApp* makeMap(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    return new (mem) mapapp::MapApp(b.kit ? b.kit->terrain.reader() : nullptr, b.r.battery, b.r.compass, b.r.settings, &b.tracerFix);
  }

  static AppConfig cfg() { return AppConfig(); }

  explicit Box(uint32_t seed = 1)
      : seed(seed), vario(arena, "vario", makeVario, this),
        compassApp(arena, "compass", makeCompass, this), tetris(arena, "tetris", makeTetris, this),
        flightSim(arena, "flightsim", makeSim, this), introApp(arena, "intro", makeIntro, this), mapApp(arena, "map", makeMap, this),
        launcher(r.battery, r.power, registry, r.settings, clock), settings(r.battery, r.compass, r.keys, r.settings, registry, "0.1"),
        host(r.panel, r.input), scores(r.keys) {
    host.add(vario);
    host.add(tetris);
    host.add(compassApp);
    host.add(settings);
    host.add(launcher);
    host.add(flightSim);
    host.add(introApp);
    host.add(mapApp);
    host.setLauncher(APP_LAUNCHER);
    host.setProbe(&probe);
    host.setLinkGate(&r.links);
    host.setSystemSheet(&r.power, &systemSheet);
    registry.add("Vario", "flying", "flight display, tracer link", APP_VARIO, ICON_VARIO);
    registry.add("Compass", "& weather", "compass and weather, no tracer", APP_COMPASS, ICON_COMPASS);
    registry.add("Sim", "game", "paraglider on real terrain", APP_SIM, ICON_SIM);
    registry.add("Tetris", "game", "falling blocks", APP_TETRIS, ICON_TETRIS);
    registry.add("Map", "topo + GPS", "topo map: roads, towns, GPS", APP_MAP, ICON_MAP);
    registry.add("Settings", "setup", "compass, mounting, time", APP_SETTINGS, ICON_SETTINGS, false);
  }

  // A map pack for the vario (the kit must outlive the box; the vario opens its own kit on its reader when it starts);
  // the compass app has no terrain.
  void setTerrain(TerrainKit* k) {
    kit = k;                                                // the sim opens the same pack when it is started
    launcher.setPack(k);
    settings.setPack(k);
  }

  // Boot like main_box.cpp: straight into the start-up app (the vario unless the launcher chose another). withIntro = a power-on
  // or the way out of the clock sleep: the start-up animation plays first unless Settings turned it off. A start-up app other
  // than the vario keeps the tracer off as before; the vario's own scan runs from the start, in parallel with the animation.
  void boot(uint32_t now, bool withIntro = false) {
    const int first = registry.startupApp(keys);
    if (first != APP_VARIO) r.tracer.pause();
    if (withIntro && r.settings.introEnabled()) {
      introNext = first;
      host.start(APP_INTRO, now);
      if (first == APP_VARIO) r.tracer.resume();            // as main_box.cpp: ending the old vario paused it, the scan runs during the animation
    } else {
      host.start(first, now);
    }
  }
  void tick(uint32_t now) {
    r.compass.nowMs = now;                                  // a mock gyro that follows the scripted orientation needs the time
    if (sleep != SS_AWAKE) { sleepTick(now); return; }
    host.tick(now);
    weather.sample(now, r.baro);                            // as main_box.cpp: sampled while awake too
    if (r.power.requests != seenRequests_) {                // the sheet's Sleep
      seenRequests_ = r.power.requests;
      enterSleep();                                       // the clock; holding on goes to System OFF
    }
    if (r.power.offs != seenOffs_) { seenOffs_ = r.power.offs; sleep = SS_OFF; }   // the sheet's Power off (or the 6 s hold)
  }

  // The clock sleep as main_box.cpp / platform/hw/power_nrf.cpp do it, over the mocks: radios and compass off, no app
  // ticking, the clock on the panel once per minute (platform/power/clock_sleep.h decides), a 1 s hold wakes into the
  // start-up app without a reset (the wall clock survives).
  enum SleepState { SS_AWAKE, SS_CLOCK, SS_RELEASE, SS_OFF } sleep = SS_AWAKE;
  ClockSleep sleeper;
  WeatherLog weather;                                       // the 24 h pressure history of the clock screen
  int barDraws = 0;                                         // countdown bar frames (the hold that powered off went on)
  int clockDraws = 0;                                       // clock screens drawn since the box started
  void enterSleep() {
    sleep = SS_CLOCK;
    sleeper.begin();
    r.compass.enable(false);
    r.tracer.pause();
  }
  void sleepTick(uint32_t now) {
    r.input.sample(now);                                    // the device's sampling timer keeps running; nobody pops the events
    const bool down = r.input.heldMs(now) > 0;
    if (sleep == SS_OFF) return;
    if (sleep == SS_RELEASE) {                           // the hold that woke the box must not reach the app as a press
      if (!down) { sleep = SS_AWAKE; r.input.flush(); boot(now, true); }   // main_box.cpp flushes too: the release must not skip the intro   // the wake plays the intro too (unless Off)
      return;
    }
    SleepIn in;
    in.nowMs = now;
    in.down = down;
    in.battPct = r.battery.percent();
    in.localMinute = clock.localMinutes(r.settings.tzMinutes(), now);
    in.msToMinute = clock.msToNextMinute(now);
    const SleepStep st = sleeper.step(in);
    if (st.act == SLEEP_DRAW) {
      if (st.bar <= 0) weatherRefresh(weather, now, r.thermo, r.baro);
      r.panel.setRotation(1);
      r.panel.clear();
      Bitmap1 bm = r.panel.bitmap();
      char date[16];
      long days;
      const bool haveDate = clock.localDays(r.settings.tzMinutes(), now, &days);
      if (haveDate) WallClock::formatDate(days, date, sizeof date);
      clockview::draw(bm, in.localMinute, in.battPct, &weather, st.bar, ClockSleep::kBarSteps, haveDate ? date : nullptr);
      r.panel.invalidate();
      r.panel.present();
      if (st.bar < 0) clockDraws++; else barDraws++;
    } else if (st.act == SLEEP_WAKE) {
      sleep = SS_RELEASE;
    } else if (st.act == SLEEP_OFF) {
      r.panel.clear();
      r.panel.invalidate();
      r.panel.present();
      sleep = SS_OFF;
    }
  }
  const char* appName() const { return host.active() ? host.active()->name() : "-"; }
};
