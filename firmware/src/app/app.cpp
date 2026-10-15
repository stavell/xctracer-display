#include "app/app.h"
#include <string.h>
#include "app/button_fsm.h"
#include "core/xctrc.h"
#include "view/screens.h"

App::App(IPanel& panel, IInput& input, IBattery& battery, ITracer& tracer, ILaptop& laptop,
         ISettings& settings, IPower& power, AppConfig cfg)
    : panel_(panel), input_(input), battery_(battery), tracer_(tracer), laptop_(laptop),
      settings_(settings), power_(power), cfg_(cfg) {}

void App::begin(uint32_t) {
  panel_.setRotation(cfg_.rotation);
  panel_.invalidate();
  redraw_ = true;
}

bool App::injecting(uint32_t now) const {
  return simOn_ || (injectMs_ && now - injectMs_ < cfg_.injectHoldMs);
}

void App::ingestLine(const char* line, uint32_t now) {
  Sample s;
  if (xctrcParse(line, s)) {
    linesOk_++;
    flight_.feed(s);
    lastSampleMs_ = now;
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
    case Command::PAGE:
      page_ = c.value ? ViewModel::PAGE_THERMAL : ViewModel::PAGE_COMPASS;
      redraw_ = true;
      break;
    case Command::FILTER:
      tracer_.setFilter(c.text);
      break;
    case Command::XCTRC:
      injectMs_ = now;           // replayed data takes precedence over the live tracer
      ingestLine(c.text, now);
      break;
    default:
      break;
  }
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
  vm.fresh = flight_.hasData() && lastSampleMs_ && now - lastSampleMs_ < cfg_.freshMs;
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
  vm.page = page_;
  vm.trailCount = 0;
  vm.markCount = 0;
  if (page_ == ViewModel::PAGE_THERMAL) {
    vm.trailCount = flight_.airTrail(vm.trail, ViewModel::TRAIL_MAX);
    vm.markCount = flight_.thermals(vm.marks, ViewModel::MARKS_MAX);
  }
  return vm;
}

void App::shutdown() {
  panel_.setRotation(cfg_.rotation);
  drawMessage(panel_.gfx(), "Powering off");
  panel_.invalidate();
  panel_.present();
  power_.shutdown();             // blanks the panel, stops BLE and VCOM, enters System OFF
}

void App::drawLiveScreen(uint32_t now, uint32_t held) {
  drawLive(panel_.gfx(), buildViewModel(now), held);
  panel_.present();
}

void App::drawListScreen(uint32_t now, uint32_t held) {
  ListModel lm;
  strncpy(lm.filter, tracer_.filter(), sizeof lm.filter - 1);
  lm.count = tracer_.listCount();
  if (lm.count > 8) lm.count = 8;
  for (int i = 0; i < lm.count; i++) tracer_.listItem(i, lm.items[i]);
  lm.selected = listSel_;
  drawList(panel_.gfx(), lm, buildViewModel(now), held);
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
    if (sim_.step(now, s)) { flight_.feed(s); lastSampleMs_ = now; linesOk_++; }
  }
  char line[200];
  while (tracer_.popLine(line, sizeof line)) {
    if (!injecting(now)) ingestLine(line, now);
  }
  laptop_.service(now);

  bool local = !laptop_.hostDrawing(now);
  if (local != wasLocal_) {
    wasLocal_ = local;
    if (local) { panel_.setRotation(cfg_.rotation); panel_.invalidate(); redraw_ = true; }
  }
  if (!local) return;

  uint32_t held = input_.heldMs(now);
  if (listOpen_) {
    int n = tracer_.listCount();
    if (ev == BTN_SHORT) { listSel_ = (listSel_ + 1) % (n + 1); redraw_ = true; }
    else if (ev == BTN_LONG) {
      if (listSel_ > 0) tracer_.listConnect(listSel_ - 1);
      else tracer_.listClose();
      listOpen_ = false;
      panel_.invalidate();
      redraw_ = true;
    }
    if (listSel_ > n) listSel_ = 0;
    if (listOpen_ && (redraw_ || now - lastDraw_ >= 250)) {
      lastDraw_ = now;
      redraw_ = false;
      drawListScreen(now, held);
    }
  } else {
    if (ev == BTN_SHORT) {
      page_ = page_ == ViewModel::PAGE_COMPASS ? ViewModel::PAGE_THERMAL : ViewModel::PAGE_COMPASS;
      redraw_ = true;
    }
    if (ev == BTN_LONG) {
      tracer_.listOpen();
      listOpen_ = true;
      listSel_ = 0;
      redraw_ = true;
    }
    uint32_t every = held >= ButtonFsm::ARM_MS ? 150 : cfg_.drawEveryMs;
    if (!listOpen_ && (redraw_ || now - lastDraw_ >= every)) {
      lastDraw_ = now;
      redraw_ = false;
      drawLiveScreen(now, held);
    }
  }
}
