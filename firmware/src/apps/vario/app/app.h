#pragma once
#include "apps/vario/app/ports.h"
#include "platform/sensors/ahrs.h"
#include "platform/sensors/compass.h"
#include "apps/vario/core/flight.h"
#include "apps/vario/core/heading.h"
#include "platform/sensors/heading_smoother.h"
#include "apps/vario/core/map_motion.h"
#include "apps/vario/core/sim.h"
#include "apps/vario/view/viewmodel.h"
#include "platform/clock.h"
#include "platform/settings/box_settings.h"
#include "platform/settings/cal_view.h"
#include "platform/settings/compass_cal.h"

struct TerrainKit;

struct AppConfig {
  uint8_t rotation = 1;              // 1 = portrait, 3 = portrait flipped
  uint32_t hostGraceMs = 10000;      // laptop frames keep the screen this long after the last draw command
  uint32_t freshMs = 3000;           // data older than this counts as no data
  uint32_t drawEveryMs = 333;        // live screen redraw cadence (3 Hz, the tracer's data rate)
  uint32_t animCompassEveryMs = 33;  // compass page and standalone compass while the dial glides (30 fps)
  float gpsGlideTauMs = 250.0f;      // dial glide for the GPS track (about 3 updates per second)
  float magGlideTauMs = 90.0f;       // dial glide for the magnetic compass alone (sensor already filtered, 80 Hz)
  float fusedGlideTauMs = 30.0f;     // dial glide for the gyro-fused heading (already smooth: nearly pass-through)
  float magDeadbandDeg = 0.4f;       // compass target only moves beyond this, or after magDeadbandHoldMs of drift
  uint32_t magDeadbandHoldMs = 300;
  // The fused compass redraws at the animation cadence while the heading differs from the one last drawn by
  // more than half a pixel at the dial ring (apps/vario/view/screens.cpp draws it with R = 104); below that, the slow cadence.
  static constexpr float DIAL_RING_RADIUS_PX = 104.0f;
  static constexpr float DIAL_HALF_PIXEL_DEG = 0.5f / DIAL_RING_RADIUS_PX * 57.29577951f;   // about 0.28 deg
  uint32_t animEveryMs = 50;         // redraw cadence while the dial is still gliding (compass, home)
  uint32_t animThermalEveryMs = 50;  // same on the thermal page (its map costs about the same to draw)
  uint32_t animMapEveryMs = 33;      // thermal page while the interpolated map is moving (30 fps)
  float mapScaleTauMs = 300.0f;      // the map zoom glides to a new step with this time constant
  // The map redraws at animMapEveryMs while the view moved more than half a pixel since the last drawn
  // frame: the centre (pixels at the current scale), or the rotation (at about the map's half height).
  static constexpr float MAP_HALF_RADIUS_PX = 104.0f;
  static constexpr float MAP_HALF_PIXEL_RAD = 0.5f / MAP_HALF_RADIUS_PX;
  bool mapHeadingUp = false;         // map page: north up (default, cheap to cache) or heading up (re-renders every 5 degrees)
  float mapMPerPx = 100.0f;          // map page scale (ground metres per pixel): the window shows 22 x 38 km
  bool standalone = false;           // the compass & weather tool: no tracer, no page switching, always the compass page
  uint32_t injectHoldMs = 3000;      // injected / simulated data silences the live tracer this long
  float gpsHeadingOnKmh = 4.0f;      // GPS track counts as a heading above this speed ...
  float gpsHeadingOffKmh = 2.5f;     // ... and stops counting below this one (hysteresis)
};

// Asked to leave for the home screen ("Home" row of the device list). Set only in the box image.
class ILauncherHook {
 public:
  virtual ~ILauncherHook() {}
  virtual void openLauncher() = 0;
};

// The application: owns the flight model and the screen state machine (live / device list / power),
// talks to the world only through the ports in ports.h. The platform host (platform/app_host.h) calls
// onInput() / onHeld() with the switch, tick() every ~10 ms, then wantsFrame() and, when true, draw()
// on a cleared panel followed by the panel's present(): the cadence rules below live in wantsFrame().
class App {
 public:
  App(IPanel& panel, IBattery& battery, ITracer& tracer, ILaptop& laptop,
      BoxSettings& settings, ICompass& compass, IThermo& thermo, IPower& power, AppConfig cfg = AppConfig());

  void begin(uint32_t nowMs);
  void end();                                        // leaving for another app: radios and sensors off
  void tick(uint32_t nowMs);
  static void forgetCreditsShown();                  // tests: the banner is once per boot, this is a new boot
  // Map picture job statistics for the status line: step() calls since the last call, the longest one (us), a job
  // running, the shown picture is sun-shaded. Resets the counters.
  void takeMapStats(uint32_t* steps, uint32_t* maxUs, bool* building, bool* sun);
  void onInput(ButtonEvent ev) { ev_ = ev; }         // the switch event, acted on by the next tick() (a missed one is dropped)
  void onHeld(uint32_t heldMs) { held_ = heldMs; }
  bool wantsFrame(uint32_t nowMs) const;
  void draw(Bitmap1& bm, Adafruit_GFX& g, uint32_t nowMs);
  bool hostDrawing() const { return !local_; }       // the laptop owns the screen
  void setLauncher(ILauncherHook* h) { launcher_ = h; }
  void setClock(WallClock* c) { clock_ = c; }       // the box's time of day, set from the tracer's GPS time
  // Attach a map pack (platform/terrain). With none (the default) nothing terrain-related is computed, drawn
  // or in the page cycle. The kit must outlive the App; pass nullptr to detach.
  void setTerrain(TerrainKit* kit);
  bool hasTerrain() const;

  const FlightModel& flight() const { return flight_; }
  uint32_t linesOk() const { return linesOk_; }
  uint32_t linesBad() const { return linesBad_; }
  bool simOn() const { return simOn_; }
  bool listOpen() const { return listOpen_; }
  bool calibrating() const { return calibrating_; }
  bool compassOn() const { return compassOn_; }
  const Compass& compass() const { return compass_; }
  const Ahrs& ahrs() const { return ahrs_; }
  bool fused() const { return fused_; }                     // the compass heading comes from the gyro fusion
  int mountIndex() const { return mount_; }
  const char* lastRaw() const { return lastRaw_; }          // last line received, parsed or not (diagnostics)
  void describeTracer(char* buf, size_t n) const { tracer_.describe(buf, n); }
  const ViewModel& buildViewModel(uint32_t nowMs);   // fills and returns the member (it is far too big for the stack)

 private:
  void handle(const Command& c, uint32_t now);
  void ingestLine(const char* line, uint32_t now);
  bool injecting(uint32_t now) const;
  void shutdown();
  void startCalibration();
  void finishCalibration(bool keep);
  void updateHeading(uint32_t now);
  void feedFlight(const Sample& s, uint32_t now);   // flight model and map motion together
  MapView mapView(uint32_t now, int trailN);
  void updateMap(uint32_t now);
  ViewModel::Page nextPage(ViewModel::Page p) const;   // the page cycle: compass, thermal, centring, [map], profile, home
  void prepareMapPage(const ViewModel& vm, uint32_t now);   // renders the cached relief when the pilot left its window                     // thermal page, once per tick: scale glide, moving flag
  void updateCredits(uint32_t now, ButtonEvent ev);
  void updateSun(const ViewModel& vm, uint32_t now);
  void stepMapPicture();
  bool hasData(uint32_t now) const;   // a valid sample arrived recently
  void drawCalibrateScreen(Bitmap1& bm, Adafruit_GFX& g, uint32_t now, uint32_t held);
  void drawLiveScreen(Bitmap1& bm, Adafruit_GFX& g, uint32_t now, uint32_t held);
  void drawListScreen(Bitmap1& bm, Adafruit_GFX& g, uint32_t now, uint32_t held);
  enum Mode : uint8_t { M_LIVE, M_LIST, M_CAL };
  Mode mode() const { return calibrating_ ? M_CAL : listOpen_ ? M_LIST : M_LIVE; }

  IPanel& panel_;
  IBattery& battery_;
  ITracer& tracer_;
  ILaptop& laptop_;
  BoxSettings& settings_;
  ICompass& compassHw_;
  IThermo& thermo_;
  IPower& power_;
  AppConfig cfg_;

  FlightModel flight_;
  FlightSim sim_;
  bool simOn_ = false;
  uint32_t injectMs_ = 0, lastSampleMs_ = 0;
  bool haveInject_ = false, haveSample_ = false;   // not 0-as-never: millis() is 0 once every 49.7 days
  uint32_t linesOk_ = 0, linesBad_ = 0;

  char lastRaw_[100] = "";
  Compass compass_;
  Ahrs ahrs_;                        // gyro + accel + mag fusion; the heading source when a gyro is present
  int mount_ = 0;                    // board -> case rotation (platform/sensors/mount.h), loaded from the settings
  bool fused_ = false;               // this tick's compass heading is the fused one
  uint32_t gyroMs_ = 0;
  bool haveGyro_ = false;
  float drawnHeading_ = NAN;         // the dial heading of the last live draw
  bool smoothFused_ = false;         // smoother is currently tuned for the fused heading
  bool compassOn_ = false;
  HeadingSelector heading_;          // GPS track or magnetic compass, whichever applies
  HeadingSource smoothSrc_ = HDG_NONE;   // source the smoother is currently tuned for
  HeadingSmoother smooth_;           // what the dial shows; advanced once per tick, never when drawing
  float ambientC_ = NAN, ambientRh_ = NAN;   // latest thermometer reading and when it came
  uint32_t ambientMs_ = 0;
  bool haveAmbient_ = false;
  MapMotion map_;                    // interpolated view centre / heading for the thermal map
  float mapScale_ = NAN, mapScaleTarget_ = NAN;   // shown zoom (px/m) and the step it glides to
  uint32_t mapScaleMs_ = 0;
  bool mapDrawnLive_ = false;        // the last frame drawn was a thermal map with interpolated motion
  bool mapMoving_ = false;           // the interpolated view differs from the last drawn frame
  float drawnCx_ = NAN, drawnCy_ = NAN, drawnMapHdg_ = NAN, drawnMapScale_ = NAN;
  bool calibrating_ = false;
  CompassCal cal_;                   // the platform's calibration flow; the screen is platform/settings/cal_view.h
  UiFonts calFonts_;                 // built on the first calibration screen
  WallClock* clock_ = nullptr;
  ViewModel vm_;
  ViewModel::Page page_ = ViewModel::PAGE_COMPASS;
  ViewModel::Page lastPage_ = ViewModel::PAGE_COMPASS;
  TerrainKit* kit_ = nullptr;
  static constexpr uint32_t kMapStepUs = 5000;      // the map picture job's share of one loop pass
  bool creditsOn_ = false;           // the once-per-boot credits banner is up
  uint32_t creditsUntil_ = 0;
  float sunAz_ = NAN, sunEl_ = NAN;                 // the sun for the map's shading (NaN: unknown)
  uint32_t sunAtMs_ = 0;
  bool sunKnown_ = false;
  bool wasLocal_ = true;
  bool listOpen_ = false;
  int listSel_ = 0;
  uint32_t lastDraw_ = 0;
  bool redraw_ = true;
  // Set by onInput / onHeld, consumed by tick.
  ButtonEvent ev_ = BTN_NONE;
  uint32_t held_ = 0;
  // Left by tick for wantsFrame / draw: the laptop does not own the screen, and which screen this tick
  // ran for. A screen change inside a tick (list opened, calibration started or ended) draws on the next one.
  bool local_ = true;
  Mode tickMode_ = M_LIVE;
  ILauncherHook* launcher_ = nullptr;
  bool left_ = false;                // end() ran: the next begin() restarts what end() stopped
};
