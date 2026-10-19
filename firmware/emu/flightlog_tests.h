// Tests of the flight log (record codec, flash log, serial shell, flash view), the yaw rate, the dead reckoner and the offline
// evaluator, on synthetic circles. Included by tests.cpp (uses its CHECK / NEAR / Rig / Driven).
#pragma once
#include <random>
#include <string>
#include "flash_mock.h"
#include "flightlog_io.h"
#include "platform/sensors/yawrate.h"
#include "platform/storage/flash_view.h"
#include "platform/storage/log_shell.h"
#include "platform/terrain/crc32.h"

namespace flightlog_tests {
using namespace storage;
using flightlog::FlightLogger;

struct Region {
  MemFlash fl;
  FlashLog log;
  uint32_t base;
  Region() : base(fl.capacity() - kLogBytes) {}
  void format() {
    log.attach(&fl, base);
    CHECK(log.state() == FlashLog::UNFORMATTED);
    CHECK(log.startErase());
    for (int i = 0; i < 100000 && log.erasing(); i++) log.service(i);
    CHECK(log.state() == FlashLog::READY);
  }
  void drain(uint32_t t) { for (int i = 0; i < 4; i++) log.service(t + i); }
};

static Sample mkSample(int sec, double lat, double lon, float spd, float crs, float vario) {
  Sample s;
  s.fix = true; s.lat = lat; s.lon = lon; s.speedKmh = spd; s.course = crs; s.vario = vario;
  s.hh = 9; s.mm = 30; s.ss = (uint8_t)sec;
  return s;
}

static void testFlashLog() {
  puts("flight log: format, record, reattach, full");
  Region R;
  R.log.attach(&R.fl, R.base);
  CHECK(R.log.state() == FlashLog::UNFORMATTED);
  uint8_t rec[10] = {'S', 1, 2, 3, 4, 5, 6, 7, 8, 9};
  CHECK(!R.log.record(rec, sizeof rec));                          // unformatted: nothing is recorded
  R.fl.mem[R.base + 5000] = 0x12;                                 // stale data in the tail does not make a log
  R.format();
  CHECK(R.fl.blockErases == 4);
  CHECK(R.log.capacityBytes() == kLogBytes - kLogPage);
  FlightLogger lg(R.log);
  lg.sessionStart(1000);
  for (int i = 0; i < 20; i++) lg.line(1000 + 333 * i, mkSample(i / 3, 42.5 + i * 1e-5, 23.3 - i * 1e-5, 33.3f + i, 90.0f + i, 1.5f - 0.1f * i));
  lg.sessionEnd(9000);
  R.drain(20000);
  CHECK(R.log.usedBytes() == 2 * kLogPage);                       // 7 + 20 * 23 bytes: two pages
  std::vector<uint8_t> raw(R.log.usedBytes());
  CHECK(R.log.read(0, raw.data(), (uint32_t)raw.size()));
  auto L = flightlog_io::decode(raw.data(), raw.size());
  CHECK(L.sessions.size() == 1 && L.sessions[0] == 1000);
  CHECK(L.lines.size() == 20);
  if (L.lines.size() == 20) {
    NEAR(L.lines[7].lat, 42.5 + 7e-5, 2e-7);
    NEAR(L.lines[7].lon, 23.3 - 7e-5, 2e-7);
    NEAR(L.lines[7].speedKmh, 40.3f, 0.06);
    NEAR(L.lines[7].courseDeg, 97.0f, 0.06);
    NEAR(L.lines[7].vario, 0.8f, 0.006);
    CHECK(L.lines[19].rxMs == 1000 + 333 * 19 && L.lines[19].ss == 6);
  }
  CHECK(!R.fl.programmedOverNonErased);
  // a reboot: the end of the log is found again, and recording continues after it
  FlashLog again;
  again.attach(&R.fl, R.base);
  CHECK(again.state() == FlashLog::READY && again.usedBytes() == 2 * kLogPage);
  // fill it up: stops when full, never writes outside the region
  FlightLogger big(again);
  uint32_t t = 0;
  for (int i = 0; i < 40000 && again.state() == FlashLog::READY; i++, t += 333) {
    big.line(t, mkSample(i % 60, 42.5, 23.3, 30, 100, 0));
    again.service(t);
  }
  again.service(t + 1); again.service(t + 2);
  CHECK(again.state() == FlashLog::FULL);
  CHECK(again.usedBytes() == again.capacityBytes());
  CHECK(!again.record(rec, sizeof rec));
  for (uint32_t a = 0; a < R.base; a += 4096) if (R.fl.mem[a] != 0xFF) { CHECK(false); break; }   // below the region untouched
  CHECK(!R.fl.programmedOverNonErased);
  FlashLog third;
  third.attach(&R.fl, R.base);
  CHECK(third.state() == FlashLog::FULL);
}

static void testYawCodec() {
  puts("flight log: yaw rate blocks, gaps, quantisation");
  Region R;
  R.format();
  FlightLogger lg(R.log);
  lg.sessionStart(0);
  std::mt19937 rng(7);
  std::normal_distribution<double> nd(0, 1);
  std::vector<std::pair<uint32_t, float>> truth;      // value at the end of each 40 ms window
  double ms = 5;
  for (int i = 0; i < 4000; i++) {
    ms += (i & 1) ? 12 : 13;                          // 80 Hz
    if (ms > 20000 && ms < 21000) continue;           // a gap of one second
    const float v = 12.0f * (float)sin(ms / 1500.0) + (float)nd(rng) * 0.5f;
    lg.yaw((uint32_t)ms, v);
    R.log.service((uint32_t)ms);
  }
  lg.sessionEnd(40000);
  R.drain(50000);
  std::vector<uint8_t> raw(R.log.usedBytes());
  R.log.read(0, raw.data(), (uint32_t)raw.size());
  auto L = flightlog_io::decode(raw.data(), raw.size());
  printf("  yaw samples %zu\n", L.yaw.size());
  CHECK(L.yaw.size() > 1100 && L.yaw.size() < 1300);   // 25 Hz for about 49 s minus the gap
  // the grid is exact, the gap shows, and the values follow the smooth part within the quantisation and the noise
  int gaps = 0;
  double maxErr = 0;
  for (size_t i = 1; i < L.yaw.size(); i++) {
    const uint32_t d = L.yaw[i].ms - L.yaw[i - 1].ms;
    if (d != 40) { gaps++; CHECK(d > 900); }
    const double smooth = 12.0 * sin((L.yaw[i].ms - 20.0) / 1500.0);
    maxErr = fmax(maxErr, fabs(L.yaw[i].dps - smooth));
  }
  CHECK(gaps == 1);
  CHECK(maxErr < 3.0);                                 // noise of 0.5 deg/s rms (averaged over 3 reads) plus the step limit
  // the bytes: well under the budget (3 lines a second would add 69 B/s)
  CHECK(raw.size() < 49 * 30 * 1.6);
}

static void testShellAndView() {
  puts("flight log: serial shell, flash view");
  Region R;
  R.format();
  FlightLogger lg(R.log);
  lg.sessionStart(0);
  for (int i = 0; i < 30; i++) { lg.line(i * 300, mkSample(i, 42.5, 23.3, 30, 100, 0)); R.log.service(i * 300); }
  lg.sessionEnd(10000);
  R.drain(20000);
  struct Out : ITextOut { std::vector<std::string> v; void put(const char* l) override { v.push_back(l); } } out;
  LogShell sh(R.log);
  auto send = [&](const char* s) { out.v.clear(); for (const char* p = s; *p; p++) sh.feed((uint8_t)*p, out); sh.feed('\n', out); };
  send("LOGINFO");
  CHECK(out.v.size() == 1 && out.v[0].find("state=ready used=" + std::to_string(R.log.usedBytes()) + " cap=261888") != std::string::npos);
  send("LOGREAD 0 256");
  CHECK(out.v.size() == 3 && out.v[2].rfind("lg ok read 0 256 crc=", 0) == 0);
  if (out.v.size() == 3) {
    std::vector<uint8_t> raw(256);
    R.log.read(0, raw.data(), 256);
    CHECK(out.v[2] == "lg ok read 0 256 crc=" + std::to_string(terrain::crc32(0, raw.data(), 256)));
    CHECK(out.v[0].rfind("lg d 0 a5", 0) == 0);
  }
  CHECK(R.log.usedBytes() >= 768 && R.log.usedBytes() <= 1024);
  send("LOGREAD 0 2048");
  CHECK(out.v.size() == 1 && out.v[0].rfind("lg err", 0) == 0);   // beyond what is written
  send("hello there");
  CHECK(out.v.empty());
  {  // TIME epoch tz: the PC sets the clock over the same shell
    struct Sink : ITimeSink { long long e = 0; int tz = 0; bool setTime(int64_t ep, int z) override { if (ep < 1000000000LL) return false; e = ep; tz = z; return true; } } sink;
    send("TIME 1760000000 180");
    CHECK(out.v.size() == 1 && out.v[0] == "tm err refused" && sink.e == 0);       // no sink wired (the vario-only image)
    sh.setTimeSink(&sink);
    send("TIME 1760000000 180");
    CHECK(out.v.size() == 1 && out.v[0] == "tm ok 1760000000 tz=180" && sink.e == 1760000000LL && sink.tz == 180);
    send("TIME 5 0");
    CHECK(out.v.size() == 1 && out.v[0] == "tm err refused");
    send("TIME 1760000000 99999");
    CHECK(out.v.size() == 1 && out.v[0] == "tm err refused" && sink.tz == 180);
    send("TIME?");
    CHECK(out.v.size() == 1 && out.v[0] == "tm now unknown");
    sh.setTimeSink(nullptr);
  }
  send("LOGERASE");
  CHECK(out.v.size() == 1 && out.v[0] == "lg ok erase started" && R.log.erasing());
  for (int i = 0; i < 1000 && R.log.erasing(); i++) R.log.service(30000 + i);
  CHECK(R.log.state() == FlashLog::READY && R.log.usedBytes() == 0);
  // the pack side cannot reach the tail
  ReservedTailFlash view(R.fl, kLogBytes);
  CHECK(view.capacity() == R.fl.capacity() - kLogBytes);
  uint8_t b[4] = {0, 0, 0, 0};
  CHECK(view.read(view.capacity() - 4, b, 4));
  CHECK(!view.read(view.capacity() - 3, b, 4));
  CHECK(!view.program(view.capacity(), b, 4));
  CHECK(!view.startErase(view.capacity(), kBlockBytes));
  CHECK(view.startErase(view.capacity() - kBlockBytes, kBlockBytes));
}

static void testYawSign() {
  puts("yaw rate: earth-frame, clockwise positive, any tilt");
  const double D = 0.017453292519943295;
  for (int tilt = 0; tilt < 3; tilt++) {
    ImuSim s(40, tilt == 0 ? 0 : 35, tilt == 2 ? -50 : 10);
    s.bias[0] = 0.01; s.bias[1] = -0.02;
    Ahrs f;
    double t = 0;
    runImu(s, f, t, 10, noRate);
    const float h0 = (float)s.truthHeading();
    double sum = 0; int n = 0;
    const double dt = 1.0 / 80;
    for (int i = 0; i < 80 * 10; i++) {
      double w[3];
      s.yawRates(-0.2, w);                              // 11.46 deg/s clockwise seen from above
      s.step(w, dt);
      t += dt;
      Vec3 g, a, m;
      s.sample(w, g, a, m);
      f.update(g, a, m, (uint32_t)llround(t * 1000));
      sum += earthYawRateCwDps(g, f.bias(), f.up()); n++;
    }
    NEAR(sum / n, 0.2 / D, 0.6);
    NEAR(angErr(s.truthHeading() > h0 ? (float)s.truthHeading() : (float)s.truthHeading() + 360, h0 + 11.459f * 10), 0, 0.5);   // and the heading did go clockwise
  }
}

// ---- synthetic circles ----
struct Circle {
  double V = 10, we = 0, wn = 0, omega = 12, omegaVar = 6;           // m/s air speed, wind east / north, turn rate deg/s clockwise
  double bias = 0, gyroNoise = 0, courseNoise = 0;     // deg/s, deg/s rms, deg rms
  double fixHz = 3, lagMs = 800, seconds = 120;
};

static std::vector<uint8_t> makeLog(const Circle& c, uint32_t seed = 1) {
  Region* R = new Region;
  R->format();
  FlightLogger lg(R->log);
  std::mt19937 rng(seed);
  std::normal_distribution<double> nd(0, 1);
  const double lat0 = 42.5, lon0 = 23.3;
  const double D = 0.017453292519943295;
  lg.sessionStart(0);
  struct Fx { double ts; };
  double h = 20, x = 0, y = 0;
  double nextFix = 1000.0 / c.fixHz, nextGyro = 0;
  struct Pending { uint32_t rx; Sample s; };
  std::vector<Pending> pend;
  const uint32_t T = (uint32_t)(c.seconds * 1000);
  for (uint32_t ms = 1; ms < T; ms++) {
    h += (c.omega + c.omegaVar * sin(ms * 2 * M_PI / 20000.0)) * 0.001;   // a turn rate that changes, or the lag cannot be seen
    const double vx = c.V * sin(h * D) + c.we, vy = c.V * cos(h * D) + c.wn;
    x += vx * 0.001; y += vy * 0.001;
    if (ms >= nextFix) {
      nextFix += 1000.0 / c.fixHz;
      double crs = atan2(vx, vy) / D + nd(rng) * c.courseNoise;
      crs = fmod(crs + 720, 360);
      Sample s = mkSample((ms / 1000) % 60, lat0 + y / flightlog_io::kMPerDeg, lon0 + x / (flightlog_io::kMPerDeg * cos(lat0 * D)),
                          (float)(hypot(vx, vy) * 3.6), (float)crs, 1.0f);
      pend.push_back({ms + (uint32_t)c.lagMs, s});
    }
    for (size_t i = 0; i < pend.size();) {
      if (pend[i].rx <= ms) { lg.line(pend[i].rx, pend[i].s); pend.erase(pend.begin() + i); } else i++;
    }
    if (ms >= nextGyro) {
      nextGyro += 12.5;
      lg.yaw(ms, (float)(c.omega + c.omegaVar * sin(ms * 2 * M_PI / 20000.0) + c.bias + nd(rng) * c.gyroNoise));
    }
    if ((ms & 15) == 0) R->log.service(ms);
    if (R->log.usedBytes() >= R->log.capacityBytes() - 2 * kLogPage) break;
  }
  lg.sessionEnd(T);
  R->drain(T + 100);
  std::vector<uint8_t> raw(R->log.usedBytes());
  R->log.read(0, raw.data(), (uint32_t)raw.size());
  delete R;
  return raw;
}

static flightlog_io::EvalResult evalCircle(const Circle& c, bool wind = false, float latency = NAN) {
  auto raw = makeLog(c);
  auto L = flightlog_io::decode(raw.data(), raw.size());
  flightlog_io::EvalOptions o;
  o.wind = wind;
  o.latencyMs = latency;
  return flightlog_io::evaluate(L, o);
}

static void testDeadReckoningCircles() {
  puts("dead reckoning: synthetic circles, error against holding the last fix");
  {  // still air, 12 deg/s, clean gyro: measured lag, then a large gain
    Circle c;
    auto r = evalCircle(c);
    printf("  clean: lag %.0f ms, position hold %.2f dr %.2f m, course hold %.2f dr %.2f deg\n", r.lagMs, r.turnHoldPos, r.turnDrPos, r.turnHoldCrs, r.turnDrCrs);
    NEAR(r.lagMs, 800, 60);
    NEAR(r.fixRateHz, 3.0, 0.1);
    CHECK(r.turnPoints > 500);
    CHECK(r.turnHoldCrs > 8.0f);
    CHECK(r.turnDrCrs < 1.0f);
    CHECK(r.turnDrPos < 0.15f * r.turnHoldPos);
    CHECK(r.turnDrPos < 1.5f);
  }
  {  // gyro bias 2 deg/s, noise 3 deg/s rms, GPS course noise 3 deg: still far better than holding
    Circle c; c.bias = 2; c.gyroNoise = 3; c.courseNoise = 3;
    auto r = evalCircle(c, false, 800);
    printf("  bias+noise: position hold %.2f dr %.2f m, course hold %.2f dr %.2f deg\n", r.turnHoldPos, r.turnDrPos, r.turnHoldCrs, r.turnDrCrs);
    CHECK(r.turnDrCrs < 0.45f * r.turnHoldCrs);
    CHECK(r.turnDrPos < 0.5f * r.turnHoldPos);
  }
  {  // wind drift: the air velocity turns, the ground track does not turn evenly. A drift wind taken from the last circle fixes that.
    Circle c; c.omega = 16; c.we = 5; c.wn = 2;
    auto a = evalCircle(c, false, 800), b = evalCircle(c, true, 800);
    printf("  wind: course dr %.2f (no wind model) %.2f (drift wind) hold %.2f deg; position %.2f / %.2f / hold %.2f m\n",
           a.turnDrCrs, b.turnDrCrs, a.turnHoldCrs, a.turnDrPos, b.turnDrPos, a.turnHoldPos);
    CHECK(a.turnDrCrs < a.turnHoldCrs);
    CHECK(b.turnDrCrs < a.turnDrCrs);
    CHECK(b.turnDrPos < a.turnDrPos);
    CHECK(b.turnDrPos < 0.1f * a.turnHoldPos);
  }
  {  // a wrong latency costs: the evaluator's sweep is meaningful
    Circle c;
    auto good = evalCircle(c, false, 800), bad = evalCircle(c, false, 0);
    CHECK(good.turnDrPos < bad.turnDrPos);
  }
  {  // straight flight: dead reckoning must not make it worse
    Circle c; c.omega = 0; c.omegaVar = 0;
    auto r = evalCircle(c, false, 800);
    CHECK(r.drPosMean <= r.holdPosMean + 0.05f);
    CHECK(r.drCrsMean < 0.5f);
  }
}

static void testReckonerBasics() {
  puts("dead reckoning: reset at a fix, stale gyro, no fix");
  DeadReckoner dr;
  dr.setLatencyMs(0);
  CHECK(!dr.at(1000).valid);
  for (uint32_t t = 0; t <= 1000; t += 20) dr.yaw(t, 90.0f);          // a quarter turn per second
  dr.fix(1000, true, 42.0, 23.0, 36.0f, 0.0f);                        // 10 m/s north
  auto e = dr.at(1000);
  NEAR(e.courseDeg, 0, 0.01);
  e = dr.at(1600);
  CHECK(e.valid && e.gyroLive == false);                              // the last gyro sample is 600 ms old: the course is only held
  for (uint32_t t = 1020; t <= 1500; t += 20) dr.yaw(t, 90.0f);
  e = dr.at(1500);
  CHECK(e.gyroLive);
  NEAR(e.courseDeg, 45, 0.5);
  NEAR(e.speedKmh, 36, 0.01);
  CHECK(e.eastM > 0 && e.northM > 4 && e.northM < 5.2);                // arc of 10 m/s over 0.5 s at 90 deg/s
  dr.fix(1500, true, 42.0, 23.0, 36.0f, 100.0f);                      // a new fix replaces everything
  NEAR(dr.at(1500).courseDeg, 100, 0.5);
  CHECK(!dr.at(1500 + 4000).valid);                                   // too old
  dr.fix(2000, false, 0, 0, NAN, NAN);
  CHECK(!dr.at(2000).valid);
}

static void testAppHooks() {
  puts("flight log: the app reports lines and yaw only while data is fresh; frames are not touched");
  struct Rec : IFlightLog {
    int starts = 0, ends = 0, lines = 0, yaws = 0;
    void sessionStart(uint32_t) override { starts++; }
    void sessionEnd(uint32_t) override { ends++; }
    void line(uint32_t, const Sample&) override { lines++; }
    void yaw(uint32_t, float) override { yaws++; }
  };
  Rig r;
  r.compass.psi = 100;
  r.compass.gyro = true;
  Rec rec;
  Driven app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power);
  app.setLog(&rec);
  app.begin(0);
  uint32_t now = 0;
  for (; now < 3000; now += 10) app.tick(now);                        // no tracer data yet
  CHECK(rec.starts == 1 && rec.lines == 0 && rec.yaws == 0);
  for (int i = 0; i < 6; i++) {
    r.tracer.lines.push_back(line("XCTRC,2026,10,15,9,52,20,0,42.500000,23.300000,800.0,30.0,90.0,1.0,,,,997.88,85"));
    for (int k = 0; k < 33; k++, now += 10) app.tick(now);
  }
  CHECK(rec.lines == 6);
  CHECK(rec.yaws > 100);
  for (int k = 0; k < 500; k++, now += 10) app.tick(now);               // the data goes stale: no more yaw
  const int y = rec.yaws;
  for (int k = 0; k < 100; k++, now += 10) app.tick(now);
  CHECK(rec.yaws == y);
  app.end();
  CHECK(rec.ends == 1);
}

inline void run() {
  testFlashLog();
  testYawCodec();
  testShellAndView();
  testYawSign();
  testReckonerBasics();
  testDeadReckoningCircles();
  testAppHooks();
}

}  // namespace flightlog_tests
