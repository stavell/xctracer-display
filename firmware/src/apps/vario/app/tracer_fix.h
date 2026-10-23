#pragma once
// The XC Tracer as a platform position source (platform/fix_source.h), for apps that only want "where am I" (the Map).
// It drives the same tracer link the vario uses (resume / pause, as the vario's begin / end do) and decodes the $XCTRC lines.
// Only one of them runs at a time (one active app), so they never compete for the line queue.
#include "apps/vario/app/ports.h"
#include "apps/vario/core/xctrc.h"
#include "platform/fix_source.h"

class TracerFix : public IFixSource {
 public:
  explicit TracerFix(ITracer& t) : t_(t) {}
  void start() override { have_ = false; t_.resume(); }
  void stop() override { t_.pause(); have_ = false; }
  void poll(uint32_t nowMs) override {
    t_.update(nowMs);
    char line[200];                                  // the vario's line buffer size; small enough for the loop stack
    while (t_.popLine(line, sizeof line)) {
      Sample s;
      if (!xctrcParse(line, s) || !s.fix) continue;  // no GPS fix: lat = lon = 0
      fix_.lat = s.lat;
      fix_.lon = s.lon;
      fix_.courseDeg = s.course;
      fix_.speedKmh = s.speedKmh;
      fix_.altM = s.alt;
      fix_.atMs = nowMs;
      have_ = true;
    }
  }
  bool linked() const override { return t_.link() == ViewModel::LINK_CONNECTED; }
  bool latest(GeoFix* out) const override {
    if (have_) *out = fix_;
    return have_;
  }

 private:
  ITracer& t_;
  GeoFix fix_;
  bool have_ = false;
};
