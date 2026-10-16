#pragma once
// The whole box on the PC: platform host + launcher + vario + Tetris + compass tool over mock ports. The
// wiring mirrors firmware/src/main_box.cpp (that one over the real hardware, this one over mocks). Used by
// the tests and by the player (`build.sh play box`).
#include "apps/games/tetris/tetris_app.h"
#include "apps/launcher/launcher_app.h"
#include "apps/vario/app/null_ports.h"
#include "apps/vario/app/terrain_kit.h"
#include "mocks.h"

struct Box {
  enum { APP_VARIO, APP_TETRIS, APP_COMPASS, APP_LAUNCHER };

  Rig r;                              // panel, switch, battery, tracer, laptop, settings, compass, thermo, power
  MockKeyStore keys;
  NullTracer noTracer;
  NullLaptop noLaptop;
  App varioCore, toolCore;
  VarioApp vario, compassTool;
  TetrisApp tetris;
  LauncherApp launcher;
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

  explicit Box(uint32_t seed = 1)
      : varioCore(r.panel, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power, cfg(false)),
        toolCore(r.panel, r.battery, noTracer, noLaptop, r.settings, r.compass, r.thermo, r.power, cfg(true)),
        vario(varioCore), compassTool(toolCore, "compass", false), tetris(seed),
        launcher(r.battery, r.power, keys, "0.1"), host(r.panel, r.input), scores(keys) {
    tetris.setScoreStore(&scores);
    host.add(vario);
    host.add(tetris);
    host.add(compassTool);
    host.add(launcher);
    host.setLauncher(APP_LAUNCHER);
    host.setProbe(&probe);
    launcher.addEntry("Vario", "flight display", APP_VARIO, ICON_VARIO);
    launcher.addEntry("Tetris", "falling blocks", APP_TETRIS, ICON_TETRIS);
    launcher.addEntry("Compass", "compass, weather", APP_COMPASS, ICON_COMPASS);
  }

  // A map pack for the vario (the kit must outlive the box); the compass tool has no terrain.
  void setTerrain(TerrainKit* kit) { varioCore.setTerrain(kit); }

  // Boot like main_box.cpp: straight into the start-up app (the vario unless the launcher chose another).
  void boot(uint32_t now) {
    const int first = launcher.startupApp();
    if (first != APP_VARIO) r.tracer.pause();
    host.start(first, now);
  }
  void tick(uint32_t now) { host.tick(now); }
  const char* appName() const { return host.active() ? host.active()->name() : "-"; }
};
