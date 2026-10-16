#pragma once
#include "apps/vario/app/app.h"
#include "platform/app_host.h"

// The flight display as a platform app: forwards to App, which owns the screen and the cadence and polls
// the single switch through its own IInput port, so onInput() has nothing to do yet.
class VarioApp : public IApp {
 public:
  explicit VarioApp(App& app) : app_(app) {}
  const char* name() const override { return "vario"; }
  void begin(uint32_t nowMs) override { app_.begin(nowMs); }
  void end() override {}                       // the vario is the only app and never stops
  void tick(uint32_t nowMs) override { app_.tick(nowMs); }

 private:
  App& app_;
};
