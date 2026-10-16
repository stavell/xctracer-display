#pragma once
#include "apps/vario/app/app.h"
#include "apps/vario/view/prof.h"
#include "platform/app_host.h"

// The flight display as a platform app: forwards to App, which owns the flight model, the screen state
// machine and the redraw cadence (its wantsFrame()); the host clears, calls draw() and presents.
//
// Two flavours of the same code: the vario (power-off on the 3 s hold, laptop host drawing) and, in the box
// image, the compass & weather tool (a second App without tracer or laptop; the hold returns to the
// launcher). Both get the "Apps..." row in their menu when the host has a launcher.
class VarioApp : public IApp, private ILauncherHook {
 public:
  explicit VarioApp(App& app, const char* name = "vario", bool isVario = true)
      : app_(app), name_(name), isVario_(isVario) {}
  const char* name() const override { return name_; }
  void begin(uint32_t nowMs) override {
    app_.setLauncher(nav() && nav()->hasLauncher() ? this : nullptr);
    app_.begin(nowMs);
  }
  void end() override { app_.end(); }
  void tick(uint32_t nowMs) override { app_.tick(nowMs); }
  void onInput(Key key, ButtonEvent ev) override { if (key == KEY_MAIN) app_.onInput(ev); }
  void onHeld(uint32_t heldMs) override { app_.onHeld(heldMs); }
  bool wantsFrame(uint32_t nowMs) override { return app_.wantsFrame(nowMs); }
  // The vario polls its sensors and the link on every host iteration, so it always wants the next tick.
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override { app_.draw(bm, gfx, nowMs); }
  bool ownsPanel(uint32_t) override { return app_.hostDrawing(); }
  bool handlesHold() const override { return isVario_; }       // power off; the tool lets the host leave

 private:
  void openLauncher() override { if (nav()) nav()->openLauncher(); }
  App& app_;
  const char* name_;
  bool isVario_;
};

// Brackets the clear and the present of a frame for the draw-time profiler (diagnostics build).
class VarioProbe : public IFrameProbe {
 public:
  void clear(bool begin) override { if (begin) PROF_BEGIN(P_CLEAR); else PROF_END(P_CLEAR); }
  void present(bool begin) override { if (begin) PROF_BEGIN(P_PRESENT); else PROF_END(P_PRESENT); }
};
