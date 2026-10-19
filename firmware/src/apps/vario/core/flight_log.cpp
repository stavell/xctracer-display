#include "apps/vario/core/flight_log.h"
#include <math.h>

namespace flightlog {

static void le32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static uint16_t scaleU16(float v, float k) {
  if (!isfinite(v) || v < 0) return kNoU16;
  const float x = v * k + 0.5f;
  return x >= 65534.0f ? (uint16_t)65534 : (uint16_t)x;
}

void FlightLogger::put(const uint8_t* rec, size_t n) {
  if (sink_.record(rec, n)) records_++;
}

void FlightLogger::sessionStart(uint32_t nowMs) {
  flushYaw();
  win_ = false;
  uint8_t r[7] = {kRecSession};
  le32(r + 1, nowMs);
  r[5] = kVersion;
  r[6] = (uint8_t)(1000 / kYawGridMs);
  put(r, sizeof r);
}

void FlightLogger::sessionEnd(uint32_t) {
  flushYaw();
  win_ = false;
  sink_.flush();
}

void FlightLogger::line(uint32_t nowMs, const Sample& s) {
  uint8_t r[23];
  r[0] = kRecLine;
  le32(r + 1, nowMs);
  r[5] = s.hh; r[6] = s.mm; r[7] = s.ss;
  r[8] = s.fix ? 1 : 0;
  le32(r + 9, (uint32_t)(int32_t)lround(s.lat * 1e7));
  le32(r + 13, (uint32_t)(int32_t)lround(s.lon * 1e7));
  le16(r + 17, scaleU16(s.speedKmh, 10.0f));
  le16(r + 19, scaleU16(s.course, 10.0f));
  int16_t v = kNoI16;
  if (isfinite(s.vario)) { float x = roundf(s.vario * 100.0f); v = x > 32767.0f ? 32767 : x < -32767.0f ? -32767 : (int16_t)x; }
  le16(r + 21, (uint16_t)v);
  put(r, sizeof r);
}

void FlightLogger::yaw(uint32_t nowMs, float dps) {
  if (!isfinite(dps)) return;
  if (!win_) { win_ = true; winStart_ = nowMs; sum_ = 0; cnt_ = 0; }
  while (nowMs - winStart_ >= kYawGridMs) {          // the window is over: emit its mean on the grid, skip empty ones
    if (cnt_) emitYaw(winStart_ + kYawGridMs, (float)(sum_ / cnt_));
    sum_ = 0; cnt_ = 0;
    winStart_ += kYawGridMs;
    if (nowMs - winStart_ >= 4 * kYawGridMs) winStart_ = nowMs;   // a long silence: restart the grid
  }
  sum_ += dps;
  cnt_++;
}

void FlightLogger::emitYaw(uint32_t gridMs, float dps) {
  int32_t target = (int32_t)lroundf(dps / kYawUnitDps);
  if (target > 30000) target = 30000;
  if (target < -30000) target = -30000;
  if (blk_ && gridMs - lastEmit_ != kYawGridMs) flushYaw();    // a gap: the block's time base would be wrong
  if (!blk_) {
    blk_ = true;
    blkMs_ = gridMs;
    first_ = (int16_t)target;
    value_ = target;
    nSteps_ = 0;
  } else {
    int32_t diff = target - value_;
    int32_t d = diff / kYawStepUnits;
    const int32_t rem = diff % kYawStepUnits;
    if (rem * 2 >= kYawStepUnits) d++; else if (rem * 2 <= -kYawStepUnits) d--;
    if (d > 127) d = 127;
    if (d < -127) d = -127;
    value_ += d * kYawStepUnits;
    steps_[nSteps_++] = (uint8_t)(int8_t)d;
  }
  lastEmit_ = gridMs;
  if (nSteps_ == kYawBlock) flushYaw();
}

void FlightLogger::flushYaw() {
  if (!blk_) return;
  uint8_t r[8 + kYawBlock];
  r[0] = kRecYaw;
  le32(r + 1, blkMs_);
  le16(r + 5, (uint16_t)first_);
  r[7] = (uint8_t)nSteps_;
  for (int i = 0; i < nSteps_; i++) r[8 + i] = steps_[i];
  put(r, 8 + nSteps_);
  blk_ = false;
  nSteps_ = 0;
}

}  // namespace flightlog
