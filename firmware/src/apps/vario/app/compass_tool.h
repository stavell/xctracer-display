#pragma once
// The compass & weather tool of the box image: the vario App without a tracer or laptop, always on the compass page,
// together with the (empty) ports it needs, as one object that can live in a LazyApp arena.
#include "apps/vario/app/app.h"
#include "apps/vario/app/null_ports.h"
#include "apps/vario/app/vario_app.h"

struct CompassToolCore {
  NullTracer noTracer;
  NullLaptop noLaptop;
  App app;
  CompassToolCore(IPanel& panel, IBattery& battery, BoxSettings& settings, ICompass& compass, IThermo& thermo,
                  IPower& power, const AppConfig& cfg)
      : app(panel, battery, noTracer, noLaptop, settings, compass, thermo, power, cfg) {}
};

class CompassTool : private CompassToolCore, public VarioApp {   // the core is a base so it is built before the adapter
 public:
  CompassTool(IPanel& panel, IBattery& battery, BoxSettings& settings, ICompass& compass, IThermo& thermo,
              IPower& power, const AppConfig& cfg)
      : CompassToolCore(panel, battery, settings, compass, thermo, power, cfg),
        VarioApp(CompassToolCore::app, "compass", false) {}
  App& core() { return CompassToolCore::app; }
};
