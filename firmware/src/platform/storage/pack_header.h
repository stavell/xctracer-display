#pragma once
// Where the map pack sits in the flash and the 32-byte header in front of it. Pure C++.
//
//   0      header sector: 32 bytes used, the rest stays erased
//   4096   the pack (host/terrain/FORMAT.md), packLen bytes
//
// Header (little endian): "BGFL", u16 version = 1, u16 header size = 32, u32 pack offset = 4096, u32 pack length,
// u32 CRC-32 of the whole pack (the raw bytes as stored), 8 zero bytes, u32 CRC-32 of the first 28 bytes.
// The header is written LAST by the loader, so a load that is interrupted leaves a blank header: "no pack", never
// half a pack. The loader also checks that the pack's own header (BGMP) carries the same file size.
#include <stdint.h>
#include "platform/storage/flash_port.h"

namespace storage {

constexpr uint32_t kPackBase = kSectorBytes;
constexpr uint32_t kFlashHeaderBytes = 32;
constexpr uint16_t kFlashHeaderVersion = 1;
constexpr uint32_t kMinPackBytes = 192;          // the pack's own header

struct FlashHeader {
  uint32_t packLen = 0;
  uint32_t packCrc = 0;
};

enum HeaderStatus : uint8_t { HDR_OK, HDR_BLANK, HDR_BAD };
const char* headerStatusName(HeaderStatus s);

void encodeHeader(uint8_t out[kFlashHeaderBytes], const FlashHeader& h);
// HDR_BLANK: all 0xFF (erased, never loaded). HDR_BAD: anything else that is not a valid header for a chip of
// `capacity` bytes (magic, version, size, base, length range, CRC).
HeaderStatus parseHeader(const uint8_t in[kFlashHeaderBytes], uint32_t capacity, FlashHeader* out);
// Read and parse the header at address 0.
HeaderStatus readHeader(IFlash& flash, FlashHeader* out);
// Does the pack's own header (first bytes at kPackBase) say "BGMP", version 1 and a file size equal to packLen?
bool packHeaderConsistent(IFlash& flash, uint32_t packLen);

// CRC-32 over a flash range in slices, so a long check never blocks the loop for long.
class CrcScan {
 public:
  void begin(IFlash* flash, uint32_t addr, uint32_t len) { flash_ = flash; addr_ = addr; left_ = len; crc_ = 0; failed_ = false; }
  // Process up to maxBytes; true when the whole range has been read (or a read failed: check failed()).
  bool step(uint32_t maxBytes);
  bool done() const { return left_ == 0 || failed_; }
  bool failed() const { return failed_; }
  uint32_t crc() const { return crc_; }
  uint32_t remaining() const { return left_; }

 private:
  IFlash* flash_ = nullptr;
  uint32_t addr_ = 0, left_ = 0, crc_ = 0;
  bool failed_ = false;
  uint8_t buf_[256];
};

}  // namespace storage
