#pragma once
// The whole box on the PC: platform host + home screen + vario + Tetris + compass tool + Settings over mock ports. The
// wiring mirrors firmware/src/main_box.cpp (that one over the real hardware, this one over mocks). Used by
// the tests and by the player (`build.sh play box`).
#include <memory>
#include "apps/games/sim/sim_app.h"
#include "apps/games/tetris/tetris_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/settings/settings_app.h"
#include "apps/vario/app/null_ports.h"
#include "apps/vario/app/terrain_kit.h"
#include "mocks.h"
#include "platform/app_registry.h"
#include "platform/clock.h"

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
  NullTracer noTracer;
  NullLaptop noLaptop;
  App varioCore, toolCore;
  VarioApp vario, compassTool;
  TetrisApp tetris;
  LauncherApp launcher;
  SettingsApp settings;
  std::unique_ptr<sim::SimApp> flightSim{new sim::SimApp};   // ~90 kB: not part of the Box object
  MockTilt tilt;
  VarioProbe probe;
  AppHost host;
  uint32_t score = 0;

  struct Scores : IScoreStore {
    MockKeyStore& k;
    explicit Scores(MockKeyStore& s) : k(s) {}
    uint32_t load() override { return k.get(SLOT_TETRIS_BEST); }
    void save(uint32_t v) override { k.set(SLOT_TETRIS_BEST, v); }
  } scores;

  static AppConfig cfg(bool tool) {
    AppConfig c;
    c.standalone = tool;
    return c;
  }

  explicit Box(uint32_t seed = 1, bool withSim = false)
      : varioCore(r.panel, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power, cfg(false)),
        toolCore(r.panel, r.battery, noTracer, noLaptop, r.settings, r.compass, r.thermo, r.power, cfg(true)),
        vario(varioCore), compassTool(toolCore, "compass", false), tetris(seed),
        launcher(r.battery, r.power, registry, r.settings, clock), settings(r.battery, r.compass, r.keys, r.settings, registry, "0.1"),
        host(r.panel, r.input), scores(r.keys) {
    tetris.setScoreStore(&scores);
    host.add(vario);
    host.add(tetris);
    host.add(compassTool);
    host.add(settings);
    host.add(launcher);
    flightSim->setTilt(&tilt);
    if (withSim) host.add(*flightSim);                      // the Sim joins the home grid only when asked: the other tests count tiles
    host.setLauncher(APP_LAUNCHER);
    host.setProbe(&probe);
    varioCore.setClock(&clock);
    registry.add("Vario", "flying", "flight display, tracer link", APP_VARIO, ICON_VARIO);
    registry.add("Tetris", "game", "falling blocks", APP_TETRIS, ICON_TETRIS);
    registry.add("Compass", "& weather", "compass and weather, no tracer", APP_COMPASS, ICON_COMPASS);
    registry.add("Settings", "setup", "compass, mounting, time, about", APP_SETTINGS, ICON_SETTINGS, false);
    if (withSim) registry.add("Flight sim", "game", "paraglider on real terrain", APP_SIM, ICON_SIM);
  }

  // A map pack for the vario (the kit must outlive the box); the compass tool has no terrain.
  void setTerrain(TerrainKit* kit) {
    varioCore.setTerrain(kit);
    flightSim->setTerrain(kit ? &kit->terrain : nullptr);
    launcher.setPack(kit);
    settings.setPack(kit);
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
