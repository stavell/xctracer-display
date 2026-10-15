#pragma once
// Robustness tests, included by tests.cpp (uses its CHECK / NEAR / line()):
//   - fuzzing of the parser and of the whole App with hostile input (fixed seed, deterministic)
//   - the millis() wrap at 2^32 ms (49.7 days)
// The sanitizer build aborts on any undefined behaviour, so "no crash" is checked by the build itself.
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <limits>
#include "fuzzgen.h"

// A check for a bug found in the firmware that must not be fixed here: it runs, prints once if the bug is
// still there and does not count as a failure. If it ever passes, turn it into a CHECK.
static int knownBugs = 0;
#define BUG_CHECK(cond, what) do { if (!(cond)) { knownBugs++; if (knownBugs <= 40) printf("  known bug: %s  (%s:%d)\n", what, __FILE__, __LINE__); } \
                                   else if (0) {} } while (0)

static double wallMs(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------------------------
static void test_fuzz_parser() {
  puts("fuzz: xctrcParse");
  Rng r(0x1234567ull);
  const int N = 60000;
  int accepted = 0, badOutput = 0, badLatLon = 0, badBatt = 0;
  Sample s;
  for (int i = 0; i < N; i++) {
    std::string l = fuzzLine(r);
    bool ok = xctrcParse(l.c_str(), s);
    if (l.size() >= 160) CHECK(!ok);                        // longer than the parser's buffer: rejected
    if (!ok) continue;
    accepted++;
    bool good = s.hh < 24 && s.mm < 60 && s.ss <= 60;
    good = good && (!isfinite(s.alt) || (s.alt >= -1000 && s.alt <= 30000));
    good = good && (!isfinite(s.speedKmh) || (s.speedKmh >= 0 && s.speedKmh <= 400));
    good = good && (!isfinite(s.course) || (s.course >= -720 && s.course <= 720));
    good = good && (!isfinite(s.vario) || (fabsf(s.vario) <= 50));
    good = good && (!isfinite(s.pressure) || (s.pressure >= 100 && s.pressure <= 1200));
    good = good && (s.fix || (!isfinite(s.speedKmh) && !isfinite(s.course)));
    if (!good) badOutput++;
    if (s.fix && !(isfinite(s.lat) && isfinite(s.lon) && fabs(s.lat) <= 90 && fabs(s.lon) <= 180)) badLatLon++;
    if (s.battPct < -1 || s.battPct > 100) badBatt++;
  }
  CHECK(accepted > 1000);                                   // the generator does produce acceptable lines
  CHECK(badOutput == 0);
  // fixed (found by this fuzz): a non-finite or impossible position is never a fix; battery is 0..100 or -1
  CHECK(badLatLon == 0);
  CHECK(badBatt == 0);
  // exact edge cases
  CHECK(!xctrcParse("$", s)); CHECK(!xctrcParse("$*", s)); CHECK(!xctrcParse("$*00", s)); CHECK(!xctrcParse("*", s));
  CHECK(!xctrcParse("$XCTRC*00", s));
  std::string lng = "$XCTRC,"; lng.append(5000, '1'); lng += "*00";
  CHECK(!xctrcParse(lng.c_str(), s));
  std::string v = xctrcValid(12 * 3600, 90, 180, 1400, 30, 90, 1);
  CHECK(xctrcParse(v.c_str(), s) && s.fix);
  v = xctrcValid(12 * 3600, -90, -180, 1400, 30, 90, 1);
  CHECK(xctrcParse(v.c_str(), s) && s.fix);
  puts("  parser fuzz done");
}

// ---------------------------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------------------------
// The view-model rule (from screens.cpp and app.cpp). Every number that is drawn as text goes through fnum(),
// which prints "--" for a non-finite value, so these MAY be NaN: alt, vario, pressure, speed, course (always
// NaN without a fix: parser), glide (NaN unless sinking with a fix) and heading (NaN when no source). Everything
// else is drawn as geometry or used in maths and MUST be finite and in range: the position of a fix, the average
// vario, the wind (when valid), the trail and thermal marks, the calibration spans and field strength, the
// local minutes. The counts never exceed the arrays.
static int headingOutOfRange = 0;
static bool vmOk(const ViewModel& vm, const char** why) {
  auto bad = [&](const char* w) { *why = w; return false; };
  const Sample& s = vm.s;
  if (!s.fix && (isfinite(s.speedKmh) || isfinite(s.course))) return bad("speed/course without a fix");
  if (s.fix && !(isfinite(s.lat) && isfinite(s.lon) && fabs(s.lat) <= 90 && fabs(s.lon) <= 180)) return bad("fix position");
  if (!isfinite(vm.avgVario)) return bad("avgVario");
  if (vm.wind.valid && !(isfinite(vm.wind.speedKmh) && vm.wind.speedKmh >= 0 && isfinite(vm.wind.toDeg) &&
                         vm.wind.toDeg >= 0 && vm.wind.toDeg < 360.001f)) return bad("wind");
  if (vm.localMinutes < -1 || vm.localMinutes > 1439) return bad("localMinutes");
  if (isfinite(vm.heading) != (vm.headingSrc != HDG_NONE)) return bad("heading vs source");
  if (isfinite(vm.heading) && (vm.heading < 0 || vm.heading >= 360.001f)) headingOutOfRange++;
  if (vm.trailCount < 0 || vm.trailCount > ViewModel::TRAIL_MAX) return bad("trailCount");
  if (vm.markCount < 0 || vm.markCount > ViewModel::MARKS_MAX) return bad("markCount");
  for (int i = 0; i < vm.trailCount; i++)
    if (!isfinite(vm.trail[i].x) || !isfinite(vm.trail[i].y) || !isfinite(vm.trail[i].vario)) return bad("trail point");
  for (int i = 0; i < vm.markCount; i++)
    if (!isfinite(vm.marks[i].x) || !isfinite(vm.marks[i].y) || !isfinite(vm.marks[i].avgClimb) || !isfinite(vm.marks[i].gain))
      return bad("thermal mark");
  for (int i = 0; i < 3; i++) if (!isfinite(vm.calSpan[i])) return bad("calSpan");
  if (!isfinite(vm.fieldUt)) return bad("fieldUt");
  return true;
}

// A plausible-but-abusive flight line source: time mostly +1 s, sometimes back or hours ahead, position random
// walk with jumps to the poles and the date line, speeds and varios mostly sane.
struct HostileFlight {
  Rng r;
  int t = 10 * 3600;
  double lat = 43, lon = 26, hdg = 90, alt = 1500;
  explicit HostileFlight(uint64_t seed) : r(seed) {}
  std::string next() {
    int k = r.range(0, 99);
    if (k < 3) t -= r.range(1, 3600);                 // time goes backwards
    else if (k < 5) t += r.range(3600, 5 * 3600);     // jumps hours ahead
    else if (k < 6) t = 86399;                        // about to wrap midnight
    else t += 1;
    if (r.chance(2)) { static const double P[][2] = {{90, 0}, {-90, 0}, {89.9999, 179.9999}, {0.0001, 180}, {0.0001, -180}, {-89.9999, -179.9999}, {0, 0}, {43, 179.99995}};
                       const double* p = P[r.below(8)]; lat = p[0]; lon = p[1]; }
    hdg += r.range(-25, 25);
    double spd = r.chance(10) ? r.unit() * 400 : 20 + r.unit() * 30;
    double v = r.chance(5) ? (r.unit() - 0.5) * 100 : (r.unit() - 0.4) * 6;
    double dx = spd / 3.6 * sin(hdg * D2R), dy = spd / 3.6 * cos(hdg * D2R);
    lat += dy / 111320.0;
    lon += dx / (111320.0 * (fabs(lat) < 89 ? cos(lat * D2R) : 0.02));
    if (lat > 90) lat = 90; if (lat < -90) lat = -90;
    if (lon > 180) lon -= 360; if (lon < -180) lon += 360;
    alt += v;
    return xctrcValid(t, lat, lon, alt, spd, fmod(hdg + 720, 360), v, !r.chance(8));
  }
};

enum { FZ_NO_XCTRC = 1, FZ_NO_SIM = 2 };

// A random laptop packet through the real decoder, or a random Command value directly.
static bool fuzzCommand(Rng& r, int flags, Command& c) {
  if (r.chance(50)) {                                 // raw packet: random type byte and length
    uint8_t d[260];
    int len = r.chance(10) ? r.range(0, 259) : r.range(0, 12);
    for (int i = 0; i < len; i++) d[i] = (uint8_t)r.below(256);
    if (len && r.chance(70)) d[0] = (uint8_t)(r.chance(50) ? r.range(0x10, 0x15) : r.range(0, 0x20));
    if (len && d[0] >= 0x01 && d[0] <= 0x04) return false;   // drawing commands are handled in the BLE layer
    c = Command();
    if (!decodeCommand(d, (uint16_t)len, c)) return false;
  } else {
    c = Command();
    switch (r.below(6)) {
      case 0: c.type = Command::SIM; c.value = r.chance(50) ? (int)r.next() : r.range(0, 3); break;
      case 1: c.type = Command::TZ; c.value = (int16_t)r.next(); break;          // the wire format is an int16
      case 2: c.type = Command::CAL; c.value = r.chance(60) ? r.range(0, 3) : (int)r.next(); break;
      case 3: c.type = Command::PAGE; c.value = r.chance(50) ? (int)r.next() : r.range(0, 1); break;
      case 4: c.type = Command::FILTER; { int n = r.range(0, 199); for (int i = 0; i < n; i++) c.text[i] = (char)r.range(1, 255); } break;
      default: c.type = Command::XCTRC; { std::string l = r.chance(50) ? fuzzLine(r) : xctrcValid(r.range(0, 86399), 43, 26, 1400, 30, 90, 1);
                                          strncpy(c.text, l.c_str(), sizeof c.text - 1); } break;
    }
  }
  if ((flags & FZ_NO_XCTRC) && c.type == Command::XCTRC) return false;
  if ((flags & FZ_NO_SIM) && c.type == Command::SIM) return false;
  return true;
}

struct FuzzStats { double worstTickMs = 0; uint32_t delivered = 0, maxOk = 0; int vmFail = 0; };

// One phase: `ticks` ticks of 10 ms. Lines come from the tracer (tracerLines), from inject commands
// (injectLines), or both; extra laptop commands as flagged.
static FuzzStats fuzzAppPhase(Rig& r, App& app, uint32_t& now, Rng& rng, int ticks, bool tracerLines, bool injectLines,
                              int cmdFlags, bool exactCount, uint32_t& delivered, HostileFlight& hf) {
  FuzzStats st;
  uint32_t lastOkBad = app.linesOk() + app.linesBad();
  for (int i = 0; i < ticks; i++, now += 10) {
    int nl = rng.chance(35) ? rng.range(1, 3) : 0;
    for (int k = 0; k < nl; k++) {
      std::string l = rng.chance(55) ? hf.next() : fuzzLine(rng);
      if (l.size() > 199) l.resize(199);                // the BLE layer / mock queue never delivers more than a buffer
      if (tracerLines && (!injectLines || rng.chance(50))) { r.tracer.lines.push_back(l); delivered++; }
      else if (injectLines) {
        Command c; c.type = Command::XCTRC; strncpy(c.text, l.c_str(), sizeof c.text - 1); r.laptop.cmds.push_back(c); delivered++;
      }
    }
    if (rng.chance(4)) { Command c; if (fuzzCommand(rng, cmdFlags, c)) { if (c.type == Command::XCTRC) delivered++; r.laptop.cmds.push_back(c); } }
    if (rng.chance(1)) r.input.press(now, (uint32_t)rng.range(20, 900));    // short and long presses (never 3 s: no power-off)
    auto t0 = std::chrono::steady_clock::now();
    app.tick(now);
    double ms = wallMs(t0);
    if (ms > st.worstTickMs) st.worstTickMs = ms;
    uint32_t total = app.linesOk() + app.linesBad();
    CHECK(total >= lastOkBad);
    lastOkBad = total;
    if (exactCount && total != delivered) { CHECK(total == delivered); exactCount = false; }   // report once
    const char* why = "";
    if (i % 7 == 0 && !vmOk(app.buildViewModel(now), &why)) {
      if (st.vmFail++ < 5) printf("  view model violation: %s (tick %d)\n", why, i);
    }
  }
  return st;
}

static void test_fuzz_app() {
  puts("fuzz: whole App");
  Rig r;
  r.compass.psi = 40; r.compass.pitch = 5; r.compass.roll = -3;
  App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.begin(0);
  uint32_t now = 1000, delivered = 0;
  Rng rng(0xABCDEF12345ull);
  HostileFlight hf(77);
  double worst = 0;
  int vmFail = 0;
  // A: tracer lines plus laptop commands other than XCTRC / SIM: every delivered line is counted exactly once
  FuzzStats a = fuzzAppPhase(r, app, now, rng, 30000, true, false, FZ_NO_XCTRC | FZ_NO_SIM, true, delivered, hf);
  CHECK(app.linesOk() + app.linesBad() == delivered);
  CHECK(app.linesOk() > 100);
  // B: injected lines only (laptop XCTRC commands: always counted, they silence the tracer)
  FuzzStats b = fuzzAppPhase(r, app, now, rng, 20000, false, true, FZ_NO_SIM, true, delivered, hf);
  CHECK(app.linesOk() + app.linesBad() == delivered);
  // C: everything, including the simulator and mixed sources. Tracer lines are silenced while injecting, and the
  // simulator counts its own samples, so only monotonic counters are checked here.
  uint32_t d2 = 0;
  FuzzStats c = fuzzAppPhase(r, app, now, rng, 30000, true, true, 0, false, d2, hf);
  for (const FuzzStats* s : {&a, &b, &c}) { if (s->worstTickMs > worst) worst = s->worstTickMs; vmFail += s->vmFail; }
  CHECK(vmFail == 0);
  CHECK(headingOutOfRange == 0);                            // fixed: courses are normalised to 0..360
  CHECK(worst < 50.0);
  printf("  %u + %u lines, %d view model violations, worst tick %.2f ms\n", delivered, d2, vmFail, worst);
  // after the abuse the app still works: a clean flight is accepted and shown
  r.laptop.cmds.clear(); r.tracer.lines.clear();
  { Command sc; sc.type = Command::SIM; sc.value = 0; r.laptop.cmds.push_back(sc); }
  for (int i = 0; i < 400; i++, now += 10) app.tick(now);       // let injection hold / sim state settle
  uint32_t ok = app.linesOk();
  for (int i = 0; i < 20; i++) {
    r.tracer.lines.push_back(xctrcValid(36000 + i, 43.25 + i * 1e-4, 26.9, 1400, 34, 90, 1.5));
    for (int k = 0; k < 40; k++, now += 10) app.tick(now);
  }
  CHECK(app.linesOk() >= ok + 19);
  const ViewModel& vm = app.buildViewModel(now);
  CHECK(vm.fresh || app.simOn());
}

// Run fn in a child process (a sanitizer abort there must not end the test run); true if it exited cleanly.
template <class F> static bool survives(F fn) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) { if (!freopen("/dev/null", "w", stderr)) _exit(3); fn(); _exit(0); }
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static void test_known_bug_nonfinite_position() {
  puts("impossible positions on the thermal page (child processes)");
  const char* bad[] = {"inf", "1e30", "-inf", "1e300"};
  for (const char* v : bad) {
    bool ok = survives([&] {
      Rig r;
      App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
      app.begin(0);
      uint32_t now = 0;
      { Command pc; pc.type = Command::PAGE; pc.value = 1; r.laptop.cmds.push_back(pc); }
      for (int i = 0; i < 70; i++) {
        std::string body = xctrcBody("12", "0", std::to_string(i), (i < 20 || i >= 25) ? "43.25" : v, (i < 20 || i >= 25) ? "26.9" : "nan", "1400", "34", "90", "1", "967", "85");
        r.tracer.lines.push_back(xctrcWithChecksum(body));
        for (int k = 0; k < 40; k++, now += 10) app.tick(now);
      }
    });
    char what[160];
    snprintf(what, sizeof what, "a line with lat %s, lon nan must not take the app down", v);
    if (!ok) printf("  FAIL %s\n", what);
    CHECK(ok);
  }
}

// ---------------------------------------------------------------------------------------------
// millis() wrap
// ---------------------------------------------------------------------------------------------
// The laptop link as LaptopBle (hw/laptop_ble.cpp) implements it: a draw command stamps millis(); the laptop
// owns the screen for `grace` ms after the last stamp; 0 means "never drew". Same expressions, mock clock.
class StampLaptop : public MockLaptop {
 public:
  explicit StampLaptop(uint32_t graceMs = 10000) : grace_(graceMs) {}
  void draw(uint32_t now) { last_ = now; }
  bool hostDrawing(uint32_t now) override { uint32_t t = last_; return t && now - t < grace_; }
 private:
  uint32_t last_ = 0, grace_;
};

struct CountPanel : MockPanel {
  void clear() override { clears++; MockPanel::clear(); }
  int clears = 0;
};

// What the app does over a fixed 100 s script, as one value per 10 ms tick, so two runs can be compared
// tick by tick. `base` is the clock value at script time 0. The script is in script milliseconds.
struct WrapTrace {
  std::vector<uint64_t> v;
  std::vector<uint32_t> drawTimes;      // script ms of every redraw
};

static WrapTrace runWrapScript(uint32_t base) {
  CountPanel panel; MockInput input; MockBattery battery; MockTracer tracer; StampLaptop laptop; MockSettings settings;
  MockCompass compass; MockThermo thermo; MockPower power;
  App app(panel, input, battery, tracer, laptop, settings, compass, thermo, power);
  app.begin(base);
  // buttons: cancelled hold (1.6 s), short, long (opens the device list), long (selects "< back")
  input.press(base + 20000, 1600);
  input.press(base + 24000, 100);
  input.press(base + 26000, 900);
  input.press(base + 29000, 900);
  input.press(base + 45000, 100);     // short
  input.press(base + 57000, 1600);    // cancelled hold ...
  input.press(base + 59950, 100);     // short press that spans the wrap when it falls at 60 s
  input.press(base + 61000, 900);     // long: list opens
  input.press(base + 63000, 900);     // long: back
  WrapTrace tr;
  int lastClears = 0;
  for (uint32_t t = 0; t < 100000; t += 10) {
    uint32_t now = base + t;
    // live tracer lines at 3 Hz with gaps; valid with a fix, time of day advances
    bool tracerGap = (t >= 34000 && t < 38000) || (t >= 58000 && t < 64000) || (t >= 80000 && t < 90000);
    if (t % 330 == 0 && !tracerGap) tracer.lines.push_back(xctrcValid(36000 + t / 1000, 43.25 + t * 1e-8, 26.9, 1400 + t / 100, 34, 90, 1.5));
    // injected lines (as the laptop's replay does) in two bursts: the tracer is silenced for 3 s after each
    if ((t >= 8000 && t < 10000 && t % 1000 == 0) || (t >= 70000 && t < 73000 && t % 1000 == 0)) laptop.inject(xctrcValid(40000 + t / 1000, 43.3, 26.8, 1300, 40, 80, -1));
    // laptop drawing: stamps at 12, 13, 14 s (grace to 24 s) and at 55, 56, 57 s (grace to 67 s, straddles the wrap)
    if (t == 12000 || t == 13000 || t == 14000 || t == 55000 || t == 56000 || t == 57000) laptop.draw(now);
    // simulator on at 40 s, off at 50 s; on at 75 s, off at 95 s
    if (t == 40000 || t == 75000) laptop.sim(true);
    if (t == 50000 || t == 95000) laptop.sim(false);
    app.tick(now);
    const ViewModel& vm = app.buildViewModel(now);
    uint64_t h = vm.fresh | (vm.sim << 1) | ((uint64_t)vm.page << 2) | ((uint64_t)app.listOpen() << 3) | ((uint64_t)app.calibrating() << 4) |
                 ((uint64_t)laptop.hostDrawing(now) << 5) | ((uint64_t)input.heldMs(now) << 8) | ((uint64_t)app.linesOk() << 32) | ((uint64_t)app.linesBad() << 52);
    tr.v.push_back(h);
    if (panel.clears != lastClears) { lastClears = panel.clears; tr.drawTimes.push_back(t); }
  }
  return tr;
}

static void test_time_wrap() {
  puts("millis() wrap");
  WrapTrace ref = runWrapScript(1000000);                 // no wrap anywhere
  // sanity: the reference script exercises what it should
  CHECK(ref.drawTimes.size() > 100);
  bool sawFresh = false, sawStale = false, sawHost = false, sawList = false, sawSim = false, sawPage = false;
  for (uint64_t h : ref.v) { sawFresh |= h & 1; sawStale |= !(h & 1); sawSim |= (h >> 1) & 1; sawPage |= (h >> 2) & 1; sawList |= (h >> 3) & 1; sawHost |= (h >> 5) & 1; }
  CHECK(sawFresh && sawStale && sawSim && sawPage && sawList && sawHost);
  // redraw cadence away from the wrap: 333 ms while idle, never two draws closer than 100 ms. The
  // heading glides (50 ms redraws) after a source switch, also when data returns after a gap: about 3 s of
  // them in this script (63 draws), so the bound is a storm detector, not a cadence check.
  { int tooClose = 0; for (size_t i = 1; i < ref.drawTimes.size(); i++) if ((ref.v[ref.drawTimes[i] / 10] & 1) && (ref.v[ref.drawTimes[i - 1] / 10] & 1) && ref.drawTimes[i] - ref.drawTimes[i - 1] < 100) tooClose++; CHECK(tooClose < 100); }

  // the same script with the wrap at script time w (w + 5 ms, so a tick never lands exactly on 0: see the
  // zero-sentinel checks below)
  int mismatches = 0, runs = 0;
  for (uint32_t w = 500; w <= 99500; w += 470) {
    uint32_t base = (uint32_t)(0x100000000ull - (w + 5));
    WrapTrace t = runWrapScript(base);
    runs++;
    bool same = t.v.size() == ref.v.size() && t.drawTimes == ref.drawTimes;
    size_t first = 0;
    for (size_t i = 0; same && i < t.v.size(); i++) if (t.v[i] != ref.v[i]) { same = false; first = i; }
    if (!same) { mismatches++; if (mismatches <= 5) printf("  wrap at script %u ms: differs (first state difference at tick %zu)\n", w, first); }
  }
  CHECK(mismatches == 0);
  printf("  %d wrap positions compared with the no-wrap run\n", runs);

  // Directly, with the clock 60 s before 2^32 and data flowing across it.
  {
    Rig r; App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    uint32_t now = 0xFFFFFFFFu - 60000 + 1;
    app.begin(now);
    int freshWrong = 0;
    for (int i = 0; i < 12000; i++, now += 10) {            // 120 s, wraps at 60 s
      if (i % 33 == 0) r.tracer.lines.push_back(xctrcValid(36000 + i / 100, 43.25, 26.9, 1400, 34, 90, 1.5));
      app.tick(now);
      if (!app.buildViewModel(now).fresh && i > 40) freshWrong++;
    }
    CHECK(freshWrong == 0);
    CHECK(app.linesOk() > 300 && app.linesBad() == 0);
  }

  // Exactly 0 is "never" for three timestamps (injectMs_, lastSampleMs_ in app.cpp; lastDraw_ in laptop_ble.cpp).
  // One tick in 49.7 days (one in ten for a 10 ms loop at that instant) hits it.
  {
    Rig r; App app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
    uint32_t now = 0xFFFFFFFFu - 500 + 1;
    app.begin(now);
    for (; now != 0; now += 10) app.tick(now);              // reach exactly 0 (500 is a multiple of 10 away)
    r.laptop.inject(xctrcValid(36000, 43.25, 26.9, 1400, 34, 90, 1.5));
    app.tick(now);                                          // processed at now == 0
    uint32_t ok = app.linesOk();
    for (now += 10; now < 200; now += 10) app.tick(now);
    r.tracer.lines.push_back(xctrcValid(36001, 43.25, 26.9, 1400, 34, 90, 1.5));
    app.tick(now);
    // fixed (found by this test): 0 used to mean "never" for the inject and sample stamps
    CHECK(app.linesOk() == ok);
    bool freshAfterZeroSample;
    {
      Rig r2; App a2(r2.panel, r2.input, r2.battery, r2.tracer, r2.laptop, r2.settings, r2.compass, r2.thermo, r2.power);
      uint32_t n2 = 0xFFFFFFFFu - 500 + 1;
      a2.begin(n2);
      for (; n2 != 0; n2 += 10) a2.tick(n2);
      r2.tracer.lines.push_back(xctrcValid(36000, 43.25, 26.9, 1400, 34, 90, 1.5));
      a2.tick(n2);
      freshAfterZeroSample = a2.buildViewModel(n2).fresh;
    }
    CHECK(freshAfterZeroSample);
  }
  // the pure state machines directly: wrap in the middle of a press
  {
    ButtonFsm f;
    uint32_t n = 0xFFFFFFFFu - 300;
    int shorts = 0, longs = 0, holds = 0;
    for (int i = 0; i < 100; i++, n += 10) { auto e = f.feed(i >= 10 && i < 40, n); shorts += e == BTN_SHORT; longs += e == BTN_LONG; holds += e == BTN_HOLD; }
    CHECK(shorts == 1 && longs == 0 && holds == 0);
    n = 0xFFFFFFFFu - 400;
    for (int i = 0; i < 200; i++, n += 10) { auto e = f.feed(i >= 5 && i < 90, n); shorts += e == BTN_SHORT; longs += e == BTN_LONG; holds += e == BTN_HOLD; }
    CHECK(longs == 1);
    n = 0xFFFFFFFFu - 1000;
    for (int i = 0; i < 500; i++, n += 10) { auto e = f.feed(i >= 5, n); holds += e == BTN_HOLD; }
    CHECK(holds == 1);
  }
  { FlightSim s; Sample o; uint32_t n = 0xFFFFFFFFu - 5000; s.reset(n); int got = 0; for (int i = 0; i < 2000; i++, n += 10) got += s.step(n, o); CHECK(got > 55 && got < 65); }
}
