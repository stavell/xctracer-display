#include "hw/laptop_ble.h"
#include <string.h>
#include "app/command_decode.h"
#include "protocol.h"

static LaptopBle* self = nullptr;
static const uint16_t W = 400, H = 240;   // host frames are always 400x240 landscape

void LaptopBle::begin() {
  self = this;
  memset(frame_, 0xFF, sizeof frame_);
  svc_.begin();
  chr_.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  chr_.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr_.setMaxLen(244);
  chr_.setWriteCallback(onWrite);
  chr_.begin();
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(svc_);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(1600, 3200);   // 1 s - 2 s, units of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void LaptopBle::onConnect(uint16_t h) { if (self) self->conn_ = h; }
void LaptopBle::onDisconnect(uint16_t, uint8_t) { if (self) self->conn_ = BLE_CONN_HANDLE_INVALID; }
void LaptopBle::onWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) { if (self) self->handle(data, len); }

// Panel native layout (Adafruit_SharpMem::drawPixel, rotation 0): byte (y*400 + x)/8, bit (x & 7),
// LSB = leftmost pixel, set = white. Host data: MSB first, 1 = black.
void LaptopBle::blitByte(uint8_t b) {
  uint16_t stride = (blit_.w + 7) / 8;
  uint32_t total = (uint32_t)stride * blit_.h;
  if (blit_.pos >= total) return;
  uint16_t row = blit_.pos / stride;
  uint16_t col = (blit_.pos % stride) * 8;
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t px = col + i;
    if (px >= blit_.w) break;
    uint16_t x = blit_.x + px, y = blit_.y + row;
    uint8_t* p = &frame_[((uint32_t)y * W + x) / 8];
    uint8_t m = (uint8_t)(1u << (x & 7));
    if (b & (0x80 >> i)) *p &= (uint8_t)~m; else *p |= m;
  }
  blit_.pos++;
}

// BLE callback context: draw commands fill the private frame, the rest is decoded and queued.
void LaptopBle::handle(const uint8_t* d, uint16_t len) {
  if (len == 0) return;
  switch (d[0]) {
    case CMD_CLEAR:
      lastDraw_ = millis();
      drawn_ = true;
      memset(frame_, 0xFF, sizeof frame_);
      blit_.open = false;
      return;
    case CMD_BLIT:
      lastDraw_ = millis();
      drawn_ = true;
      if (len < 9) return;
      blit_.x = d[1] | (d[2] << 8);
      blit_.y = d[3] | (d[4] << 8);
      blit_.w = d[5] | (d[6] << 8);
      blit_.h = d[7] | (d[8] << 8);
      blit_.pos = 0;
      blit_.open = blit_.x < W && blit_.y < H && blit_.w && blit_.h &&
                   blit_.x + blit_.w <= W && blit_.y + blit_.h <= H;
      return;
    case CMD_DATA:
      if (!blit_.open) return;
      for (uint16_t i = 1; i < len; i++) blitByte(d[i]);
      return;
    case CMD_FLUSH:
      lastDraw_ = millis();
      drawn_ = true;
      flushPending_ = true;
      return;
  }
  Command c;
  if (decodeCommand(d, len, c)) q_.push(c);
}

bool LaptopBle::hostDrawing(uint32_t now) {
  return drawn_ && now - lastDraw_ < grace_;
}

// Loop context: the only place the frame reaches the panel.
void LaptopBle::service(uint32_t) {
  if (flushPending_.exchange(false)) panel_.showRaw(frame_);
}

void LaptopBle::disconnect() {
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
}

void LaptopBle::stopAdvertising() {
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
}
