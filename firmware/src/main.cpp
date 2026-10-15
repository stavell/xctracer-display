#include <Arduino.h>
#include <SPI.h>
#include <bluefruit.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include "pins.h"
#include "protocol.h"

static const uint16_t W = 400, H = 240;
static const uint16_t BLACK = 0, WHITE = 1;
Adafruit_SharpMem display(&SPI, PIN_DISP_CS, W, H, 2000000);

BLEService dispService(DISP_SERVICE_UUID);
BLECharacteristic cmdChar(DISP_CMD_UUID);

SoftwareTimer vcomTimer;
static void vcomToggle(TimerHandle_t) { digitalToggle(PIN_DISP_EXTCOMIN); }

// Open BLIT state, written from the BLE write callback
static struct {
  bool open;
  uint16_t x, y, w, h;
  uint32_t pos;  // next pixel index in the w*h rectangle
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

static void showWaiting() {
  display.fillRect(0, 0, W, H, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(3);
  display.setCursor(20, 100);
  display.print("Waiting for host...");
  display.refresh();
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  vcomTimer.begin(500, vcomToggle);  // 500 ms toggle = 1 Hz VCOM
  vcomTimer.start();

  display.begin();
  showWaiting();

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setName(DISP_NAME);
  Bluefruit.setTxPower(0);

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
  Bluefruit.Advertising.setInterval(1600, 3200);  // 1 s - 2 s, units of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
  Serial.println("ready");
}

void loop() {
  if (flushPending) {
    flushPending = false;
    display.refresh();
  }
  delay(5);
}
