#pragma once
// The compass & weather tool of the box image: the vario App without a tracer or laptop, together with the (empty) ports it needs,
// as one object that can live in a LazyApp arena. It opens on the 3D compass (apps/compass3d: a card floating level in a glass
// dome, 50 fps); a short press of the switch swaps to the flat dial and weather page of the vario (the page the tool always had),
// and back. The menu (long press: back, Home) and the calibration screen are the App's own and take over the frame loop while
// they are open, so every gesture that leaves the tool is unchanged.
#include "apps/compass3d/compass3d.h"
#include "apps/vario/app/app.h"
#include "apps/vario/app/null_ports.h"
#include "apps/vario/app/vario_app.h"
#include "platform/power/weather.h"

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
  static const uint32_t kFrameMs = 20;               // 50 fps
  CompassTool(IPanel& panel, IBattery& battery, BoxSettings& settings, ICompass& compass, IThermo& thermo,
              IPower& power, const AppConfig& cfg)
      : CompassToolCore(panel, battery, settings, compass, thermo, power, cfg),
        VarioApp(CompassToolCore::app, "compass", false) {}
  App& core() { return CompassToolCore::app; }
  compass3d::Compass3d& view3d() { return view_; }
  bool showing3d() const { return use3d(); }
  bool flatPage() const { return flat_; }
  void setWeather(const WeatherLog* w) { weather_ = w; }     // the 24 h pressure log of the box (sampled by the main loop)

  void begin(uint32_t nowMs) override {
    flat_ = false;
    view_.reset();
    VarioApp::begin(nowMs);
  }
  void onInput(Key key, ButtonEvent ev) override {
    const App& a = CompassToolCore::app;
    if (key == KEY_MAIN && ev == BTN_SHORT && !a.listOpen() && !a.calibrating()) {
      flat_ = !flat_;                                  // 3D dome <-> flat dial and weather
      CompassToolCore::app.requestRedraw();
      return;
    }
    VarioApp::onInput(key, ev);
  }
  // Game mode (a steady frame clock) while the dome is on screen; the App's own change-driven cadence for the flat page and its screens.
  uint32_t frameIntervalMs() const override { return use3d() ? kFrameMs : 0; }
  bool wantsFrame(uint32_t nowMs) override { return use3d() ? false : VarioApp::wantsFrame(nowMs); }
  void step(uint32_t simMs, uint32_t dtMs) override {
    const App& a = CompassToolCore::app;
    compass3d::Input in;
    in.valid = a.fused() && a.ahrs().valid() && !a.ahrs().gyroFailed();
    if (in.valid) {
      a.ahrs().quaternion(in.q);
      in.headingDeg = a.ahrs().heading();
      in.trust = a.ahrs().trust();
    }
    in.calibrated = a.compass().calibration().valid;
    in.tempC = a.ambientC(nowMs_);
    in.rhPct = a.ambientRh(nowMs_);
    if (weather_) {
      in.pressure10 = weather_->latest();
      in.trendOk = weather_->tendency(in.trendCls);
    }
    view_.step(in, dtMs, simMs);
  }
  void tick(uint32_t nowMs) override {
    nowMs_ = nowMs;
    VarioApp::tick(nowMs);
  }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override {
    if (use3d()) view_.draw(bm, nowMs);
    else VarioApp::draw(bm, gfx, nowMs);
  }

 private:
  bool use3d() const { return !flat_ && !CompassToolCore::app.listOpen() && !CompassToolCore::app.calibrating(); }
  compass3d::Compass3d view_;
  bool flat_ = false;
  uint32_t nowMs_ = 0;
  const WeatherLog* weather_ = nullptr;
};
