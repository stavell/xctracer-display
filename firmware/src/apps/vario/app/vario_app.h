#pragma once
#include "apps/vario/app/app.h"
#include "apps/vario/view/prof.h"
#include "platform/app_host.h"

struct TerrainKit;

// Gives the vario its terrain kit while it runs. The box image keeps the 65 kB kit in the shared app arena (the vario is
// the only app that needs it), so the kit exists from begin() to end(): acquire() builds and opens it (nullptr: no pack
// or no room, the vario then shows no terrain), release() destroys it.
struct ITerrainProvider {
  virtual ~ITerrainProvider() {}
  virtual TerrainKit* acquire() = 0;
  virtual void release() = 0;
};

// The flight display as a platform app: forwards to App, which owns the flight model, the screen state
// machine and the redraw cadence (its wantsFrame()); the host clears, calls draw() and presents.
//
// Two flavours of the same code: the vario (power-off on the 3 s hold, laptop host drawing) and, in the box
// image, the compass & weather tool (a second App without tracer or laptop; the hold returns to the
// launcher). Both get the "Apps..." row in their menu when the host has a launcher.
class VarioApp : public IApp {
 public:
  explicit VarioApp(App& app, const char* name = "vario", bool isVario = true)
      : app_(app), name_(name), isVario_(isVario) {}
  const char* name() const override { return name_; }
  void setTerrainProvider(ITerrainProvider* p) { terrain_ = p; }
  void begin(uint32_t nowMs) override {
    if (terrain_) app_.setTerrain(terrain_->acquire());
    app_.begin(nowMs);
  }
  void end() override {
    app_.end();
    if (terrain_) {                    // the kit goes away with the app; the next begin() builds it again
      app_.setTerrain(nullptr);
      terrain_->release();
    }
  }
  void tick(uint32_t nowMs) override { app_.tick(nowMs); }
  void onInput(Key key, ButtonEvent ev) override { if (key == KEY_MAIN) app_.onInput(ev); }
  // The six buttons drive the same screens as the switch (a box may have both fitted): down / right = press, A = long press, B = back.
  void onKey(Key key, bool down, uint32_t) override {
    if (!down) return;
    if (key == KEY_DOWN || key == KEY_RIGHT) onInput(KEY_MAIN, BTN_SHORT);
    else if (key == KEY_ACTION) onInput(KEY_MAIN, BTN_LONG);
    else if (key == KEY_B) app_.backKey();
  }
  void onHeld(uint32_t heldMs) override { app_.onHeld(heldMs); }
  SystemSheet::Row sheetStart() const override { return SystemSheet::R_BACK; }
  bool wantsFrame(uint32_t nowMs) override { return app_.wantsFrame(nowMs); }
  // The vario polls its sensors and the link on every host iteration, so it always wants the next tick.
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override {
    app_.setSixHint(nav() && nav()->sixButtons());
    app_.draw(bm, gfx, nowMs);
  }
  bool ownsPanel(uint32_t) override { return app_.hostDrawing(); }
  bool handlesHold() const override { return isVario_; }       // power off; the tool lets the host leave

 private:
  App& app_;
  const char* name_;
  bool isVario_;
  ITerrainProvider* terrain_ = nullptr;
};

// Brackets the clear and the present of a frame for the draw-time profiler (diagnostics build).
class VarioProbe : public IFrameProbe {
 public:
  void clear(bool begin) override { if (begin) PROF_BEGIN(P_CLEAR); else PROF_END(P_CLEAR); }
  void present(bool begin) override { if (begin) PROF_BEGIN(P_PRESENT); else PROF_END(P_PRESENT); }
};
