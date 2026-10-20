#include "apps/vario/hw/laptop_ble.h"
#include <string.h>
#include "apps/vario/hw/protocol.h"

static const uint16_t W = 400, H = 240;   // host frames are always 400x240 landscape

void LaptopBle::begin() {
  memset(frame_, 0xFF, sizeof frame_);
  link_.add(CMD_CLEAR, CMD_FLUSH, *this);               // 0x01..0x04
  link_.add(CMD_SET_FILTER, CMD_SET_ORIENT, cmds_);     // 0x10..0x16
  link_.add(CMD_DIAG_HANG, CMD_DIAG_OFF, cmds_);        // 0x70..0x71 (decodeCommand drops them unless DIAGNOSTICS)
}

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

// BLE callback context: draw commands fill the private frame.
void LaptopBle::onPacket(const uint8_t* d, uint16_t len, uint32_t now) {
  if (len == 0) return;
  switch (d[0]) {
    case CMD_CLEAR:
      lastDraw_ = now;
      drawn_ = true;
      memset(frame_, 0xFF, sizeof frame_);
      blit_.open = false;
      return;
    case CMD_BLIT:
      lastDraw_ = now;
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
      lastDraw_ = now;
      drawn_ = true;
      flushPending_ = true;
      return;
  }
}

bool LaptopBle::hostDrawing(uint32_t now) {
  return drawn_ && now - lastDraw_ < grace_;
}

// Loop context: the only place the frame reaches the panel.
void LaptopBle::service(uint32_t) {
  if (flushPending_.exchange(false)) panel_.showRaw(frame_);
}
