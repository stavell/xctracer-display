#include "platform/hw/power_nrf.h"
#include <bluefruit.h>
#include "platform/hw/watchdog.h"
#include "platform/hw/pins.h"
#include "platform/hw/pack_service.h"
#include "platform/clock.h"
#include "platform/power/clock_view.h"
#include "platform/power/retained.h"
#include "platform/power/weather.h"
#include "platform/hw/retention_nrf.h"
#include "platform/settings/box_settings.h"

static void waitRelease(uint8_t pin) {
  while (digitalRead(pin) == LOW) { watchdog::feed(); delay(10); }
  delay(50);
}

void PowerNrf::sleepNow(uint8_t pin) {
  digitalWrite(PIN_DISP_EXTCOMIN, LOW);
  digitalWrite(PIN_DISP_CS, LOW);
  waitRelease(pin);
  retention_hw::prepareSystemOff();  // the retained history stays powered
  systemOff(pin, LOW);              // wakes on the user switch
}

void PowerNrf::shutdown() {
  radios_.disconnect();
  delay(150);
  radios_.stop();
  watchdog::feed();
  delay(800);
  watchdog::feed();
  if (clockSleep_) clockSleep();
  else fullOff();
}

void PowerNrf::powerOff() {
  radios_.disconnect();
  delay(150);
  radios_.stop();
  watchdog::feed();
  delay(800);
  watchdog::feed();
  fullOff();
}

void PowerNrf::fullOff() {
  if (clockSleep_ && deps_.retention) deps_.retention->save(millis(), *deps_.weather, *deps_.clock);   // the last pressures and the time
  panel_.clear();                   // never leave a picture on a panel whose VCOM is stopped
  panel_.invalidate();
  panel_.present();
  panel_.stopVcom();                // waits for the last transfer before VCOM stops
  panel_.waitIdle();                // and never power down mid-transfer
  sleepNow(pin_);
}

// The switch wakes the sleeping loop task (the GPIOTE interrupt calls this).
static TaskHandle_t sleeper = nullptr;
static volatile uint32_t switchIrqs = 0;   // edges seen by the switch interrupt (ring below)
static void onSwitch() {
  BaseType_t woken = pdFALSE;
  switchIrqs++;
  if (sleeper) vTaskNotifyGiveFromISR(sleeper, &woken);
  portYIELD_FROM_ISR(woken);
}

// A ring of what the sleep loop did (wake reason, draws), printed as "slp ..." lines whenever a terminal is attached while
// it sleeps (plug in the cable, open the port) and again after the wake. For the bug "the sleeping clock does not advance":
// the ring says whether the loop woke, why, and whether it drew. About 0.4 kB of RAM.
namespace {
constexpr uint32_t kRing = 8;                       // small on purpose: the diag image has only ~300 bytes of heap to spare
struct SleepEv { uint32_t ms; int16_t minute; uint8_t ask64; char what; };   // what: E enter, T timeout, S switch, D draw, X exit
SleepEv ring[kRing];
uint32_t ringN = 0, nFeed = 0;                      // nFeed: the plain 4 s watchdog wake-ups (not logged one by one)
void ringAdd(char what, uint32_t ms, uint32_t asked, int minute) {
  if (what == 'T' && asked >= 4000) { nFeed++; return; }
  SleepEv& e = ring[ringN++ % kRing];
  e.ms = ms; e.ask64 = (uint8_t)(asked / 64 > 255 ? 255 : asked / 64); e.minute = (int16_t)minute; e.what = what;
}
void ringDump() {
  Serial.printf("slp ring n=%lu feeds=%lu irqs=%lu now=%lu\n", (unsigned long)ringN, (unsigned long)nFeed, (unsigned long)switchIrqs, (unsigned long)millis());
  for (uint32_t i = ringN > kRing ? ringN - kRing : 0; i < ringN; i++) {
    const SleepEv& e = ring[i % kRing];
    Serial.printf("slp %c ms=%lu ask=%u min=%d\n", e.what, (unsigned long)e.ms, (unsigned)e.ask64 * 64, (int)e.minute);
  }
}
}  // namespace

// Clock sleep, System ON; the radios are already off. The CPU sleeps in FreeRTOS's tickless idle (the RTC keeps
// millis() running) until the next minute, a press of the switch, or the watchdog's feed time. The panel keeps its
// picture and its 1 Hz VCOM toggle (a FreeRTOS timer: the only regular wake-up). No polling except the 20 ms steps
// while the switch is down.
void PowerNrf::clockSleep() {
  static const uint32_t kFeedMs = 4000;                      // the watchdog (8 s) keeps running in sleep
  if (deps_.compass) deps_.compass->enable(false);
  if (deps_.pack && !deps_.pack->loading()) deps_.pack->flash().powerDown();
  sleeper = xTaskGetCurrentTaskHandle();
  pinMode(pin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin_), onSwitch, FALLING);
  ClockSleep sm;
  sm.begin();
  ringN = 0;
  nFeed = 0;
  switchIrqs = 0;
  ringAdd('E', millis(), 0, -1);
  bool term = false;
  for (;;) {
    watchdog::feed();
    const uint32_t now = millis();
    const bool t = Serial;                                   // a terminal was attached meanwhile: show what happened so far
    if (t && !term) ringDump();
    term = t;
    deps_.battery->update(now);
    SleepIn in;
    in.nowMs = now;
    in.down = digitalRead(pin_) == LOW;
    in.battPct = deps_.battery->percent();
    in.localMinute = deps_.clock->localMinutes(deps_.settings->tzMinutes(), now);
    in.msToMinute = deps_.clock->msToNextMinute(now);
    const SleepStep st = sm.step(in);
    if (st.act == SLEEP_DRAW) {
      if (st.bar <= 0) {                                      // not while the hold counts down
        uint32_t epoch = 0;
        if (!deps_.clock->epoch(now, &epoch)) epoch = 0;
        weatherRefresh(*deps_.weather, now, *deps_.thermo, *deps_.baro, epoch);
        if (deps_.retention) deps_.retention->tick(now, *deps_.weather, *deps_.clock);
      }
      panel_.setRotation(deps_.rotation);
      panel_.clear();
      Bitmap1 bm = panel_.bitmap();
      char date[16];
      long days;
      const bool haveDate = deps_.clock->localDays(deps_.settings->tzMinutes(), now, &days);
      if (haveDate) WallClock::formatDate(days, date, sizeof date);
      clockview::draw(bm, in.localMinute, in.battPct, deps_.weather, st.bar, ClockSleep::kBarSteps, haveDate ? date : nullptr);
      panel_.invalidate();
      panel_.present();
      panel_.waitIdle();
      ringAdd('D', now, 0, in.localMinute);
    } else if (st.act == SLEEP_WAKE) {
      break;
    } else if (st.act == SLEEP_OFF) {
      detachInterrupt(digitalPinToInterrupt(pin_));
      sleeper = nullptr;
      fullOff();                                           // does not return
    } else {
      uint32_t ms = st.wakeAtMs - now;
      if ((int32_t)ms < 0) ms = 0;
      if (ms > kFeedMs) ms = kFeedMs;
      const uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
      ringAdd(n ? 'S' : 'T', millis(), ms, in.localMinute);
    }
  }
  detachInterrupt(digitalPinToInterrupt(pin_));
  sleeper = nullptr;
  ringAdd('X', millis(), 0, -1);
  if (Serial) ringDump();
  waitRelease(pin_);                                         // the hold that woke us must not reach the app as a press
  radios_.resume();
  resumed_ = true;
}

void PowerNrf::gateOnWake(uint8_t pin, uint32_t holdMs) {
  if (!(readResetReason() & POWER_RESETREAS_OFF_Msk)) return;
  pinMode(pin, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(pin) == LOW && millis() - t0 < holdMs) delay(10);
  if (digitalRead(pin) == LOW) { waitRelease(pin); return; }
  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  pinMode(PIN_DISP_CS, OUTPUT);
  sleepNow(pin);
}
