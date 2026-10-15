#include "hw/laptop_ble.h"
#include "protocol.h"

static LaptopBle* self = nullptr;
static const uint16_t W = 400, H = 240;   // host frames are always 400x240 landscape

void LaptopBle::begin() {
  self = this;
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

void LaptopBle::push(const Command& c) {
  uint8_t nh = (qh_ + 1) % QN;
  if (nh == qt_) return;       // full: drop
  q_[qh_] = c;
  qh_ = nh;
}

bool LaptopBle::pop(Command& out) {
  if (qt_ == qh_) return false;
  out = q_[qt_];
  qt_ = (qt_ + 1) % QN;
  return true;
}

void LaptopBle::blitByte(uint8_t b) {
  uint16_t stride = (blit_.w + 7) / 8;
  uint32_t total = (uint32_t)stride * blit_.h;
  if (blit_.pos >= total) return;
  uint16_t row = blit_.pos / stride;
  uint16_t col = (blit_.pos % stride) * 8;
  Adafruit_GFX& g = panel_.gfx();
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t px = col + i;
    if (px >= blit_.w) break;
    g.drawPixel(blit_.x + px, blit_.y + row, (b & (0x80 >> i)) ? 0 : 1);
  }
  blit_.pos++;
}

// BLE callback context: draw commands paint directly, the rest is copied into the queue.
void LaptopBle::handle(const uint8_t* d, uint16_t len) {
  if (len == 0) return;
  Command c;
  switch (d[0]) {
    case CMD_CLEAR:
      lastDraw_ = millis();
      panel_.setRotation(0);
      panel_.clear();
      blit_.open = false;
      break;
    case CMD_BLIT:
      lastDraw_ = millis();
      panel_.setRotation(0);
      if (len < 9) return;
      blit_.x = d[1] | (d[2] << 8);
      blit_.y = d[3] | (d[4] << 8);
      blit_.w = d[5] | (d[6] << 8);
      blit_.h = d[7] | (d[8] << 8);
      blit_.pos = 0;
      blit_.open = blit_.x < W && blit_.y < H && blit_.w && blit_.h &&
                   blit_.x + blit_.w <= W && blit_.y + blit_.h <= H;
      break;
    case CMD_DATA:
      if (!blit_.open) return;
      for (uint16_t i = 1; i < len; i++) blitByte(d[i]);
      break;
    case CMD_FLUSH:
      lastDraw_ = millis();
      flushPending_ = true;
      break;
    case CMD_SET_TZ:
      if (len >= 3) { c.type = Command::TZ; c.value = (int16_t)(d[1] | (d[2] << 8)); push(c); }
      break;
    case CMD_SIM:
      if (len >= 2) { c.type = Command::SIM; c.value = d[1] != 0; push(c); }
      break;
    case CMD_CAL:
      if (len >= 2) { c.type = Command::CAL; c.value = d[1]; push(c); }
      break;
    case CMD_PAGE:
      if (len >= 2) { c.type = Command::PAGE; c.value = d[1] != 0; push(c); }
      break;
    case CMD_XCTRC:
    case CMD_SET_FILTER: {
      size_t n = (size_t)(len - 1) < sizeof(c.text) - 1 ? (size_t)(len - 1) : sizeof(c.text) - 1;
      c.type = d[0] == CMD_XCTRC ? Command::XCTRC : Command::FILTER;
      memcpy(c.text, d + 1, n);
      c.text[n] = 0;
      push(c);
      break;
    }
  }
}

bool LaptopBle::hostDrawing(uint32_t now) {
  uint32_t t = lastDraw_;
  return t && now - t < grace_;
}

void LaptopBle::service(uint32_t) {
  if (flushPending_) {
    flushPending_ = false;
    panel_.refresh();
  }
}

void LaptopBle::disconnect() {
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
}

void LaptopBle::stopAdvertising() {
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
}
