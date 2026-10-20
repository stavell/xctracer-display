#pragma once
// Retained state: the barometer history (WeatherLog) and the wall clock survive a restart. A copy lives in a block of
// RAM that no start-up code clears (the device puts it in the top 4 kB page of RAM, platform/hw/retention_nrf.h) and, for the System
// OFF of the power-off, is kept powered. Pure code: this file is the block layout, its checksum, the restore and the
// bookkeeping; the device only decides where the block lives and which RAM stays on.
//
// What survives what:
//   soft reset, watchdog, reset pin       RAM intact: history restored; the clock restored from its last save (at most a few seconds
//                                        old, plus the time the box was down). USB bench command REBOOT does a soft reset
//   flash by USB (the bootloader's DFU)   RAM lost (the bootloader reuses it): ret=none, history empty, clock unknown until the
//                                        PC's TIME (host/settime.py) or a GPS fix. Measured, see NOTES "Retention across resets"
//   System OFF (power off, wake by switch)  RAM retained: history restored; the clock stays unknown (the time asleep is unknown) until
//                                        GPS or the PC sets it, then the slots that passed become gaps in the history
//   battery out, brown-out               RAM lost: the checksum fails (or the magic is absent), everything starts empty
// A history older than 24 h when the time is known again is dropped.
#include <stddef.h>
#include <stdint.h>
#include "platform/clock.h"
#include "platform/power/weather.h"

namespace retained {

constexpr uint32_t kMagic = 0x52455442u;            // "RETB"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kSaveEveryMs = 5000;             // the clock's heartbeat (a change of the history or a PC time saves at once)

struct Block {
  uint32_t magic, version;
  uint32_t clockEpoch;                              // wall clock UTC seconds at the last save, 0 = unknown
  uint32_t refEpoch;                                // UTC seconds of the sample `pushed` places before the newest, 0 = unknown
  uint16_t count, pushed;
  int16_t v[WeatherLog::kN];                        // pressures hPa*10, oldest first, 0 = a gap
  uint32_t crc;                                     // CRC-32 of everything above
};

enum class Result : uint8_t { NONE, OK, CRC };      // no block (power-on, never written) / restored / a block that fails its checksum
const char* name(Result r);                         // "none", "ok", "crc"

uint32_t checksum(const Block& b);
Result check(const Block& b);

class Retention {
 public:
  explicit Retention(Block& block) : b_(block) {}
  // At boot, before the first sample. restoreClock: a restart that did not lose time (a reset), not the System OFF wake.
  Result boot(WeatherLog& weather, WallClock& clock, bool restoreClock, uint32_t nowMs);
  // Every loop pass: bridges the history once the time is known, and saves when the history grew, the PC set the time, or
  // kSaveEveryMs passed.
  void tick(uint32_t nowMs, WeatherLog& weather, const WallClock& clock);
  Result result() const { return result_; }
  bool clockRestored() const { return clockRestored_; }
  int bridged() const { return bridged_; }          // gaps inserted by the last bridge, -1 = the history was dropped (older than 24 h)
  void save(uint32_t nowMs, const WeatherLog& weather, const WallClock& clock);

 private:
  Block& b_;
  Result result_ = Result::NONE;
  bool clockRestored_ = false;
  int bridged_ = 0;
  uint32_t lastSaveMs_ = 0, lastPush_ = 0, lastSets_ = 0;
};

// The RAM that must stay powered in System OFF for [addr, addr + len): nRF52840 has eight 8 kB blocks (two 4 kB sections each) from
// 0x20000000 and a 192 kB block (six 32 kB sections) from 0x20010000. Each entry is a block index and the retention bit
// (POWERSET bit 16 + section).
struct RamSection { uint8_t block; uint8_t section; };
int ramSections(uint32_t addr, uint32_t len, RamSection* out, int max);

}  // namespace retained
