#pragma once
// The vario of the box image as ONE object that lives in the shared app arena (platform/lazy_app.h): the flight App (flight
// model, wind, thermal memory, trails, view caches), its IApp adapter and the map kit (47 kB) are built when the vario is
// opened and destroyed when it is left, so the other apps get the same RAM. What must outlive it stays outside: the BLE
// radios and their queues (TracerBle, LaptopBle), the settings, the wall clock, the saved tracer.
// Leaving the vario therefore loses its flight state (trail, wind, thermals); coming back starts fresh.
#include "apps/vario/app/app.h"
#include "apps/vario/app/terrain_kit.h"
#include "apps/vario/app/vario_app.h"

struct VarioBoxCore {
  // Opens the kit that sits next to the App on the vario's begin(); a member, so the core itself has no virtuals.
  struct Feed : ITerrainProvider {
    TerrainKit& kit;
    terrain::IBlockReader* reader;  // the map pack, or nullptr: no terrain page
    Feed(TerrainKit& k, terrain::IBlockReader* r) : kit(k), reader(r) {}
    TerrainKit* acquire() override { return reader && kit.open(reader) ? &kit : nullptr; }
    void release() override {}      // the kit goes away with the object
  };
  App app;
  TerrainKit kit;
  Feed feed;

  VarioBoxCore(IPanel& panel, IBattery& battery, ITracer& tracer, ILaptop& laptop, BoxSettings& settings,
               ICompass& compass, IThermo& thermo, IPower& power, const AppConfig& cfg, terrain::IBlockReader* r)
      : app(panel, battery, tracer, laptop, settings, compass, thermo, power, cfg), feed(kit, r) {}
};

class VarioBox : private VarioBoxCore, public VarioApp {   // the core is a base so it is built before the adapter
 public:
  VarioBox(IPanel& panel, IBattery& battery, ITracer& tracer, ILaptop& laptop, BoxSettings& settings,
           ICompass& compass, IThermo& thermo, IPower& power, const AppConfig& cfg, WallClock* clock,
           terrain::IBlockReader* reader)
      : VarioBoxCore(panel, battery, tracer, laptop, settings, compass, thermo, power, cfg, reader),
        VarioApp(VarioBoxCore::app, "vario", true) {
    VarioBoxCore::app.setClock(clock);
    setTerrainProvider(&feed);
  }
  App& core() { return VarioBoxCore::app; }
};
