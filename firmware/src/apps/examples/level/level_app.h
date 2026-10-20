#pragma once
// Example app "Level": a bubble level that reads the box's tilt. It shows what an app needs from the platform and nothing else:
// MotionService for roll / pitch, game frame mode (50 fps), the title bar and hint line of platform/gfx/ui.h, the switch and the
// six buttons, and the system sheet as the only way out. A press freezes the reading ("hold"). See docs/PLATFORM.md.
#include "platform/app_host.h"
#include "platform/gfx/ui.h"
#include "platform/sensors/motion.h"

namespace examples {

class LevelApp : public IApp {
 public:
  static const uint32_t kFrameMs = 20;                 // 50 fps
  static constexpr float kFullScaleDeg = 15.0f;        // the bubble reaches the vial's rim at this tilt
  static constexpr float kAlpha = 0.25f;               // smoothing per step (tau about 70 ms)

  LevelApp(IBattery& battery, ICompass& compass, BoxSettings& settings) : battery_(battery), motion_(compass, settings) {}

  const char* name() const override { return "level"; }
  void begin(uint32_t nowMs) override;
  void end() override { motion_.stop(); }              // sensors off the moment the app is left
  void tick(uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override { if (key == KEY_MAIN && ev == BTN_SHORT) hold_ = !hold_; }
  void onKey(Key key, bool down, uint32_t) override { if (down && key == KEY_ACTION) hold_ = !hold_; }
  uint32_t frameIntervalMs() const override { return kFrameMs; }
  void step(uint32_t simMs, uint32_t dtMs) override;
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  bool holding() const { return hold_; }
  bool haveTilt() const { return have_; }
  float rollDeg() const { return roll_; }              // the shown (smoothed, possibly frozen) reading
  float pitchDeg() const { return pitch_; }

 private:
  IBattery& battery_;
  MotionService motion_;
  UiFonts fonts_;
  bool hold_ = false, have_ = false;
  float roll_ = 0, pitch_ = 0;
  int battPct_ = -1;
  uint32_t battAt_ = 0;
};

}  // namespace examples
