#include "platform/storage/pack_proto.h"
#include <string.h>
#include "platform/terrain/crc32.h"

namespace storage {

static uint32_t frameCrc(uint8_t cmd, const uint8_t* payload, size_t len) {
  uint8_t h[3] = {cmd, (uint8_t)len, (uint8_t)(len >> 8)};
  uint32_t c = terrain::crc32(0, h, 3);
  return len ? terrain::crc32(c, payload, len) : c;
}

size_t encodeFrame(uint8_t* out, size_t cap, uint8_t cmd, const uint8_t* payload, size_t len) {
  if (len > kMaxPayload || cap < len + 9) return 0;
  out[0] = kSync0; out[1] = kSync1; out[2] = cmd; out[3] = (uint8_t)len; out[4] = (uint8_t)(len >> 8);
  if (len) memcpy(out + 5, payload, len);
  uint32_t c = frameCrc(cmd, payload, len);
  for (int i = 0; i < 4; i++) out[5 + len + i] = (uint8_t)(c >> (8 * i));
  return len + 9;
}

FrameParser::Result FrameParser::feed(uint8_t b, uint32_t nowMs) {
  if (st_ != S0 && nowMs - last_ > kFrameGapMs) st_ = S0;   // half a frame, then silence
  last_ = nowMs;
  switch (st_) {
    case S0: if (b == kSync0) st_ = S1; break;
    case S1: st_ = b == kSync1 ? CMD : (b == kSync0 ? S1 : S0); break;
    case CMD: cmd_ = b; st_ = L0; break;
    case L0: len_ = b; st_ = L1; break;
    case L1:
      len_ |= (uint32_t)b << 8;
      pos_ = 0;
      if (len_ > kMaxPayload) { st_ = S0; return BAD; }
      st_ = len_ ? DATA : C0;
      break;
    case DATA:
      buf_[pos_++] = b;
      if (pos_ == len_) st_ = C0;
      break;
    case C0: crc_ = b; st_ = C1; break;
    case C1: crc_ |= (uint32_t)b << 8; st_ = C2; break;
    case C2: crc_ |= (uint32_t)b << 16; st_ = C3; break;
    case C3:
      crc_ |= (uint32_t)b << 24;
      st_ = S0;
      return crc_ == frameCrc(cmd_, buf_, len_) ? FRAME : BAD;
  }
  return NONE;
}

}  // namespace storage
