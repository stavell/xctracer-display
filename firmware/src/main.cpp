#include <Arduino.h>
#include <SPI.h>
#include <bluefruit.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include "pins.h"
#include "protocol.h"
#include "button.h"
#include "battery.h"
#include "ui.h"
#include "vario.h"

static const uint16_t W = 400, H = 240;
static const uint16_t BLACK = 0, WHITE = 1;
static const uint32_t HOST_GRACE_MS = 10000;   // after a disconnect, keep the last host frame this long
static const uint32_t POWER_ON_HOLD_MS = 1000;

Adafruit_SharpMem display(&SPI, PIN_DISP_CS, W, H, 2000000);
Button button(PIN_BUTTON1);

BLEService dispService(DISP_SERVICE_UUID);
BLECharacteristic cmdChar(DISP_CMD_UUID);

SoftwareTimer vcomTimer;
static void vcomToggle(TimerHandle_t) { digitalToggle(PIN_DISP_EXTCOMIN); }

// ---- host drawing (BLE) -----------------------------------------------------
static struct {
  bool open;
  uint16_t x, y, w, h;
  uint32_t pos;
} blit;
static volatile bool flushPending = false;
static volatile uint32_t lastHostDrawMs = 0;   // last time the laptop sent a draw command
static char pendingFilter[24];
static volatile bool filterPending = false;

static void drawBlitByte(uint8_t b) {
  uint16_t stride = (blit.w + 7) / 8;
  uint32_t total = (uint32_t)stride * blit.h;
  if (blit.pos >= total) return;
  uint16_t row = blit.pos / stride;
  uint16_t col = (blit.pos % stride) * 8;
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t px = col + i;
    if (px >= blit.w) break;
    display.drawPixel(blit.x + px, blit.y + row, (b & (0x80 >> i)) ? BLACK : WHITE);
  }
  blit.pos++;
}

static volatile uint32_t cmdCount = 0, injCount = 0;
static void onCmdWrite(uint16_t, BLECharacteristic*, uint8_t* d, uint16_t len) {
  if (len == 0) return;
  cmdCount++;
  switch (d[0]) {
    case CMD_CLEAR:
      lastHostDrawMs = millis();
      display.setRotation(0);   // host frames are 400x240 landscape
      display.fillRect(0, 0, W, H, WHITE);
      blit.open = false;
      break;
    case CMD_BLIT:
      lastHostDrawMs = millis();
      display.setRotation(0);   // host frames are 400x240 landscape
      if (len < 9) return;
      blit.x = d[1] | (d[2] << 8);
      blit.y = d[3] | (d[4] << 8);
      blit.w = d[5] | (d[6] << 8);
      blit.h = d[7] | (d[8] << 8);
      blit.pos = 0;
      blit.open = blit.x < W && blit.y < H && blit.w && blit.h &&
                  blit.x + blit.w <= W && blit.y + blit.h <= H;
      break;
    case CMD_DATA:
      if (!blit.open) return;
      for (uint16_t i = 1; i < len; i++) drawBlitByte(d[i]);
      break;
    case CMD_FLUSH:
      lastHostDrawMs = millis();
      flushPending = true;
      break;
    case CMD_SET_TZ:
      if (len >= 3) varioSetTz((int16_t)(d[1] | (d[2] << 8)));
      break;
    case CMD_XCTRC: {
      char s[200];
      uint16_t n = min<uint16_t>(len - 1, sizeof(s) - 1);
      memcpy(s, d + 1, n);
      s[n] = 0;
      injCount++;
      varioInject(s);
      break;
    }
    case CMD_SIM:
      if (len >= 2) varioSim(d[1] != 0);
      break;
    case CMD_SET_FILTER: {
      uint16_t n = min<uint16_t>(len - 1, sizeof(pendingFilter) - 1);
      memcpy(pendingFilter, d + 1, n);
      pendingFilter[n] = 0;
      filterPending = true;
      break;
    }
  }
}

// ---- link state ---------------------------------------------------------------
static volatile uint16_t connHandle = BLE_CONN_HANDLE_INVALID;

static void onConnect(uint16_t h) { connHandle = h; }
static void onDisconnect(uint16_t, uint8_t) { connHandle = BLE_CONN_HANDLE_INVALID; }

// The vario UI owns the screen unless the laptop has been drawing recently.
static bool localActive() {
  uint32_t t = lastHostDrawMs;
  return !(t && millis() - t < HOST_GRACE_MS);
}

// ---- power ---------------------------------------------------------------------
static void waitRelease() {
  while (digitalRead(PIN_BUTTON1) == LOW) delay(10);
  delay(50);
}

static void sleepNow() {
  digitalWrite(PIN_DISP_EXTCOMIN, LOW);
  digitalWrite(PIN_DISP_CS, LOW);
  waitRelease();
  systemOff(PIN_BUTTON1, LOW);   // wakes on the user switch; wake = reset
}

static void powerOff() {
  display.setRotation(DISPLAY_ROTATION);
  uiMessage("Powering off");
  if (connHandle != BLE_CONN_HANDLE_INVALID) {
    Bluefruit.disconnect(connHandle);
    delay(150);
  }
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
  delay(800);
  uiBlank();            // never leave a picture on a panel whose VCOM is stopped
  vcomTimer.stop();
  sleepNow();
}

// Woke from System OFF: continue only if the switch is held long enough.
static void powerOnGate() {
  if (!(readResetReason() & POWER_RESETREAS_OFF_Msk)) return;
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(PIN_BUTTON1) == LOW && millis() - t0 < POWER_ON_HOLD_MS) delay(10);
  if (digitalRead(PIN_BUTTON1) == LOW) { waitRelease(); return; }
  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  pinMode(PIN_DISP_CS, OUTPUT);
  sleepNow();
}

// ---- main ------------------------------------------------------------------------
static bool listOpen = false;
static int listSel = 0;
static bool wasLocal = false;
static uint32_t lastDraw = 0;

void setup() {
  powerOnGate();
  Serial.begin(115200);
  button.begin();

  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  vcomTimer.begin(500, vcomToggle);   // 500 ms toggle = 1 Hz VCOM
  vcomTimer.start();

  display.begin();
  display.setRotation(DISPLAY_ROTATION);
  uiBegin(&display);

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  dispService.begin();
  cmdChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  cmdChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  cmdChar.setMaxLen(244);
  cmdChar.setWriteCallback(onCmdWrite);
  cmdChar.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(dispService);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(1600, 3200);   // 1 s - 2 s, units of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
  varioBegin();
  Serial.println("ready");
}

void loop() {
  ButtonEvent ev = button.poll();
  if (ev == BTN_HOLD) powerOff();

  bool local = localActive();
  if (local != wasLocal) {
    wasLocal = local;
    if (local) { display.setRotation(DISPLAY_ROTATION); uiInvalidate(); lastDraw = 0; }
  }

  if (filterPending) {
    filterPending = false;
    varioSetFilter(pendingFilter);
  }
  varioUpdate();

  if (local && listOpen) {
    int n = varioListCount();
    if (ev == BTN_SHORT) { listSel = (listSel + 1) % (n + 1); lastDraw = 0; }
    else if (ev == BTN_LONG) {
      if (listSel > 0) varioListConnect(listSel - 1);
      else varioListClose();
      listOpen = false;
      uiInvalidate();
      lastDraw = 0;
    }
    if (listSel > n) listSel = 0;
    if (listOpen && (millis() - lastDraw >= 250 || lastDraw == 0)) {
      lastDraw = millis();
      uiDrawList(listSel, button.heldMs());
    }
  } else if (local) {
    if (ev == BTN_LONG) {
      varioListOpen();
      listOpen = true;
      listSel = 0;
      lastDraw = 0;
    }
    batteryUpdate();
    uint32_t held = button.heldMs();
    uint32_t every = held >= Button::ARM_MS ? 150 : uiIntervalMs();
    if (millis() - lastDraw >= every || lastDraw == 0) {
      lastDraw = millis();
      uiDraw(held);
    }
  } else if (!local && flushPending) {
    flushPending = false;
    display.refresh();
  }
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 2000) {   // USB serial diagnostics
    lastDbg = millis();
    const VarioData& v = varioData();
    Serial.printf("dbg lines=%lu bad=%lu fresh=%d fix=%d alt=%.0f spd=%.0f vs=%+.1f hist=%d sim=%d local=%d cmds=%lu inj=%lu\n",
                  (unsigned long)varioLines(), (unsigned long)varioBadLines(), varioFresh(), v.fix,
                  v.alt, v.speedKmh, v.vario, varioHistCount(), varioSimOn(), local, (unsigned long)cmdCount, (unsigned long)injCount);
  }
  delay(10);
}
