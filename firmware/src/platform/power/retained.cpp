#include "platform/power/retained.h"
#include <stddef.h>
#include <string.h>
#include "platform/terrain/crc32.h"

namespace retained {

const char* name(Result r) { return r == Result::OK ? "ok" : r == Result::CRC ? "crc" : "none"; }

uint32_t checksum(const Block& b) { return terrain::crc32(0, &b, offsetof(Block, crc)); }

Result check(const Block& b) {
  if (b.magic != kMagic || b.version != kVersion) return Result::NONE;
  if (b.count > WeatherLog::kN || b.pushed > b.count || b.crc != checksum(b)) return Result::CRC;
  return Result::OK;
}

Result Retention::boot(WeatherLog& weather, WallClock& clock, bool restoreClock, uint32_t nowMs) {
  result_ = check(b_);
  clockRestored_ = false;
  bridged_ = 0;
  if (result_ == Result::OK) {
    weather.restore(b_.v, b_.count, b_.refEpoch, b_.pushed);
    if (restoreClock && b_.clockEpoch && clock.setEpoch((int64_t)b_.clockEpoch, nowMs)) clockRestored_ = true;
  }
  uint32_t e;
  if (clock.epoch(nowMs, &e)) bridged_ = weather.bridge(e, nowMs);   // the clock came back with the restore: bridge at once
  lastPush_ = weather.pushCount();
  lastSets_ = clock.sets();
  save(nowMs, weather, clock);                       // the block now describes this boot (a stale clock epoch of a System OFF is gone)
  return result_;
}

void Retention::save(uint32_t nowMs, const WeatherLog& weather, const WallClock& clock) {
  uint32_t e = 0;
  if (!clock.epoch(nowMs, &e)) e = 0;
  b_.magic = kMagic;
  b_.version = kVersion;
  b_.clockEpoch = e;
  b_.refEpoch = weather.refEpoch();
  b_.count = (uint16_t)weather.snapshot(b_.v);
  b_.pushed = (uint16_t)weather.pushedSince();
  if (b_.pushed > b_.count) b_.pushed = b_.count;
  for (int i = b_.count; i < WeatherLog::kN; i++) b_.v[i] = 0;
  b_.crc = checksum(b_);
  lastSaveMs_ = nowMs;
  lastPush_ = weather.pushCount();
  lastSets_ = clock.sets();
}

void Retention::tick(uint32_t nowMs, WeatherLog& weather, const WallClock& clock) {
  uint32_t e;
  if (weather.bridgePending() && clock.epoch(nowMs, &e)) bridged_ = weather.bridge(e, nowMs);
  if (weather.pushCount() != lastPush_ || clock.sets() != lastSets_ || (uint32_t)(nowMs - lastSaveMs_) >= kSaveEveryMs)
    save(nowMs, weather, clock);
}

int ramSections(uint32_t addr, uint32_t len, RamSection* out, int max) {
  const uint32_t kBase = 0x20000000u, kBig = 0x20010000u, kEnd = 0x20040000u;
  if (!len || addr < kBase || addr >= kEnd) return 0;
  uint32_t last = addr + len - 1;
  if (last >= kEnd) last = kEnd - 1;
  int n = 0;
  for (uint32_t a = addr; a <= last;) {
    RamSection s;
    uint32_t next;
    if (a < kBig) {                                  // 4 kB sections, two per 8 kB block
      const uint32_t off = a - kBase;
      s.block = (uint8_t)(off / 0x2000);
      s.section = (uint8_t)((off % 0x2000) / 0x1000);
      next = kBase + (off / 0x1000 + 1) * 0x1000;
    } else {                                         // 32 kB sections of the big block
      const uint32_t off = a - kBig;
      s.block = 8;
      s.section = (uint8_t)(off / 0x8000);
      next = kBig + (off / 0x8000 + 1) * 0x8000;
    }
    if (n < max) out[n++] = s;
    if (next == 0 || next > last) break;
    a = next;
  }
  return n;
}

}  // namespace retained
