#include "hw/panel_sharp.h"
#include <Arduino.h>
#include <SPI.h>
#include "pins.h"
#include "core/linediff.h"

static const size_t BUF_LEN = 400 * 240 / 8;
// More changed lines than this: the full refresh is as good and keeps the common path simple.
static const int FULL_REFRESH_LINES = 200;

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
  if (dirty_ || !snap_) { refresh(); return; }
  uint8_t* buf = display_.getBuffer();
  static uint8_t idx[LD_LINES];
  int n = changedLines(buf, snap_, idx);
  if (n == 0) return;
  if (n > FULL_REFRESH_LINES) { refresh(); return; }
  uint32_t t0 = micros();
  sendLines(idx, n);
  refreshUs_ = micros() - t0;
  for (int i = 0; i < n; i++) {
    size_t o = (size_t)idx[i] * LD_BYTES_PER_LINE;
    memcpy(snap_ + o, buf + o, LD_BYTES_PER_LINE);
  }
  lastLines_ = n;
}

// Same wire format as Adafruit_SharpMem::refresh(), restricted to the given lines, one transaction:
//   SPISettings(2 MHz, LSBFIRST, SPI_MODE0); CS (active HIGH) raised for the whole transaction;
//   command = _sharpmem_vcom | SHARPMEM_BIT_WRITECMD, then the vcom bit is toggled;
//   per line: address byte (y + 1, as the library's ((i + 1) / 50) + 1 with i = y * 50), 50 data
//   bytes, 0x00; then one final 0x00. LSBFIRST in the SPI settings does the bit reversal.
// VCOM bit: the library's _sharpmem_vcom is private and toggles once per refresh()/clearDisplay().
// We keep a mirror (same start value, SHARPMEM_BIT_VCOM) and toggle it after every command we send and
// after every library refresh() (see refresh()), so the bit alternates over the mixed sequence of
// partial and full writes exactly as if only the library were writing. clearDisplay() is never called.
void SharpPanel::sendLines(const uint8_t* idx, int n) {
  const uint8_t* buf = display_.getBuffer();
  SPI.beginTransaction(SPISettings(2000000, LSBFIRST, SPI_MODE0));
  digitalWrite(PIN_DISP_CS, HIGH);
  SPI.transfer(vcomBit_ | SHARPMEM_BIT_WRITECMD);
  vcomBit_ = vcomBit_ ? 0x00 : SHARPMEM_BIT_VCOM;
  for (int i = 0; i < n; i++) {
    uint8_t line[LD_BYTES_PER_LINE + 2];
    line[0] = (uint8_t)(idx[i] + 1);
    memcpy(line + 1, buf + (size_t)idx[i] * LD_BYTES_PER_LINE, LD_BYTES_PER_LINE);
    line[LD_BYTES_PER_LINE + 1] = 0x00;
    SPI.transfer(line, LD_BYTES_PER_LINE + 2);
  }
  SPI.transfer(0x00);
  digitalWrite(PIN_DISP_CS, LOW);
  SPI.endTransaction();
}

void SharpPanel::refresh() {
  uint32_t t0 = micros();
  display_.refresh();
  vcomBit_ = vcomBit_ ? 0x00 : SHARPMEM_BIT_VCOM;   // the library toggled its own copy
  refreshUs_ = micros() - t0;
  if (snap_) memcpy(snap_, display_.getBuffer(), BUF_LEN);
  dirty_ = false;
  lastLines_ = LD_LINES;
}

void SharpPanel::showRaw(const uint8_t* frame) {
  memcpy(display_.getBuffer(), frame, BUF_LEN);
  refresh();
}

void SharpPanel::stopVcom() {
  vcom_.stop();
  digitalWrite(PIN_DISP_EXTCOMIN, LOW);
}
