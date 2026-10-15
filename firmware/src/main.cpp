#include <Arduino.h>
#include <SPI.h>
#include <bluefruit.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include "pins.h"
#include "protocol.h"
#include "button.h"
#include "sensors.h"
#include "ui.h"

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

static void onCmdWrite(uint16_t, BLECharacteristic*, uint8_t* d, uint16_t len) {
  if (len == 0) return;
  switch (d[0]) {
    case CMD_CLEAR:
      display.fillRect(0, 0, W, H, WHITE);
      blit.open = false;
      break;
    case CMD_BLIT:
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
      flushPending = true;
      break;
  }
}

// ---- link state ---------------------------------------------------------------
static volatile bool hostConnected = false;
static volatile uint16_t connHandle = BLE_CONN_HANDLE_INVALID;
static uint32_t lastDisconnectMs = 0;
static bool everConnected = false;

static void onConnect(uint16_t h) { connHandle = h; hostConnected = true; everConnected = true; }
static void onDisconnect(uint16_t, uint8_t) {
  hostConnected = false;
  connHandle = BLE_CONN_HANDLE_INVALID;
  lastDisconnectMs = millis();
}

static bool localActive() {
  if (hostConnected) return false;
  if (!everConnected) return true;
  return millis() - lastDisconnectMs >= HOST_GRACE_MS;
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
  uiMessage("Powering off");
  sensorsSleep();
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
static Page page = PAGE_ENV;
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
  display.setRotation(0);
  uiBegin(&display);
  sensorsBegin();

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
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
  Serial.println("ready");
}

void loop() {
  ButtonEvent ev = button.poll();
  if (ev == BTN_HOLD) powerOff();

  bool local = localActive();
  if (local != wasLocal) {
    wasLocal = local;
    if (local) {
      sensorsSetPage(page);
      uiInvalidate();
      lastDraw = 0;
    } else {
      sensorsSleep();
    }
  }

  if (local) {
    if (ev == BTN_SHORT) {
      if (page == PAGE_COMPASS && calState().active) calFinish(false);
      else page = (Page)((page + 1) % PAGE_COUNT);
      sensorsSetPage(page);
      lastDraw = 0;
    } else if (ev == BTN_LONG) {
      if (page == PAGE_COMPASS) {
        if (calState().active) calFinish(true);
        else calStart();
      } else {
        page = (Page)((page + PAGE_COUNT - 1) % PAGE_COUNT);
        sensorsSetPage(page);
      }
      lastDraw = 0;
    }
    sensorsUpdate();
    uint32_t held = button.heldMs();
    uint32_t every = held >= Button::ARM_MS ? 150 : uiIntervalMs(page);
    if (millis() - lastDraw >= every || lastDraw == 0) {
      lastDraw = millis();
      uiDraw(page, held);
    }
  } else if (flushPending) {
    flushPending = false;
    display.refresh();
  }
  delay(10);
}
