#include "hw/panel_sharp.h"
#include <Arduino.h>
#include <SPI.h>
#include "pins.h"

static const size_t BUF_LEN = 400 * 240 / 8;

static void vcomToggle(TimerHandle_t) { digitalToggle(PIN_DISP_EXTCOMIN); }

SharpPanel::SharpPanel() : display_(&SPI, PIN_DISP_CS, 400, 240, 2000000) {}

void SharpPanel::begin() {
  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  vcom_.begin(500, vcomToggle);        // 500 ms toggle = 1 Hz VCOM
  vcom_.start();
  display_.begin();
  snap_ = (uint8_t*)malloc(BUF_LEN);
  dirty_ = true;
}

void SharpPanel::present() {
  uint8_t* buf = display_.getBuffer();
  if (!dirty_ && snap_ && memcmp(buf, snap_, BUF_LEN) == 0) return;
  refresh();
}

void SharpPanel::refresh() {
  uint32_t t0 = micros();
  display_.refresh();
  refreshUs_ = micros() - t0;
  if (snap_) memcpy(snap_, display_.getBuffer(), BUF_LEN);
  dirty_ = false;
}

void SharpPanel::showRaw(const uint8_t* frame) {
  memcpy(display_.getBuffer(), frame, BUF_LEN);
  refresh();
}

void SharpPanel::stopVcom() {
  vcom_.stop();
  digitalWrite(PIN_DISP_EXTCOMIN, LOW);
}
