#pragma once
// What the flight logger writes, and the logger itself. Pure C++ (the sink is the flash log on the device, a vector in the tests).
//
// A log is a stream of records inside 256-byte pages (platform/storage/flash_log.h). All numbers little endian.
//
//   'S' 0x53  session start     u32 ms (box clock), u8 version = 1, u8 yaw grid Hz = 25                      7 bytes
//   'L' 0x4C  one tracer line   u32 ms (box clock, when the line was received), u8 h, u8 m, u8 s (the line's UTC time),
//                               u8 flags (bit 0 = GPS fix), i32 lat * 1e7, i32 lon * 1e7, u16 speed * 0.1 km/h,
//                               u16 course * 0.1 deg (0xFFFF = none), i16 vario * 100 cm/s (0x8000 = none)    23 bytes
//   'Y' 0x59  yaw rate block    u32 ms of the first sample, i16 first value, u8 n, n * i8 steps. Sample k is at ms + 40 k.
//                               Values are in units of 0.05 deg/s; a step is 5 units (0.25 deg/s) times the i8, and the
//                               encoder follows the decoded value, so the quantisation error never adds up.  8 + n bytes
//   0xFF      padding to the end of the page
//
// The yaw rate is the turn rate about the vertical, clockwise positive (platform/sensors/yawrate.h), averaged over each
// 40 ms of the 80 Hz sensor reads. About 28 bytes/s for it, 69 bytes/s for the tracer lines at 3 Hz: roughly 360 kB/hour
// with the page padding.
#include <stddef.h>
#include <stdint.h>
#include "apps/vario/core/flight_log_port.h"
#include "platform/storage/record_sink.h"

namespace flightlog {

constexpr uint8_t kVersion = 1;
constexpr uint8_t kRecSession = 'S', kRecLine = 'L', kRecYaw = 'Y', kRecPad = 0xFF;
constexpr uint32_t kYawGridMs = 40;
constexpr int kYawBlock = 48;                     // steps per block (49 samples, about 2 s)
constexpr float kYawUnitDps = 0.05f;
constexpr int kYawStepUnits = 5;
constexpr uint16_t kNoU16 = 0xFFFF;
constexpr int16_t kNoI16 = (int16_t)0x8000;

class FlightLogger : public IFlightLog {
 public:
  explicit FlightLogger(storage::IRecordSink& sink) : sink_(sink) {}
  void sessionStart(uint32_t nowMs) override;
  void sessionEnd(uint32_t nowMs) override;
  void line(uint32_t nowMs, const Sample& s) override;
  void yaw(uint32_t nowMs, float cwDps) override;
  uint32_t records() const { return records_; }

 private:
  void emitYaw(uint32_t gridMs, float dps);
  void flushYaw();
  void put(const uint8_t* rec, size_t n);
  storage::IRecordSink& sink_;
  uint32_t records_ = 0;
  // 40 ms averaging window
  bool win_ = false;
  uint32_t winStart_ = 0, lastEmit_ = 0;
  double sum_ = 0;
  int cnt_ = 0;
  // open block
  bool blk_ = false;
  uint32_t blkMs_ = 0;
  int16_t first_ = 0;
  int32_t value_ = 0;
  uint8_t steps_[kYawBlock];
  int nSteps_ = 0;
};

}  // namespace flightlog
