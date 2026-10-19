#include "platform/hw/power_nrf.h"
#include <bluefruit.h>
#include "platform/hw/watchdog.h"
#include "platform/hw/pins.h"
#include "platform/hw/pack_service.h"
#include "platform/clock.h"
#include "platform/power/clock_view.h"
#include "platform/settings/box_settings.h"

static void waitRelease(uint8_t pin) {
  while (digitalRead(pin) == LOW) { watchdog::feed(); delay(10); }
  delay(50);
}

void PowerNrf::sleepNow(uint8_t pin) {
  digitalWrite(PIN_DISP_EXTCOMIN, LOW);
  digitalWrite(PIN_DISP_CS, LOW);
  waitRelease(pin);
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

void PowerNrf::fullOff() {
  panel_.clear();                   // never leave a picture on a panel whose VCOM is stopped
  panel_.invalidate();
  panel_.present();
  panel_.stopVcom();                // waits for the last transfer before VCOM stops
  panel_.waitIdle();                // and never power down mid-transfer
  sleepNow(pin_);
}

// The switch wakes the sleeping loop task (the GPIOTE interrupt calls this).
static TaskHandle_t sleeper = nullptr;
static void onSwitch() {
  BaseType_t woken = pdFALSE;
  if (sleeper) vTaskNotifyGiveFromISR(sleeper, &woken);
  portYIELD_FROM_ISR(woken);
}

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
  for (;;) {
    watchdog::feed();
    const uint32_t now = millis();
    deps_.battery->update(now);
    SleepIn in;
    in.nowMs = now;
    in.down = digitalRead(pin_) == LOW;
    in.battPct = deps_.battery->percent();
    in.localMinute = deps_.clock->localMinutes(deps_.settings->tzMinutes(), now);
    in.msToMinute = deps_.clock->msToNextMinute(now);
    const SleepStep st = sm.step(in);
    if (st.act == SLEEP_DRAW) {
      panel_.setRotation(deps_.rotation);
      panel_.clear();
      Bitmap1 bm = panel_.bitmap();
      clockview::draw(bm, in.localMinute, in.battPct, st.bar, ClockSleep::kBarSteps);
      panel_.invalidate();
      panel_.present();
      panel_.waitIdle();
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
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
    }
  }
  detachInterrupt(digitalPinToInterrupt(pin_));
  sleeper = nullptr;
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
