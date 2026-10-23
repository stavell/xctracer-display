#pragma once
// The Map app of the box image: the map pack's relief, contours, roads and towns (platform/map/map_view), browsable.
//   - Position: from an IFixSource (the box: the XC Tracer's GPS). With a fix the map follows it and shows the pilot arrow;
//     without one it opens where it was told to (the first takeoff) and is a plain map to browse.
//   - North up, or Compass: turned so the direction the box points is up (platform MotionService, sensors on only then).
//   - Zoom 25 .. 400 m per pixel.
// Controls. The switch: short = zoom in (after the closest it wraps to the widest), long = North up / Compass.
// Six buttons: D-pad pans (stops following the fix), A / B zoom in / out, hold A = back to the fix, hold B = North up / Compass.
// Leaving is the system sheet's (3 s hold / A + B), like every app.
#include "platform/app_host.h"
#include "platform/fix_source.h"
#include "platform/gfx/ui.h"
#include "platform/map/map_view.h"
#include "platform/sensors/heading_smoother.h"
#include "platform/sensors/motion.h"
#include "platform/settings/box_settings.h"
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/terrain.h"

namespace mapapp {

class MapApp : public IApp {
 public:
  static constexpr int kTop = ui::BAR_H;                // the map area: below the title bar, to the bottom
  static constexpr int kW = 240, kH = 400 - kTop;
  static constexpr int kScales = 5;
  static constexpr int kStartScale = 2;                 // 100 m per pixel
  static constexpr uint32_t kStepUs = 9000;             // tile work per tick (the host ticks every ~10 ms)
  static constexpr uint32_t kFrameMs = 40;              // at most 25 frames a second (panning, turning)
  static constexpr uint32_t kTileFrameMs = 150;         // while tiles come in, a frame at most this often
  static constexpr uint32_t kHoldMs = 700;              // hold A / B this long for the second function
  static constexpr uint32_t kRepeatMs = 250;            // a D-pad press moves once, held this long it glides
  static constexpr float kStepPx = 24, kGlidePxS = 220;
  static constexpr uint32_t kFixStaleMs = 15000;        // an older fix draws a hollow arrow
  static constexpr uint32_t kToastMs = 1500, kHintMs = 4000;
  static constexpr float kTurnDeg = 0.5f;               // compass: a new frame when the heading moved this far

  MapApp(terrain::IBlockReader* pack, IBattery& battery, ICompass& compass, BoxSettings& settings, IFixSource* fix)
      : pack_(pack), battery_(battery), motion_(compass, settings), fix_(fix) {}
  void setStart(double lat, double lon) { startLat_ = lat; startLon_ = lon; }   // where the map opens without a fix

  const char* name() const override { return "map"; }
  void begin(uint32_t nowMs) override;
  void end() override;
  void tick(uint32_t nowMs) override;
  void onInput(Key key, ButtonEvent ev) override;
  void onKey(Key key, bool down, uint32_t nowMs) override;
  bool wantsFrame(uint32_t nowMs) override;
  uint32_t nextWakeMs(uint32_t nowMs) override { return nowMs + 10; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  // For tests and the PC.
  bool compassMode() const { return compass_; }
  bool following() const { return follow_; }
  int scaleIndex() const { return scale_; }
  static float scaleAt(int i);
  mapview::MapView& view() { return view_; }
  bool hasPack() const { return open_; }
  void setClock(uint32_t (*micros)()) { mapview::MapView::setClock(micros); }

 private:
  void zoom(int dir, bool wrap, uint32_t nowMs);
  void toggleCompass(uint32_t nowMs);
  void recentre(uint32_t nowMs);
  void toast(const char* s, uint32_t nowMs);
  void glide(uint32_t nowMs);
  void drawPilot(const Bitmap1& bm, uint32_t nowMs);
  void drawNorth(const Bitmap1& bm);
  void drawScale(const Bitmap1& bm);
  void drawNote(const Bitmap1& bm, const char* s, int y);

  terrain::IBlockReader* pack_;
  IBattery& battery_;
  MotionService motion_;
  IFixSource* fix_;
  terrain::FullTerrain terr_;
  terrain::MapVectors vec_;
  mapview::MapView view_;
  HeadingSmoother smooth_;
  UiFonts fonts_;
  double startLat_ = 42.6920, startLon_ = 24.7531;
  bool open_ = false, compass_ = false, follow_ = true, dirty_ = true, smoothTuned_ = false, smoothFused_ = false;
  int scale_ = kStartScale, battPct_ = -1;
  uint32_t lastFrameMs_ = 0, toastUntil_ = 0, hintUntil_ = 0, lastTickMs_ = 0;
  const char* toast_ = "";
  bool tilePending_ = false;
  // Keys: down or not, since when, whether its hold already fired.
  bool down_[KEY_COUNT] = {};
  uint32_t downMs_[KEY_COUNT] = {};
  bool held_[KEY_COUNT] = {};
  float panX_ = 0, panY_ = 0;                           // sub-pixel remainder of the glide (north up moves whole pixels)
  float shownHdg_ = 0;
  GeoFix fix0_;
  bool haveFix_ = false;
  float pilotSx_ = -1000, pilotSy_ = -1000;
};

}  // namespace mapapp
