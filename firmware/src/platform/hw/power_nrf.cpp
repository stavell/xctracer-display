#include "platform/hw/power_nrf.h"
#include <bluefruit.h>
#include "platform/hw/watchdog.h"
#include "platform/hw/pins.h"

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
  panel_.clear();                   // never leave a picture on a panel whose VCOM is stopped
  panel_.invalidate();
  panel_.present();
  panel_.stopVcom();                // waits for the last transfer before VCOM stops
  panel_.waitIdle();                // and never power down mid-transfer
  sleepNow(pin_);
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
