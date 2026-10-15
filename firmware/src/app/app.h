#pragma once
#include "app/ports.h"
#include "core/flight.h"
#include "core/sim.h"
#include "view/viewmodel.h"

struct AppConfig {
  uint8_t rotation = 1;              // 1 = portrait, 3 = portrait flipped
  uint32_t hostGraceMs = 10000;      // laptop frames keep the screen this long after the last draw command
  uint32_t freshMs = 3000;           // data older than this counts as no data
  uint32_t drawEveryMs = 333;        // live screen redraw cadence (3 Hz, the tracer's data rate)
  uint32_t injectHoldMs = 3000;      // injected / simulated data silences the live tracer this long
};

// The application: owns the flight model and the screen state machine (live / device list / power),
// talks to the world only through the ports in ports.h. Call tick() every ~10 ms.
class App {
 public:
  App(IPanel& panel, IInput& input, IBattery& battery, ITracer& tracer, ILaptop& laptop,
      ISettings& settings, IPower& power, AppConfig cfg = AppConfig());

  void begin(uint32_t nowMs);
  void tick(uint32_t nowMs);

  const FlightModel& flight() const { return flight_; }
  uint32_t linesOk() const { return linesOk_; }
  uint32_t linesBad() const { return linesBad_; }
  bool simOn() const { return simOn_; }
  bool listOpen() const { return listOpen_; }
  const ViewModel& buildViewModel(uint32_t nowMs);   // fills and returns the member (it is far too big for the stack)

 private:
  void handle(const Command& c, uint32_t now);
  void ingestLine(const char* line, uint32_t now);
  bool injecting(uint32_t now) const;
  void shutdown();
  void drawLiveScreen(uint32_t now, uint32_t held);
  void drawListScreen(uint32_t now, uint32_t held);

  IPanel& panel_;
  IInput& input_;
  IBattery& battery_;
  ITracer& tracer_;
  ILaptop& laptop_;
  ISettings& settings_;
  IPower& power_;
  AppConfig cfg_;

  FlightModel flight_;
  FlightSim sim_;
  bool simOn_ = false;
  uint32_t injectMs_ = 0, lastSampleMs_ = 0;
  uint32_t linesOk_ = 0, linesBad_ = 0;

  ViewModel vm_;
  ViewModel::Page page_ = ViewModel::PAGE_COMPASS;
  bool wasLocal_ = true;
  bool listOpen_ = false;
  int listSel_ = 0;
  uint32_t lastDraw_ = 0;
  bool redraw_ = true;
};
