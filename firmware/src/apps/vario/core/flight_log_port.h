#pragma once
#include <stdint.h>
#include "apps/vario/core/types.h"

// Flight recorder for validation data (apps/vario/core/flight_log.h): the app reports what it received and measured, the
// logger decides what to keep. Never blocks, never changes what the app does. Optional (nullptr = no logging).
class IFlightLog {
 public:
  virtual ~IFlightLog() {}
  virtual void sessionStart(uint32_t nowMs) = 0;            // the vario app was opened
  virtual void sessionEnd(uint32_t nowMs) = 0;              // ... and left
  virtual void line(uint32_t nowMs, const Sample& s) = 0;   // a tracer line was parsed, nowMs = when it came in
  virtual void yaw(uint32_t nowMs, float cwDps) = 0;        // turn rate about the vertical, clockwise, at a sensor read
};
