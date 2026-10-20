#pragma once
// Retained state (platform/power/retained.h): the block's checksum, the restore after a reset and after a System OFF, the gaps
// bridged once the time is known, a history older than 24 h, a corrupted block, and the RAM sections kept powered.
// Included by tests.cpp.
#include <string.h>
#include "platform/power/retained.h"
#include "platform/power/wth_line.h"
#include "platform/sensors/bmp280.h"

namespace rt {

static const uint32_t T0 = 1790000000u;           // an epoch in 2026
static const uint32_t SLOT = 600;

// A box that has run for a while: n samples 10 min apart (value 1000.0 + i hPa*10), the newest at epoch T0, the clock set.
static void fill(WeatherLog& w, WallClock& c, int n, uint32_t nowMs) {
  c.setEpoch((int64_t)T0, nowMs);
  for (int i = 0; i < n; i++) w.push(1000.0f + 0.1f * (float)i, T0 - (uint32_t)(n - 1 - i) * SLOT);
}

static void block_and_checksum() {
  puts("retained: block checksum, fresh and corrupted blocks");
  static retained::Block b;
  memset(&b, 0, sizeof b);
  CHECK(retained::check(b) == retained::Result::NONE);              // power-on RAM: no block
  memset(&b, 0xA5, sizeof b);
  CHECK(retained::check(b) == retained::Result::NONE);
  WeatherLog w; WallClock c;
  fill(w, c, 40, 1000);
  retained::Retention r(b);
  r.save(1000, w, c);
  CHECK(retained::check(b) == retained::Result::OK && b.count == 40 && b.clockEpoch == T0 && b.refEpoch == T0);
  retained::Block good = b;
  b.v[7] ^= 1;                                                      // one flipped bit
  CHECK(retained::check(b) == retained::Result::CRC);
  b = good; b.clockEpoch += 1;
  CHECK(retained::check(b) == retained::Result::CRC);
  b = good; b.magic ^= 0x100;
  CHECK(retained::check(b) == retained::Result::NONE);
  b = good; b.version++;
  CHECK(retained::check(b) == retained::Result::NONE);              // another layout: never read
  b = good; b.count = 500; b.crc = retained::checksum(b);
  CHECK(retained::check(b) == retained::Result::CRC);               // a valid checksum on an impossible count is still refused
  CHECK(!strcmp(retained::name(retained::Result::OK), "ok") && !strcmp(retained::name(retained::Result::CRC), "crc") && !strcmp(retained::name(retained::Result::NONE), "none"));
}

static void reset_restore() {
  puts("retained: a reset restores the history and the clock; the gap since the last sample becomes empty slots");
  static retained::Block b;
  memset(&b, 0, sizeof b);
  WeatherLog w; WallClock c;
  fill(w, c, 60, 5000);
  retained::Retention r(b);
  r.save(5000, w, c);
  // Reset 12 s later: the box restarts its millis() at 0.
  { WeatherLog w2; WallClock c2;
    retained::Retention r2(b);
    CHECK(r2.boot(w2, c2, true, 0) == retained::Result::OK);
    CHECK(r2.clockRestored() && c2.valid(0));
    uint32_t e = 0;
    CHECK(c2.epoch(0, &e) && e == T0);                              // lost: the time since the last save (here nothing)
    CHECK(w2.count() == 60 && w2.get(0) == 10059 && w2.get(59) == 10000);
    CHECK(r2.bridged() == 0);
    // The next sample is not due yet: the slot grid goes on where it was (10 min after the newest sample).
    MockBaro baro; baro.hPa = 1100.0f;
    w2.sample(1000, baro, T0 + 1);
    CHECK(baro.reads == 0 && w2.count() == 60);
    w2.sample(SLOT * 1000u, baro, T0 + SLOT);
    CHECK(baro.reads == 1 && w2.count() == 61 && w2.latest() == 11000);
  }
  // Reset 35 min after the newest sample (the clock restored from the last save, which was then 35 min later): 3 slots passed, 2 are gaps,
  // the next sample is taken at once and fills the third.
  { c.setEpoch((int64_t)T0 + 35 * 60, 5000);
    r.save(5000, w, c);                                             // the heartbeat 35 min later; the history has not grown
    WeatherLog w2; WallClock c2;
    retained::Retention r2(b);
    CHECK(r2.boot(w2, c2, true, 0) == retained::Result::OK);
    CHECK(r2.bridged() == 2 && w2.count() == 62);
    CHECK(w2.get(0) == 0 && w2.get(1) == 0 && w2.get(2) == 10059);
    MockBaro baro; baro.hPa = 1012.3f;
    w2.sample(100, baro, T0 + 35 * 60);
    CHECK(baro.reads == 1 && w2.latest() == 10123 && w2.get(1) == 0 && w2.get(2) == 0 && w2.get(3) == 10059);
  }
  // The clock is not restored (System OFF): the history is, the clock stays unknown.
  { WeatherLog w2; WallClock c2;
    retained::Retention r2(b);
    CHECK(r2.boot(w2, c2, false, 0) == retained::Result::OK && !r2.clockRestored() && !c2.valid(0));
    CHECK(w2.count() == 62 && w2.bridgePending());           // the ring as the 35 min boot left it
  }
}

static void unknown_clock_then_known() {
  puts("retained: System OFF: samples taken while the time is unknown, the gap is bridged once it is known");
  static retained::Block b;
  memset(&b, 0, sizeof b);
  WeatherLog w; WallClock c;
  fill(w, c, 50, 0);
  retained::Retention r(b);
  r.save(0, w, c);
  WeatherLog w2; WallClock c2;
  retained::Retention r2(b);
  CHECK(r2.boot(w2, c2, false, 0) == retained::Result::OK);
  CHECK(b.clockEpoch == 0);                                         // the save at boot forgot the stale clock
  MockBaro baro;
  baro.hPa = 1001.0f; w2.sample(0, baro, 0);                        // at boot: one sample, time unknown
  baro.hPa = 1002.0f; w2.sample(SLOT * 1000u, baro, 0);             // 10 min later
  CHECK(w2.count() == 52 && w2.pushedSince() == 2);
  // A reset while the time is still unknown keeps the anchor and the count of the samples since.
  r2.tick(SLOT * 1000u + 1, w2, c2);
  { WeatherLog w3; WallClock c3;
    retained::Retention r3(b);
    CHECK(r3.boot(w3, c3, true, 0) == retained::Result::OK && !r3.clockRestored());
    CHECK(w3.count() == 52 && w3.pushedSince() == 2 && w3.bridgePending());
  }
  // The PC sets the time: 2 h after the last old sample. The two new samples are the last two of the 12 slots that passed.
  c2.setEpoch((int64_t)T0 + 2 * 3600, SLOT * 1000u + 5000);
  r2.tick(SLOT * 1000u + 5000, w2, c2);
  CHECK(r2.bridged() == 12 - 2 && !w2.bridgePending());
  CHECK(w2.count() == 50 + 10 + 2);
  CHECK(w2.get(0) == 10020 && w2.get(1) == 10010 && w2.get(2) == 0 && w2.get(11) == 0 && w2.get(12) == 10049);
  CHECK(w2.refEpoch() == T0 + 2 * 3600 && w2.pushedSince() == 0);
  // Fresh gaps do not harm the chart's range or the trend.
  int lo, hi;
  CHECK(w2.chartRange(lo, hi) && lo > 900);
  int cls = 0;
  CHECK(w2.tendency(cls));                                          // both ends of the 3 h span are real samples (the gap is newer)
}

static void too_old_and_corrupt() {
  puts("retained: older than 24 h is dropped, a corrupted block starts empty");
  static retained::Block b;
  memset(&b, 0, sizeof b);
  WeatherLog w; WallClock c;
  fill(w, c, 80, 0);
  retained::Retention r(b);
  r.save(0, w, c);
  c.setEpoch((int64_t)T0 + 25 * 3600, 0);
  r.save(0, w, c);                                                  // a reset 25 h after the last sample
  { WeatherLog w2; WallClock c2;
    retained::Retention r2(b);
    CHECK(r2.boot(w2, c2, true, 0) == retained::Result::OK);
    CHECK(r2.bridged() == -1 && w2.count() == 0 && w2.latest() == 0);
  }
  c.setEpoch((int64_t)T0 + 23 * 3600, 0);                           // 23 h: kept, 137 gaps would overflow the ring, the oldest fall off
  r.save(0, w, c);
  { WeatherLog w2; WallClock c2;
    retained::Retention r2(b);
    r2.boot(w2, c2, true, 0);
    int cls = 0;
    CHECK(r2.bridged() == 137);
    CHECK(w2.count() == WeatherLog::kN && w2.get(0) == 0 && w2.get(136) == 0 && w2.get(137) == 10079);
    CHECK(!w2.tendency(cls));                                       // a gap at the newest end: no tendency
  }
  b.v[3] ^= 0x40;                                                   // a bit flips in retained RAM
  { WeatherLog w2; WallClock c2;
    retained::Retention r2(b);
    CHECK(r2.boot(w2, c2, true, 0) == retained::Result::CRC);
    CHECK(w2.count() == 0 && !c2.valid(0) && !r2.clockRestored());
    CHECK(retained::check(b) == retained::Result::OK);              // and the block is rewritten clean
  }
}

static void saving_and_clock() {
  puts("retained: saves on change and on the heartbeat; the clock exposes its epoch");
  static retained::Block b;
  memset(&b, 0, sizeof b);
  WeatherLog w; WallClock c;
  retained::Retention r(b);
  r.boot(w, c, true, 0);
  CHECK(r.result() == retained::Result::NONE && b.count == 0 && b.clockEpoch == 0);
  uint32_t e = 0;
  CHECK(!c.epoch(0, &e));
  c.set(12 * 3600, 0);                                              // GPS time of day: no date, no epoch
  CHECK(!c.epoch(0, &e));
  CHECK(c.setEpoch((int64_t)T0, 100) && c.epoch(100, &e) && e == T0);
  CHECK(c.epoch(100 + 90 * 1000, &e) && e == T0 + 90);
  r.tick(200, w, c);                                                // the PC set the time: saved at once
  CHECK(b.clockEpoch == T0 + 0 && retained::check(b) == retained::Result::OK);
  w.push(1013.2f, T0);
  r.tick(300, w, c);                                                // a new sample: saved at once
  CHECK(b.count == 1 && b.v[0] == 10132 && b.refEpoch == T0);
  const uint32_t crc0 = b.crc;
  r.tick(300 + 4000, w, c);
  CHECK(b.crc == crc0);                                             // nothing changed, not yet 5 s
  r.tick(300 + 6000, w, c);
  CHECK(b.clockEpoch == T0 + 6 && b.crc != crc0);                   // the heartbeat moved the clock on
  CHECK(w.pushCount() == 1);
}

static void ram_sections() {
  puts("retained: the RAM sections that stay powered in System OFF");
  retained::RamSection s[8];
  CHECK(retained::ramSections(0x20006000, 0x100, s, 8) == 1 && s[0].block == 3 && s[0].section == 0);       // 0x6000 = block 3, first half
  CHECK(retained::ramSections(0x20007F00, 0x200, s, 8) == 2 && s[0].block == 3 && s[0].section == 1 && s[1].block == 4 && s[1].section == 0);
  CHECK(retained::ramSections(0x20010000, 4, s, 8) == 1 && s[0].block == 8 && s[0].section == 0);
  CHECK(retained::ramSections(0x20028000, 400, s, 8) == 1 && s[0].block == 8 && s[0].section == 3);
  CHECK(retained::ramSections(0x2003FFF0, 0x100, s, 8) == 1 && s[0].block == 8 && s[0].section == 5);        // clipped at the end of RAM
  CHECK(retained::ramSections(0x10000000, 16, s, 8) == 0 && retained::ramSections(0x20000000, 0, s, 8) == 0);
}

static void wth_line() {
  puts("wth status line, BMP280 temperature");
  WthData d;
  char b[120];
  formatWth(b, sizeof b, d);
  CHECK(!strcmp(b, "wth p=-- tb=-- ts=-- rh=-- batt=-- -- log=0 ret=none"));
  d.hPa = 1013.24f; d.baroC = 21.46f; d.shtC = 21.0f; d.rh = 47.6f; d.battPct = 76; d.battMv = 3987; d.logCount = 12; d.ret = "ok";
  formatWth(b, sizeof b, d);
  CHECK(!strcmp(b, "wth p=1013.2 tb=21.5 ts=21.0 rh=48 batt=76% 3987mV log=12 ret=ok"));
  d.ret = "crc"; d.rh = NAN;
  formatWth(b, sizeof b, d);
  CHECK(strstr(b, "rh=-- ") && strstr(b, "ret=crc"));
  char small[16];
  CHECK(formatWth(small, sizeof small, d) > (int)sizeof small && small[sizeof small - 1] == 0);   // truncated, still a string
  bx::Bmp280Cal c;                                                       // the datasheet's example (section 3.12): 25.08 C
  const uint8_t raw[24] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B, 0x8C, 0x00,
                           0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17};
  c.parse(raw);
  NEAR(c.tempC(519888), 25.08, 0.005);
  MockBaro baro;
  baro.tempC = 18.5f;
  float h = 0, t = 0;
  CHECK(baro.readNowT(h, t) && t == 18.5f && h > 1000.0f);
  baro.present = false;
  CHECK(!baro.readNowT(h, t));
}

static void run() {
  wth_line();
  block_and_checksum();
  reset_restore();
  unknown_clock_then_known();
  too_old_and_corrupt();
  saving_and_clock();
  ram_sections();
}

}  // namespace rt
