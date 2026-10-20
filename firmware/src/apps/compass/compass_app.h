#pragma once
// The Compass & weather app of the box image. No tracer, no flight model: it takes the orientation from the platform's
// MotionService, the air from IThermo, the pressure history from the box's WeatherLog and the battery from IBattery.
//   Page 1 (opens here): the 3D compass in a glass dome (apps/compass3d, a game-mode app: 50 fps).
//   Page 2 (short press): the flat heading-up dial (platform/gfx/widgets/compass_dial.h) and the weather strip.
// A short press (or DOWN / RIGHT / A on the six buttons) swaps the pages. Leaving is the system sheet's (3 s hold / A + B);
// calibration and mounting are the Settings app's. The sensors run only while the app is open (begin() .. end()).
#include "apps/compass3d/compass3d.h"
#include "platform/app_host.h"
#include "platform/gfx/font.h"
#include "platform/gfx/ui.h"
#include "platform/power/weather.h"
#include "platform/sensors/heading_smoother.h"
#include "platform/sensors/motion.h"
#include "platform/settings/box_settings.h"

namespace compass {

class CompassApp : public IApp {
 public:
  static const uint32_t kFrameMs = 20;               // 50 fps on the dome
  static constexpr uint32_t kFlatEveryMs = 333;      // the flat page's slow cadence (the thermometer reports every 2 s)
  static constexpr uint32_t kFlatAnimMs = 33;        // while the dial glides (30 fps)
  static constexpr float kFusedTauMs = 30.0f;        // dial glide: gyro-fused heading (already smooth: nearly pass-through)
  static constexpr float kMagTauMs = 90.0f;          // dial glide: the magnetic compass alone
  static constexpr float kDeadbandDeg = 0.4f;        // compass-only target moves beyond this, or after kDeadbandHoldMs of drift
  static constexpr uint32_t kDeadbandHoldMs = 300;
  static constexpr uint32_t kAmbientFreshMs = 10000; // a thermometer reading older than this is no reading
  static constexpr float kHalfPixelDeg = 0.5f / 104.0f * 57.29577951f;   // half a pixel at the dial ring (R = 104)

  CompassApp(IBattery& battery, ICompass& compass, IThermo& thermo, BoxSettings& settings)
      : battery_(battery), thermo_(thermo), motion_(compass, settings) {}
  void setWeather(const WeatherLog* w) { weather_ = w; }     // the box's pressure log (sampled by the main loop)

  const char* name() const override { return "compass"; }
  void begin(uint32_t nowMs) override;
  void end() override;
  void tick(uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  // Game mode (a steady frame clock) while the dome is on screen; change-driven on the flat page.
  uint32_t frameIntervalMs() const override { return flat_ ? 0 : kFrameMs; }
  bool wantsFrame(uint32_t nowMs) override;
  void step(uint32_t simMs, uint32_t dtMs) override;
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  bool showing3d() const { return !flat_; }
  bool flatPage() const { return flat_; }
  compass3d::Compass3d& view3d() { return view_; }
  const MotionService& motion() const { return motion_; }
  float ambientC(uint32_t nowMs) const { return haveAmbient_ && nowMs - ambientMs_ <= kAmbientFreshMs ? ambientC_ : NAN; }
  float ambientRh(uint32_t nowMs) const { return haveAmbient_ && nowMs - ambientMs_ <= kAmbientFreshMs ? ambientRh_ : NAN; }

 private:
  void toggle();
  void drawFlat(Bitmap1& bm, uint32_t nowMs);
  void cell(const Bitmap1& bm, int x, int y, int w, const char* label);

  IBattery& battery_;
  IThermo& thermo_;
  MotionService motion_;
  const WeatherLog* weather_ = nullptr;
  compass3d::Compass3d view_;
  UiFonts fonts_;
  Font1 big_;                                         // SansBold 18 for the strip's numbers
  HeadingSmoother smooth_;
  bool smoothFused_ = false, smoothTuned_ = false;
  bool flat_ = false;
  bool redraw_ = true;
  float drawnHeading_ = NAN;
  float ambientC_ = NAN, ambientRh_ = NAN;
  uint32_t ambientMs_ = 0, lastDraw_ = 0, nowMs_ = 0;
  bool haveAmbient_ = false;
  int battPct_ = -1;
};

}  // namespace compass
