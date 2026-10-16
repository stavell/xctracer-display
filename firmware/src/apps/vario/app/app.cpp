#include "apps/vario/app/app.h"
#include "apps/vario/app/terrain_kit.h"
#include <string.h>
#include "platform/input/button_fsm.h"
#include "apps/vario/core/home.h"
#include "platform/sensors/mount.h"
#include "apps/vario/core/xctrc.h"
#include "apps/vario/view/prof.h"
#include "apps/vario/view/screens.h"
#include <math.h>

static float angDiffDeg(float a, float b) {      // absolute shortest angle between two headings
  float d = fabsf(fmodf(a - b, 360.0f));
  return d > 180.0f ? 360.0f - d : d;
}

App::App(IPanel& panel, IBattery& battery, ITracer& tracer, ILaptop& laptop,
         BoxSettings& settings, ICompass& compass, IThermo& thermo, IPower& power, AppConfig cfg)
    : panel_(panel), battery_(battery), tracer_(tracer), laptop_(laptop),
      settings_(settings), compassHw_(compass), thermo_(thermo), power_(power), cfg_(cfg), cal_(settings) {}

void App::begin(uint32_t) {
  heading_.configure(cfg_.gpsHeadingOnKmh, cfg_.gpsHeadingOffKmh);
  MagCal cal;
  if (settings_.loadMagCal(cal)) compass_.setCalibration(cal);
  mount_ = settings_.mountIndex();
  if (!mountValid(mount_)) mount_ = 0;
  tracer_.resume();                  // no-op unless end() paused it
  if (left_) {                       // back from another app: whatever the laptop sent meanwhile is stale
    left_ = false;
    Command stale;
    while (laptop_.pop(stale)) {}
  }
  panel_.setRotation(cfg_.rotation);
  panel_.invalidate();
  redraw_ = true;
}

// Another app takes over: stop the tracer link and scan, switch the compass sensors off. The flight model
// stays as it is; its data goes stale on its own (hasData() needs a sample in the last freshMs).
void App::end() {
  if (listOpen_) { tracer_.listClose(); listOpen_ = false; }
  if (calibrating_) finishCalibration(false);
  tracer_.pause();
  if (compassOn_) { compassHw_.enable(false); compassOn_ = false; }
  compass_.reset();                  // keeps the calibration
  ahrs_.reset();
  haveGyro_ = false;
  fused_ = false;
  ev_ = BTN_NONE;
  left_ = true;
}

void App::setTerrain(TerrainKit* kit) {
  kit_ = kit;
  if (!kit_ && page_ == ViewModel::PAGE_MAP) page_ = ViewModel::PAGE_COMPASS;
  redraw_ = true;
}

bool App::hasTerrain() const { return kit_ && kit_->ok; }

// compass, thermal, centring, map (only with a pack), wind profile, home
ViewModel::Page App::nextPage(ViewModel::Page p) const {
  static const ViewModel::Page order[] = {ViewModel::PAGE_COMPASS, ViewModel::PAGE_THERMAL, ViewModel::PAGE_CENTER,
                                          ViewModel::PAGE_MAP, ViewModel::PAGE_PROFILE, ViewModel::PAGE_HOME};
  const int n = sizeof order / sizeof order[0];
  int i = 0;
  while (i < n && order[i] != p) i++;
  for (int k = 1; k <= n; k++) {
    ViewModel::Page q = order[(i + k) % n];
    if (q != ViewModel::PAGE_MAP || hasTerrain()) return q;
  }
  return ViewModel::PAGE_COMPASS;
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
    feedFlight(s, now);
    lastSampleMs_ = now;
    haveSample_ = true;
  } else {
    linesBad_++;
  }
}

void App::feedFlight(const Sample& s, uint32_t now) {
  bool newSecond = flight_.feed(s);
  map_.feed(now, s, newSecond, flight_.clockMs());
  if (clock_ && s.fix) clock_->set(s.hh * 3600 + s.mm * 60 + s.ss, now);   // GPS time of day for the home screen
  if (kit_ && s.fix) kit_->map.feedTrack(s.lat, s.lon, flight_.clockMs());   // flight clock: replays at any speed
}

void App::handle(const Command& c, uint32_t now) {
  switch (c.type) {
    case Command::SIM:
      simOn_ = c.value != 0;
      flight_.reset();
      map_.reset();
      if (kit_) kit_->map.clearTrack();
      if (simOn_) sim_.reset(now);
      break;
    case Command::TZ:
      settings_.setTzMinutes(c.value);
      break;
    case Command::ORIENT:
      if (mountValid(c.value) && c.value != mount_) {
        settings_.setMountIndex(c.value);            // the platform moves the stored offsets (device frame) along
        MagCal cal;
        if (settings_.loadMagCal(cal)) compass_.setCalibration(cal);
        mount_ = c.value;
        compass_.reset();                            // keeps the calibration
        ahrs_.reset();
      }
      break;
    case Command::CAL:
      if (c.value == 1) startCalibration();
      else if (calibrating_) finishCalibration(c.value == 2);
      break;
    case Command::PAGE:
      if (c.value >= 0 && c.value < ViewModel::PAGE_COUNT && (c.value != ViewModel::PAGE_MAP || hasTerrain())) {
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
  cal_.start();
  calibrating_ = true;
  listOpen_ = false;
  panel_.invalidate();
  redraw_ = true;
}

void App::finishCalibration(bool keep) {
  if (cal_.finish(keep)) {                                       // new offsets: use them now and re-seed the fusion
    MagCal cal;
    if (settings_.loadMagCal(cal)) compass_.setCalibration(cal);
    ahrs_.reset();
  }
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
  Vec3 a, m, g;
  bool hg = false;
  if (compassOn_ && compassHw_.read(a, m, g, hg)) {
    a = mountApply(mount_, a);                    // board frame -> device frame, all three sensors alike
    m = mountApply(mount_, m);
    compass_.update(a, m, now);                   // field strength and the no-gyro fallback
    cal_.feed(a, m, now);                         // a calibration the laptop started (ctl.py cal start)
    if (hg) {
      g = mountApply(mount_, g);
      ahrs_.update(g, a, compass_.corrected(m), now);
      gyroMs_ = now;
      haveGyro_ = true;
    }
  }
  if (haveGyro_ && now - gyroMs_ > 500) haveGyro_ = false;     // the gyro went quiet: the compass alone again
  fused_ = haveGyro_ && ahrs_.valid();
  bool magOk = fused_ || compass_.valid();
  heading_.feedMag(compassOn_ && magOk, fused_ ? ahrs_.heading() : compass_.heading());
  Heading h = heading_.current();
  if (h.src != smoothSrc_ || (h.src == HDG_MAG && smoothFused_ != fused_)) {   // GPS track: ~3 updates/s, long glide; compass: fast, deadband
    smoothSrc_ = h.src;
    smoothFused_ = fused_;
    bool gps = h.src == HDG_GPS;
    smooth_.setTau(gps ? cfg_.gpsGlideTauMs : (fused_ ? cfg_.fusedGlideTauMs : cfg_.magGlideTauMs));
    // The fused heading is smooth and steady: no deadband, and no snapping to the target, or slow turns
    // would show as steps (and still ones as jumps).
    const bool plain = gps || fused_;
    smooth_.setDeadband(plain ? 0.0f : cfg_.magDeadbandDeg, cfg_.magDeadbandHoldMs);
    smooth_.setSettle(fused_ && !gps ? 0.01f : HeadingSmoother::SETTLE_DEG);
  }
  smooth_.update(h.deg, now);   // once per tick: buildViewModel only reads it
}

// The map view as of `now`: the centre interpolated between the last two samples, one sample interval
// behind the newest. The wind is the one airTrail removes.
MapView App::mapView(uint32_t now, int trailN) {
  const Wind w = flight_.wind();
  float we = 0, wn = 0;
  if (w.valid) {
    const float ms = w.speedKmh / 3.6f, th = w.toDeg * 0.017453292f;
    we = ms * sinf(th);
    wn = ms * cosf(th);
  }
  return map_.view(now, we, wn, trailN - 1);
}

// Once per tick on the thermal page: glide the zoom toward its step, and decide whether the picture
// differs from the drawn one by more than half a pixel.
void App::updateMap(uint32_t now) {
  mapMoving_ = false;
  if (page_ != ViewModel::PAGE_THERMAL) { mapScale_ = NAN; mapDrawnLive_ = false; return; }
  if (isfinite(mapScale_) && isfinite(mapScaleTarget_)) {
    const uint32_t dt = now - mapScaleMs_;
    const float a = cfg_.mapScaleTauMs > 0 ? 1.0f - expf(-(float)(dt > 1000 ? 1000 : dt) / cfg_.mapScaleTauMs) : 1.0f;
    mapScale_ += (mapScaleTarget_ - mapScale_) * a;
    if (fabsf(mapScaleTarget_ - mapScale_) < 0.003f * mapScaleTarget_) mapScale_ = mapScaleTarget_;
  }
  mapScaleMs_ = now;
  if (!hasData(now) || !isfinite(mapScale_)) return;
  MapView mv = mapView(now, ViewModel::TRAIL_MAX);
  if (!mv.valid) return;
  if (!mapDrawnLive_) return;      // the last frame was not an interpolated one: the normal cadence draws one
  const float movedPx = hypotf(mv.ax - drawnCx_, mv.ay - drawnCy_) * mapScale_;
  float turn = 0;
  if (isfinite(mv.hdgRad) && isfinite(drawnMapHdg_)) {
    turn = fabsf(fmodf(mv.hdgRad - drawnMapHdg_, 6.2831853f));
    if (turn > 3.1415927f) turn = 6.2831853f - turn;
  }
  mapMoving_ = movedPx > 0.5f || turn > AppConfig::MAP_HALF_PIXEL_RAD || mapScale_ != drawnMapScale_;
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
  cal_.span(vm.calSpan);
  vm.fieldUt = compass_.fieldUt();
  vm.page = page_;
  bool ambientFresh = haveAmbient_ && now - ambientMs_ <= 10000;   // the sensor reports every 2 s
  vm.ambientC = ambientFresh ? ambientC_ : NAN;
  vm.humidityPct = ambientFresh ? ambientRh_ : NAN;
  vm.terrainOn = hasTerrain();
  vm.agl = NAN;
  vm.clearance = NAN;
  vm.map = nullptr;
  vm.mapMPerPx = cfg_.mapMPerPx;
  vm.mapAbout = false;
  if (vm.terrainOn) {
    // GNSS altitude (the tracer's GPS height, which is what the DEM's heights are comparable to), never the
    // barometric one: the study found the baro altitude 64-80 m off. Only with a fresh fix and a lookup that worked.
    if (vm.fresh && vm.s.fix && isfinite(vm.s.alt)) {
      float g, c;
      if (kit_->terrain.elevation(vm.s.lat, vm.s.lon, &g)) vm.agl = vm.s.alt - g;
      if (kit_->terrain.ceiling(vm.s.lat, vm.s.lon, &c)) vm.clearance = vm.s.alt - c;
    }
    vm.map = &kit_->map;
    vm.mapAbout = page_ == ViewModel::PAGE_MAP && now - mapEntryMs_ < cfg_.mapAboutMs;
  }
  vm.trailCount = 0;
  vm.markCount = 0;
  if (page_ == ViewModel::PAGE_THERMAL) {
    vm.trailCount = flight_.airTrail(vm.trail, ViewModel::TRAIL_MAX);
    vm.markCount = flight_.thermals(vm.marks, ViewModel::MARKS_MAX);
    vm.mapLive = false;
    vm.mapScale = NAN;
    if (vm.trailCount >= 2) {
      MapView mv = mapView(now, vm.trailCount);
      if (mv.valid) {
        vm.mapLive = true;
        vm.mapCx = mv.cx;
        vm.mapCy = mv.cy;
        vm.mapAx = mv.ax;
        vm.mapAy = mv.ay;
        vm.mapHdg = mv.hdgRad;
        vm.mapTailVario = vm.trail[vm.trailCount - 1].vario;
        vm.trailCount -= mv.drop;
      }
      int mw, mh;
      thermalMapSize(panel_.gfx().width(), panel_.gfx().height(), mw, mh);
      mapScaleTarget_ = thermalMapScale(vm, mw, mh);
      if (!isfinite(mapScale_)) mapScale_ = mapScaleTarget_;      // page entry: no glide
      vm.mapScale = mapScale_;
    }
  }
  vm.col = nullptr;
  if (page_ == ViewModel::PAGE_CENTER) vm.col = &flight_.column();
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

// Map page: bring the cached relief up to date before the view copies it. The render is the only slow thing
// the page does (it is why the view caches), and it happens here, in draw(), on the loop task like every draw.
void App::prepareMapPage(const ViewModel& vm, uint32_t now) {
  if (vm.page != ViewModel::PAGE_MAP || !hasTerrain() || !vm.fresh || !vm.s.fix) return;
  TerrainMap::Params p;
  p.lat = vm.s.lat;
  p.lon = vm.s.lon;
  p.headingDeg = cfg_.mapHeadingUp ? vm.heading : NAN;   // NaN (also: no heading yet) = north up
  p.mPerPx = cfg_.mapMPerPx;
  mapPageSize(panel_.gfx().width(), panel_.gfx().height(), p.w, p.h);
  kit_->map.update(kit_->terrain, kit_->vec, p, viewFont(viewGfxFont(1), TerrainMap::kCacheRotation), now);
}

void App::drawLiveScreen(Bitmap1& bm, Adafruit_GFX& g, uint32_t now, uint32_t held) {
  const ViewModel& vm = buildViewModel(now);
  prepareMapPage(vm, now);
  drawnHeading_ = vm.heading;
  mapDrawnLive_ = vm.page == ViewModel::PAGE_THERMAL && vm.mapLive;
  if (mapDrawnLive_) {
    drawnCx_ = vm.mapAx; drawnCy_ = vm.mapAy; drawnMapHdg_ = vm.mapHdg; drawnMapScale_ = vm.mapScale;
  }
  drawLive(g, vm, held, &bm);
}

void App::drawCalibrateScreen(Bitmap1& bm, Adafruit_GFX&, uint32_t, uint32_t held) {
  CalModel c;
  cal_.span(c.span);
  c.fieldUt = cal_.fieldUt();
  c.stored = cal_.stored();
  c.battPct = battery_.percent();
  c.heldMs = held;
  drawCompassCal(bm, calFonts_, c);
}

void App::drawListScreen(Bitmap1& bm, Adafruit_GFX& g, uint32_t now, uint32_t held) {
  ListModel lm;
  strncpy(lm.filter, tracer_.filter(), sizeof lm.filter - 1);
  lm.count = tracer_.listCount();
  if (lm.count > 8) lm.count = 8;
  for (int i = 0; i < lm.count; i++) tracer_.listItem(i, lm.items[i]);
  lm.selected = listSel_;
  lm.homeRow = launcher_ != nullptr;
  lm.tools = cfg_.standalone;
  drawList(g, lm, buildViewModel(now), held, &bm);
}

void App::tick(uint32_t now) {
  ButtonEvent ev = ev_;
  ev_ = BTN_NONE;
  if (ev == BTN_HOLD) { shutdown(); return; }

  Command c;
  while (laptop_.pop(c)) handle(c, now);

  battery_.update(now);
  tracer_.update(now);
  if (simOn_) {
    Sample s;
    if (sim_.step(now, s)) { feedFlight(s, now); lastSampleMs_ = now; haveSample_ = true; linesOk_++; }
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
  if (page_ != lastPage_) {                          // a page change, from the switch or the laptop
    if (page_ == ViewModel::PAGE_MAP) mapEntryMs_ = now;
    lastPage_ = page_;
  }
  updateMap(now);

  bool local = !laptop_.hostDrawing(now);
  if (local != wasLocal_) {
    wasLocal_ = local;
    if (local) { panel_.setRotation(cfg_.rotation); panel_.invalidate(); redraw_ = true; }
  }
  local_ = local;
  if (!local) return;

  tickMode_ = mode();
  if (calibrating_) {                               // turn the unit in every direction; hold = keep, press = cancel
    if (ev == BTN_LONG) finishCalibration(true);
    else if (ev == BTN_SHORT) finishCalibration(false);
    return;
  }
  if (listOpen_) {
    int n = tracer_.listCount();
    const int extra = launcher_ ? 1 : 0;             // the "Home" row after "< back" (box image only)
    if (ev == BTN_SHORT) { listSel_ = (listSel_ + 1) % (n + 1 + extra); redraw_ = true; }
    else if (ev == BTN_LONG) {
      if (listSel_ == 0) tracer_.listClose();
      else if (launcher_ && listSel_ == 1) { tracer_.listClose(); launcher_->openLauncher(); }
      else tracer_.listConnect(listSel_ - 1 - extra);
      listOpen_ = false;
      panel_.invalidate();
      redraw_ = true;
    }
    if (listSel_ > n + extra) listSel_ = 0;
  } else {
    if (ev == BTN_SHORT && !cfg_.standalone) {
      page_ = nextPage(page_);
      redraw_ = true;
    }
    if (ev == BTN_LONG) {
      tracer_.listOpen();
      listOpen_ = true;
      listSel_ = 0;
      redraw_ = true;
    }
  }
}

// The redraw cadence. True when the picture should be refreshed now: something asked for it (redraw_), or the
// screen's own cadence is due. A screen change made inside the last tick (the list opened, the calibration
// started or ended) draws on the next tick, as the app always did.
bool App::wantsFrame(uint32_t now) const {
  if (!local_ || mode() != tickMode_) return false;
  if (tickMode_ == M_CAL) return redraw_ || now - lastDraw_ >= 200;
  if (tickMode_ == M_LIST) return redraw_ || now - lastDraw_ >= 250;
  const uint32_t held = held_;
  uint32_t every = held >= ButtonFsm::ARM_MS ? 150 : cfg_.drawEveryMs;
  uint32_t anim = page_ == ViewModel::PAGE_THERMAL ? cfg_.animThermalEveryMs
                : page_ == ViewModel::PAGE_COMPASS ? cfg_.animCompassEveryMs
                : page_ == ViewModel::PAGE_HOME ? cfg_.animEveryMs : every;
  if (!hasData(now)) anim = cfg_.animCompassEveryMs;      // the standalone compass glides on every page
  // Fused compass: animate while the shown heading is more than half a pixel from the drawn one. Others: until settled.
  bool moving;
  if (smoothSrc_ == HDG_MAG && smoothFused_) {
    const float v = smooth_.value();
    moving = isfinite(v) && (!isfinite(drawnHeading_) || angDiffDeg(v, drawnHeading_) > AppConfig::DIAL_HALF_PIXEL_DEG);
  } else {
    moving = !smooth_.settled();
  }
  if (moving && anim < every) every = anim;
  if (mapMoving_ && cfg_.animMapEveryMs < every) every = cfg_.animMapEveryMs;
  return redraw_ || now - lastDraw_ >= every;
}

void App::draw(Bitmap1& bm, Adafruit_GFX& g, uint32_t now) {
  lastDraw_ = now;
  redraw_ = false;
  const uint32_t held = held_;
  if (tickMode_ == M_CAL) drawCalibrateScreen(bm, g, now, held);
  else if (tickMode_ == M_LIST) drawListScreen(bm, g, now, held);
  else drawLiveScreen(bm, g, now, held);
}
