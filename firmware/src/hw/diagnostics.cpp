#include "hw/diagnostics.h"
#include <Arduino.h>
#ifdef DIAGNOSTICS
#include "utility/debug.h"
#endif
#include "app/app.h"
#include "hw/compass_lis.h"
#include "hw/panel_sharp.h"
#include "core/compass.h"
#include "core/flight.h"
#include "core/wind.h"
#include "core/xctrc.h"

static App* gApp;
static SharpPanel* gPanel;
static CompassLis* gCompass;

void diagnosticsBegin(App& app, SharpPanel& panel, CompassLis& compass) {
  gApp = &app;
  gPanel = &panel;
  gCompass = &compass;
}

#ifdef DIAGNOSTICS
// Timing of the numerically heavy pieces on this chip (diagnostics only).
static uint32_t benchParseUs = 0, benchWindUs = 0, benchVmUs = 0;
static void runBench() {
  static TrackPoint pts[30];
  static bool init = false;
  if (!init) {                                           // a 30 s circle in a 12 km/h wind
    for (int i = 0; i < 30; i++) {
      double hdg = (40 + 18 * i) * 0.017453292519943295;
      double gx = 10.0 * sin(hdg) - 3.33, gy = 10.0 * cos(hdg);
      pts[i] = {true, 43.0, 26.0, 1400, 1.5f, (float)(hypot(gx, gy) * 3.6), (float)fmod(atan2(gx, gy) * 57.29577951308232 + 360, 360)};
    }
    init = true;
  }
  uint32_t t0 = micros();
  Sample s;
  bool ok = xctrcParse("$XCTRC,2026,8,16,10,38,24,0,43.252650,26.927967,1387.00,34.13,12.5,-1.30,,,,967.62,85*7c", s);
  uint32_t t1 = micros();
  WindFit f = fitWind(pts, 30);
  uint32_t t2 = micros();
  (void)ok; (void)f;
  benchParseUs = t1 - t0;
  benchWindUs = t2 - t1;
}

// One-off timing of the whole data path: 120 s of a circling flight (wind fit, circling detection, thermal
// finder and memory run every second) through a private FlightModel, plus a compass update.
static FlightModel benchFm;
static uint32_t benchFeedAvgUs = 0, benchFeedMaxUs = 0, benchCompassUs = 0;
static bool benchDone = false;
static void runFlightBench() {
  if (benchDone) return;
  benchDone = true;
  uint32_t sum = 0, mx = 0;
  const double D2R = 0.017453292519943295;
  double x = 0, y = 0, hdg = 40;
  for (int i = 0; i < 120; i++, hdg += 18) {
    double gx = 10.0 * sin(hdg * D2R) - 3.33, gy = 10.0 * cos(hdg * D2R);
    x += gx; y += gy;
    Sample s;
    s.fix = true; s.lat = 43 + y / 111320.0; s.lon = 26 + x / (111320.0 * cos(43 * D2R));
    s.speedKmh = (float)(hypot(gx, gy) * 3.6); s.course = (float)fmod(atan2(gx, gy) / D2R + 360, 360);
    s.vario = 2.5f; s.alt = 1400.0f + i; s.hh = 12; s.mm = i / 60; s.ss = i % 60;
    uint32_t t0 = micros();
    benchFm.feed(s);
    uint32_t dt = micros() - t0;
    sum += dt;
    if (dt > mx) mx = dt;
  }
  benchFeedAvgUs = sum / 120;
  benchFeedMaxUs = mx;
  Compass c;
  Vec3 a, mg;
  a.x = 0.5f; a.y = 0.3f; a.z = 9.7f; mg.x = 20; mg.y = -10; mg.z = -35;
  uint32_t t0 = micros();
  for (int i = 0; i < 10; i++) c.update(a, mg);
  benchCompassUs = (micros() - t0) / 10;
}
#else
static uint32_t benchParseUs = 0, benchWindUs = 0, benchVmUs = 0, benchFeedAvgUs = 0, benchFeedMaxUs = 0, benchCompassUs = 0;
static void runBench() {}
static void runFlightBench() {}
#endif

void diagnosticsTick(uint32_t now, uint32_t& worstTickUs) {
  App& app = *gApp;
  SharpPanel& panel = *gPanel;
  CompassLis& compassHw = *gCompass;
  static uint32_t last = 0;
  if (now - last < 2000) return;
  last = now;
  if (!Serial || Serial.availableForWrite() < 120) return;
  runBench();
  runFlightBench();
  uint32_t v0 = micros();
  const ViewModel& vm = app.buildViewModel(now);
#ifdef DIAGNOSTICS
  benchVmUs = micros() - v0;
#else
  (void)v0;
#endif
  Serial.printf("st ok=%lu bad=%lu fresh=%d fix=%d alt=%.0f spd=%.0f vs=%+.1f wind=%d/%.0f sim=%d link=%d pg=%d marks=%d trail=%d tick=%luus refresh=%luus parse=%luus windfit=%luus viewmodel=%luus feed_avg=%luus feed_max=%luus compass=%luus",
                (unsigned long)app.linesOk(), (unsigned long)app.linesBad(), vm.fresh, vm.s.fix,
                vm.s.alt, vm.s.speedKmh, vm.s.vario, vm.wind.valid, vm.wind.speedKmh, vm.sim, (int)vm.link,
                (int)vm.page, vm.markCount, vm.trailCount, (unsigned long)worstTickUs, (unsigned long)panel.refreshUs(),
                (unsigned long)benchParseUs, (unsigned long)benchWindUs, (unsigned long)benchVmUs,
                (unsigned long)benchFeedAvgUs, (unsigned long)benchFeedMaxUs, (unsigned long)benchCompassUs);
#ifdef DIAGNOSTICS
  Serial.printf(" up=%lu heap=%d", (unsigned long)(millis() / 1000), dbgHeapUsed());
#endif
  Serial.printf(" lines=%d", panel.lastLines());
  Serial.print("\n");
  char desc[120];
  app.describeTracer(desc, sizeof desc);
  // reset cause (POWER->RESETREAS at boot: 0x1 pin, 0x2 watchdog, 0x4 soft reset incl. hard fault,
  // 0x10000 wake from System OFF; 0 = power-on) and the loop task's smallest free stack so far, in bytes
  Serial.printf("dev %s rst=0x%lx loopstack=%lu\nraw %s\n", desc, (unsigned long)readResetReason(),
                (unsigned long)uxTaskGetStackHighWaterMark(nullptr) * 4, app.lastRaw());
  const Vec3& ca = compassHw.lastAccel();
  const Vec3& cm = compassHw.lastMag();
  Serial.printf("cmp on=%d present=%d acc=%.2f,%.2f,%.2f mag=%.1f,%.1f,%.1f hdg=%.1f src=%d cal=%d\n", app.compassOn(),
                compassHw.present(), ca.x, ca.y, ca.z, cm.x, cm.y, cm.z, vm.heading, (int)vm.headingSrc, vm.calValid);
  worstTickUs = 0;
}

