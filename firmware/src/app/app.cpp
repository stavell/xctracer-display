#include "app/app.h"
#include <string.h>
#include "app/button_fsm.h"
#include "core/home.h"
#include "core/xctrc.h"
#include "view/prof.h"
#include "view/screens.h"

App::App(IPanel& panel, IInput& input, IBattery& battery, ITracer& tracer, ILaptop& laptop,
         ISettings& settings, ICompass& compass, IThermo& thermo, IPower& power, AppConfig cfg)
    : panel_(panel), input_(input), battery_(battery), tracer_(tracer), laptop_(laptop),
      settings_(settings), compassHw_(compass), thermo_(thermo), power_(power), cfg_(cfg) {}

void App::begin(uint32_t) {
  heading_.configure(cfg_.gpsHeadingOnKmh, cfg_.gpsHeadingOffKmh);
  MagCal cal;
  if (settings_.loadMagCal(cal)) compass_.setCalibration(cal);
  panel_.setRotation(cfg_.rotation);
  panel_.invalidate();
  redraw_ = true;
}

bool App::injecting(uint32_t now) const {
  return simOn_ || (haveInject_ && now - injectMs_ < cfg_.injectHoldMs);
}

void App::ingestLine(const char* line, uint32_t now) {
  strncpy(lastRaw_, line, sizeof lastRaw_ - 1);
  lastRaw_[sizeof lastRaw_ - 1] = 0;
  Sample s;
  if (xctrcParse(line, s)) {
    linesOk_++;
    flight_.feed(s);
    lastSampleMs_ = now;
    haveSample_ = true;
  } else {
    linesBad_++;
  }
}

void App::handle(const Command& c, uint32_t now) {
  switch (c.type) {
    case Command::SIM:
      simOn_ = c.value != 0;
      flight_.reset();
      if (simOn_) sim_.reset(now);
      break;
    case Command::TZ:
      settings_.setTzMinutes(c.value);
      break;
    case Command::CAL:
      if (c.value == 1) startCalibration();
      else if (calibrating_) finishCalibration(c.value == 2);
      break;
    case Command::PAGE:
      if (c.value >= 0 && c.value < ViewModel::PAGE_COUNT) {
        page_ = (ViewModel::Page)c.value;
        redraw_ = true;
      }
      break;
    case Command::FILTER:
      tracer_.setFilter(c.text);
      break;
    case Command::XCTRC:
      injectMs_ = now;           // replayed data takes precedence over the live tracer
      haveInject_ = true;
      ingestLine(c.text, now);
      break;
#ifdef DIAGNOSTICS
    case Command::DIAG_HANG:
      for (;;) {}
    case Command::DIAG_OFF:
      shutdown();
      break;
#endif
    default:
      break;
  }
}

void App::startCalibration() {
  compass_.startCalibration();
  calibrating_ = true;
  listOpen_ = false;
  panel_.invalidate();
  redraw_ = true;
}

void App::finishCalibration(bool keep) {
  bool saved = compass_.finishCalibration(keep);
  if (keep && saved) settings_.saveMagCal(compass_.calibration());
  calibrating_ = false;
  panel_.invalidate();
  redraw_ = true;
}

// Feed the heading selector from both sources. The magnetometer always runs (a fraction of a mA), so it
// has a settled heading the moment the GPS track drops out and its reading is always in the status line.
bool App::hasData(uint32_t now) const {
  return flight_.hasData() && haveSample_ && now - lastSampleMs_ < cfg_.freshMs;
}

void App::updateHeading(uint32_t now) {
  const Sample& s = flight_.last();
  bool fresh = hasData(now);
  heading_.feedGps(fresh && s.fix, s.speedKmh, s.course);

  if (!compassOn_) {
    compassOn_ = true;
    compassHw_.enable(true);
  }
  Vec3 a, m;
  if (compassOn_ && compassHw_.read(a, m)) compass_.update(a, m);
  heading_.feedMag(compassOn_ && compass_.valid(), compass_.heading());
  smooth_.update(heading_.current().deg, now);   // once per tick: buildViewModel only reads it
}

const ViewModel& App::buildViewModel(uint32_t now) {
  ViewModel& vm = vm_;
  vm.link = tracer_.link();
  vm.hasSaved = tracer_.hasSaved();
  strncpy(vm.device, tracer_.deviceName(), sizeof vm.device - 1);
  vm.device[sizeof vm.device - 1] = 0;
  strncpy(vm.filter, tracer_.filter(), sizeof vm.filter - 1);
  vm.filter[sizeof vm.filter - 1] = 0;
  vm.linesOk = linesOk_;
  vm.linesBad = linesBad_;
  vm.fresh = hasData(now);
  vm.sim = simOn_;
  vm.s = flight_.last();
  vm.avgVario = flight_.avgVario();
  vm.glide = flight_.glide();
  vm.wind = flight_.wind();
  vm.localMinutes = -1;
  if (flight_.hasData()) {
    int m = vm.s.hh * 60 + vm.s.mm + settings_.tzMinutes();
    vm.localMinutes = ((m % 1440) + 1440) % 1440;
  }
  vm.localBattPct = battery_.percent();
  Heading h = heading_.current();
  vm.heading = smooth_.value();
  vm.headingSrc = h.src;
  vm.calibrating = calibrating_;
  vm.calValid = compass_.calibration().valid;
  compass_.calibrationSpan(vm.calSpan);
  vm.fieldUt = compass_.fieldUt();
  vm.page = page_;
  bool ambientFresh = haveAmbient_ && now - ambientMs_ <= 10000;   // the sensor reports every 2 s
  vm.ambientC = ambientFresh ? ambientC_ : NAN;
  vm.humidityPct = ambientFresh ? ambientRh_ : NAN;
  vm.trailCount = 0;
  vm.markCount = 0;
  if (page_ == ViewModel::PAGE_THERMAL) {
    vm.trailCount = flight_.airTrail(vm.trail, ViewModel::TRAIL_MAX);
    vm.markCount = flight_.thermals(vm.marks, ViewModel::MARKS_MAX);
  }
  vm.profileCount = 0;
  vm.homeValid = false;
  if (page_ == ViewModel::PAGE_PROFILE) {
    vm.profileCount = flight_.profile().query(vm.profile, ViewModel::PROFILE_MAX, flight_.clockMs());
    vm.profileAlt = vm.s.alt;
  }
  if (page_ == ViewModel::PAGE_HOME) {
    const HomePoint& hp = flight_.home();
    HomeInfo hi = computeHome(vm.s.lat, vm.s.lon, vm.s.alt, vm.heading, hp, vm.wind);
    if (!vm.s.fix) hi = HomeInfo();             // no position now: distance and bearing would be stale
    vm.homeValid = hp.valid;                    // distance etc. stay NaN while there is no fix
    vm.homeProvisional = hp.provisional;
    vm.homeDist = hi.distM;
    vm.homeRelBearing = hi.relBearingDeg;
    vm.homeBearing = hi.bearingDeg;
    vm.homeGlideReq = hi.glideReq;
    vm.homeArrival = hi.arrivalM;
    vm.homeReachable = hi.reachable;
  }
  return vm;
}

void App::shutdown() {
  panel_.setRotation(cfg_.rotation);
  panel_.clear();
  Bitmap1 bm = panel_.bitmap();
  drawMessage(panel_.gfx(), "Powering off", nullptr, &bm);
  panel_.invalidate();
  panel_.present();
  power_.shutdown();             // blanks the panel, stops BLE and VCOM, enters System OFF
}

void App::drawLiveScreen(uint32_t now, uint32_t held) {
  PROF_BEGIN(P_CLEAR);
  panel_.clear();
  PROF_END(P_CLEAR);
  Bitmap1 bm = panel_.bitmap();
  drawLive(panel_.gfx(), buildViewModel(now), held, &bm);
  PROF_BEGIN(P_PRESENT);
  panel_.present();
  PROF_END(P_PRESENT);
}

void App::drawCalibrateScreen(uint32_t now, uint32_t held) {
  panel_.clear();
  Bitmap1 bm = panel_.bitmap();
  drawCalibrate(panel_.gfx(), buildViewModel(now), held, &bm);
  panel_.present();
}

void App::drawListScreen(uint32_t now, uint32_t held) {
  ListModel lm;
  strncpy(lm.filter, tracer_.filter(), sizeof lm.filter - 1);
  lm.count = tracer_.listCount();
  if (lm.count > 8) lm.count = 8;
  for (int i = 0; i < lm.count; i++) tracer_.listItem(i, lm.items[i]);
  lm.selected = listSel_;
  panel_.clear();
  Bitmap1 bm = panel_.bitmap();
  drawList(panel_.gfx(), lm, buildViewModel(now), held, &bm);
  panel_.present();
}

void App::tick(uint32_t now) {
  ButtonEvent ev = input_.poll(now);
  if (ev == BTN_HOLD) { shutdown(); return; }

  Command c;
  while (laptop_.pop(c)) handle(c, now);

  battery_.update(now);
  tracer_.update(now);
  if (simOn_) {
    Sample s;
    if (sim_.step(now, s)) { flight_.feed(s); lastSampleMs_ = now; haveSample_ = true; linesOk_++; }
  }
  char line[200];
  while (tracer_.popLine(line, sizeof line)) {
    if (!injecting(now)) ingestLine(line, now);
  }
  float tc, rh;
  if (thermo_.read(tc, rh)) {
    flight_.feedTemperature(tc);
    ambientC_ = tc; ambientRh_ = rh; ambientMs_ = now; haveAmbient_ = true;
  }
  laptop_.service(now);
  updateHeading(now);

  bool local = !laptop_.hostDrawing(now);
  if (local != wasLocal_) {
    wasLocal_ = local;
    if (local) { panel_.setRotation(cfg_.rotation); panel_.invalidate(); redraw_ = true; }
  }
  if (!local) return;

  uint32_t held = input_.heldMs(now);
  if (calibrating_) {                               // turn the unit in every direction; hold = keep, press = cancel
    if (ev == BTN_LONG) finishCalibration(true);
    else if (ev == BTN_SHORT) finishCalibration(false);
    if (calibrating_ && (redraw_ || now - lastDraw_ >= 200)) {
      lastDraw_ = now;
      redraw_ = false;
      drawCalibrateScreen(now, held);
    }
    return;
  }
  if (listOpen_) {
    int n = tracer_.listCount();
    if (ev == BTN_SHORT) { listSel_ = (listSel_ + 1) % (n + 2); redraw_ = true; }
    else if (ev == BTN_LONG) {
      if (listSel_ == 0) tracer_.listClose();
      else if (listSel_ == 1) { tracer_.listClose(); startCalibration(); }
      else tracer_.listConnect(listSel_ - 2);
      listOpen_ = false;
      panel_.invalidate();
      redraw_ = true;
    }
    if (listSel_ > n + 1) listSel_ = 0;
    if (listOpen_ && (redraw_ || now - lastDraw_ >= 250)) {
      lastDraw_ = now;
      redraw_ = false;
      drawListScreen(now, held);
    }
  } else {
    if (ev == BTN_SHORT) {
      page_ = (ViewModel::Page)((page_ + 1) % ViewModel::PAGE_COUNT);
      redraw_ = true;
    }
    if (ev == BTN_LONG) {
      tracer_.listOpen();
      listOpen_ = true;
      listSel_ = 0;
      redraw_ = true;
    }
    uint32_t every = held >= ButtonFsm::ARM_MS ? 150 : cfg_.drawEveryMs;
    uint32_t anim = page_ == ViewModel::PAGE_THERMAL ? cfg_.animThermalEveryMs
                  : (page_ == ViewModel::PAGE_COMPASS || page_ == ViewModel::PAGE_HOME) ? cfg_.animEveryMs : every;
    if (!hasData(now)) anim = cfg_.animEveryMs;      // the standalone compass glides on every page
    if (!smooth_.settled() && anim < every) every = anim;
    if (!listOpen_ && (redraw_ || now - lastDraw_ >= every)) {
      lastDraw_ = now;
      redraw_ = false;
      drawLiveScreen(now, held);
    }
  }
}
