#pragma once
// A position from outside the platform: in the box, the XC Tracer's GPS through the vario's tracer link (apps/vario/app/tracer_fix.h).
// An app that only wants "where am I" (the Map) takes this port and never sees the tracer, its protocol or the vario.
#include <math.h>
#include <stdint.h>

struct GeoFix {
  double lat = 0, lon = 0;
  float courseDeg = NAN, speedKmh = NAN, altM = NAN;   // NaN: not known
  uint32_t atMs = 0;                                   // host time it arrived
};

class IFixSource {
 public:
  virtual ~IFixSource() {}
  virtual void start() = 0;                            // look for the source (the tracer link)
  virtual void stop() = 0;                             // and give it up again
  virtual void poll(uint32_t nowMs) = 0;               // every tick: take what arrived
  virtual bool linked() const = 0;                     // the source is connected (it may still have no GPS fix)
  virtual bool latest(GeoFix* out) const = 0;          // the newest fix; false when none came since start()
};
