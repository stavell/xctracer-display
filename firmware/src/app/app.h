#pragma once
#include "app/ports.h"
#include "core/compass.h"
#include "core/flight.h"
#include "core/heading.h"
#include "core/heading_smoother.h"
#include "core/sim.h"
#include "view/viewmodel.h"

struct AppConfig {
  uint8_t rotation = 1;              // 1 = portrait, 3 = portrait flipped
  uint32_t hostGraceMs = 10000;      // laptop frames keep the screen this long after the last draw command
  uint32_t freshMs = 3000;           // data older than this counts as no data
  uint32_t drawEveryMs = 333;        // live screen redraw cadence (3 Hz, the tracer's data rate)
  uint32_t animCompassEveryMs = 33;  // compass page and standalone compass while the dial glides (30 fps)
  float gpsGlideTauMs = 250.0f;      // dial glide for the GPS track (about 3 updates per second)
  float magGlideTauMs = 90.0f;       // dial glide for the magnetic compass (sensor already filtered, 80 Hz)
  float magDeadbandDeg = 0.4f;       // compass target only moves beyond this, or after magDeadbandHoldMs of drift
  uint32_t magDeadbandHoldMs = 300;
  uint32_t animEveryMs = 50;         // redraw cadence while the dial is still gliding (compass, home)
  uint32_t animThermalEveryMs = 50;  // same on the thermal page (its map costs about the same to draw)
  uint32_t injectHoldMs = 3000;      // injected / simulated data silences the live tracer this long
  float gpsHeadingOnKmh = 4.0f;      // GPS track counts as a heading above this speed ...
  float gpsHeadingOffKmh = 2.5f;     // ... and stops counting below this one (hysteresis)
};

// The application: owns the flight model and the screen state machine (live / device list / power),
// talks to the world only through the ports in ports.h. Call tick() every ~10 ms.
class App {
 public:
  App(IPanel& panel, IInput& input, IBattery& battery, ITracer& tracer, ILaptop& laptop,
      ISettings& settings, ICompass& compass, IThermo& thermo, IPower& power, AppConfig cfg = AppConfig());

  void begin(uint32_t nowMs);
  void tick(uint32_t nowMs);

  const FlightModel& flight() const { return flight_; }
  uint32_t linesOk() const { return linesOk_; }
  uint32_t linesBad() const { return linesBad_; }
  bool simOn() const { return simOn_; }
  bool listOpen() const { return listOpen_; }
  bool calibrating() const { return calibrating_; }
  bool compassOn() const { return compassOn_; }
  const Compass& compass() const { return compass_; }
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
  bool hasData(uint32_t now) const;   // a valid sample arrived recently
  void drawCalibrateScreen(uint32_t now, uint32_t held);
  void drawLiveScreen(uint32_t now, uint32_t held);
  void drawListScreen(uint32_t now, uint32_t held);

  IPanel& panel_;
  IInput& input_;
  IBattery& battery_;
  ITracer& tracer_;
  ILaptop& laptop_;
  ISettings& settings_;
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
  bool compassOn_ = false;
  HeadingSelector heading_;          // GPS track or magnetic compass, whichever applies
  HeadingSource smoothSrc_ = HDG_NONE;   // source the smoother is currently tuned for
  HeadingSmoother smooth_;           // what the dial shows; advanced once per tick, never when drawing
  float ambientC_ = NAN, ambientRh_ = NAN;   // latest thermometer reading and when it came
  uint32_t ambientMs_ = 0;
  bool haveAmbient_ = false;
  bool calibrating_ = false;
  ViewModel vm_;
  ViewModel::Page page_ = ViewModel::PAGE_COMPASS;
  bool wasLocal_ = true;
  bool listOpen_ = false;
  int listSel_ = 0;
  uint32_t lastDraw_ = 0;
  bool redraw_ = true;
};
