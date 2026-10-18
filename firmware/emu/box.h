#pragma once
// The whole box on the PC: platform host + home screen + vario + Tetris + compass tool + Settings over mock ports. The
// wiring mirrors firmware/src/main_box.cpp (that one over the real hardware, this one over mocks). Used by
// the tests and by the player (`build.sh play box`).
#include <memory>
#include "apps/games/sim/sim_box.h"
#include "apps/games/tetris/tetris_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/settings/settings_app.h"
#include "apps/vario/app/compass_tool.h"
#include "apps/vario/app/terrain_kit.h"
#include "mocks.h"
#include "platform/app_registry.h"
#include "platform/clock.h"
#include "platform/lazy_app.h"

// Tilt of the box for the flight sim: the PC player drives it from the mouse, tests set it directly.
struct MockTilt : sim::ITilt {
  bool on = false;
  float roll = 0, pitch = 0;
  bool read(float* r, float* p) override { if (!on) return false; *r = roll; *p = pitch; return true; }
};

struct Box {
  enum { APP_VARIO, APP_TETRIS, APP_COMPASS, APP_SETTINGS, APP_LAUNCHER, APP_SIM };

  Rig r;                              // panel, switch, battery, tracer, laptop, key store + settings, compass, thermo, power
  MockKeyStore& keys = r.keys;
  WallClock clock;
  AppRegistry registry;
  App varioCore;
  VarioApp vario;
  // Like main_box.cpp: the tool, Tetris and the sim share one arena and exist only while they are the active app.
  static constexpr size_t kArenaBytes = maxSize(sizeof(sim::SimBox), maxSize(sizeof(TerrainKit), maxSize(sizeof(CompassTool), sizeof(TetrisApp))));
  AppArenaStore<kArenaBytes> arena;
  uint32_t seed;
  LazyApp<CompassTool> compassTool;
  LazyApp<TetrisApp> tetris;
  LazyApp<sim::SimBox> flightSim;
  LauncherApp launcher;
  SettingsApp settings;
  MockTilt tilt;
  TerrainKit* kit = nullptr;
  VarioProbe probe;
  AppHost host;
  uint32_t score = 0;

  struct Scores : IScoreStore {
    MockKeyStore& k;
    explicit Scores(MockKeyStore& s) : k(s) {}
    uint32_t load() override { return k.get(SLOT_TETRIS_BEST); }
    void save(uint32_t v) override { k.set(SLOT_TETRIS_BEST, v); }
  } scores;

  static CompassTool* makeCompass(void* mem, void* ctx) {
    Box& b = *static_cast<Box*>(ctx);
    return new (mem) CompassTool(b.r.panel, b.r.battery, b.r.settings, b.r.compass, b.r.thermo, b.r.power, cfg(true));
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
    s->setTilt(&b.tilt);
    return s;
  }

  static AppConfig cfg(bool tool) {
    AppConfig c;
    c.standalone = tool;
    return c;
  }

  explicit Box(uint32_t seed = 1)
      : varioCore(r.panel, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power, cfg(false)),
        vario(varioCore), seed(seed),
        compassTool(arena, "compass", makeCompass, this), tetris(arena, "tetris", makeTetris, this),
        flightSim(arena, "flightsim", makeSim, this),
        launcher(r.battery, r.power, registry, r.settings, clock), settings(r.battery, r.compass, r.keys, r.settings, registry, "0.1"),
        host(r.panel, r.input), scores(r.keys) {
    host.add(vario);
    host.add(tetris);
    host.add(compassTool);
    host.add(settings);
    host.add(launcher);
    host.add(flightSim);
    host.setLauncher(APP_LAUNCHER);
    host.setProbe(&probe);
    varioCore.setClock(&clock);
    registry.add("Vario", "flying", "flight display, tracer link", APP_VARIO, ICON_VARIO);
    registry.add("Tetris", "game", "falling blocks", APP_TETRIS, ICON_TETRIS);
    registry.add("Compass", "& weather", "compass and weather, no tracer", APP_COMPASS, ICON_COMPASS);
    registry.add("Settings", "setup", "compass, mounting, time, about", APP_SETTINGS, ICON_SETTINGS, false);
    registry.add("Flight sim", "game", "paraglider on real terrain", APP_SIM, ICON_SIM);
  }

  // A map pack for the vario (the kit must outlive the box); the compass tool has no terrain.
  void setTerrain(TerrainKit* k) {
    kit = k;                                                // the sim opens the same pack when it is started
    varioCore.setTerrain(k);
    launcher.setPack(k);
    settings.setPack(k);
  }

  // Boot like main_box.cpp: straight into the start-up app (the vario unless the launcher chose another).
  void boot(uint32_t now) {
    const int first = registry.startupApp(keys);
    if (first != APP_VARIO) r.tracer.pause();
    host.start(first, now);
  }
  void tick(uint32_t now) { host.tick(now); }
  const char* appName() const { return host.active() ? host.active()->name() : "-"; }
};
