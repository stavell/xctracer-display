#include "platform/storage/pack_header.h"
#include <string.h>
#include "platform/terrain/crc32.h"

namespace storage {

static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const char* headerStatusName(HeaderStatus s) { return s == HDR_OK ? "ok" : s == HDR_BLANK ? "blank" : "bad"; }

void encodeHeader(uint8_t out[kFlashHeaderBytes], const FlashHeader& h) {
  memset(out, 0, kFlashHeaderBytes);
  memcpy(out, "BGFL", 4);
  out[4] = kFlashHeaderVersion & 255; out[5] = kFlashHeaderVersion >> 8;
  out[6] = kFlashHeaderBytes;
  put32(out + 8, kPackBase);
  put32(out + 12, h.packLen);
  put32(out + 16, h.packCrc);
  put32(out + 28, terrain::crc32(0, out, 28));
}

HeaderStatus parseHeader(const uint8_t in[kFlashHeaderBytes], uint32_t capacity, FlashHeader* out) {
  bool blank = true;
  for (uint32_t i = 0; i < kFlashHeaderBytes; i++) if (in[i] != 0xFF) { blank = false; break; }
  if (blank) return HDR_BLANK;
  if (memcmp(in, "BGFL", 4) != 0) return HDR_BAD;
  if ((in[4] | in[5] << 8) != kFlashHeaderVersion || (in[6] | in[7] << 8) != (int)kFlashHeaderBytes) return HDR_BAD;
  if (get32(in + 8) != kPackBase) return HDR_BAD;
  if (get32(in + 28) != terrain::crc32(0, in, 28)) return HDR_BAD;
  uint32_t len = get32(in + 12);
  if (len < kMinPackBytes || capacity < kPackBase || len > capacity - kPackBase) return HDR_BAD;
  if (out) { out->packLen = len; out->packCrc = get32(in + 16); }
  return HDR_OK;
}

HeaderStatus readHeader(IFlash& flash, FlashHeader* out) {
  uint8_t b[kFlashHeaderBytes];
  if (!flash.read(0, b, sizeof b)) return HDR_BAD;
  return parseHeader(b, flash.capacity(), out);
}

bool packHeaderConsistent(IFlash& flash, uint32_t packLen) {
  uint8_t b[16];
  if (!flash.read(kPackBase, b, sizeof b)) return false;
  return memcmp(b, "BGMP", 4) == 0 && (b[4] | b[5] << 8) == 1 && get32(b + 8) == packLen;
}

bool CrcScan::step(uint32_t maxBytes) {
  while (maxBytes > 0 && left_ > 0 && !failed_) {
    uint32_t n = left_ < sizeof buf_ ? left_ : (uint32_t)sizeof buf_;
    if (n > maxBytes) n = maxBytes;
    if (!flash_ || !flash_->read(addr_, buf_, n)) { failed_ = true; break; }
    crc_ = terrain::crc32(crc_, buf_, n);
    addr_ += n; left_ -= n; maxBytes -= n;
  }
  return done();
}

}  // namespace storage
