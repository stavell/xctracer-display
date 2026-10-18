#pragma once
// Port to a NOR flash chip (the Feather's on-board 2 MB QSPI flash on the device, an in-memory mock in the tests).
// Pure C++. NOR rules: erase sets bytes to 0xFF in blocks, programming can only clear bits, so a range must be
// erased before it is programmed with other data (re-programming the same data is harmless).
#include <stdint.h>

namespace storage {

constexpr uint32_t kSectorBytes = 4096;
constexpr uint32_t kBlockBytes = 65536;          // the larger erase unit: several times faster per byte than 4 kB sectors

struct IFlash {
  virtual ~IFlash() {}
  virtual uint32_t jedecId() = 0;                // manufacturer << 16 | type << 8 | capacity code; 0 if the chip does not answer
  virtual uint32_t capacity() const = 0;         // bytes
  // Any address and length, no alignment needed. False if the range is outside the chip or the transfer failed.
  virtual bool read(uint32_t addr, void* dst, uint32_t len) = 0;
  // Any address and length (it may cross pages); returns when the data is in the chip. False on a bad range.
  virtual bool program(uint32_t addr, const void* src, uint32_t len) = 0;
  // Start erasing the 4 kB sector (size = kSectorBytes) or the 64 kB block (size = kBlockBytes) at addr, which must be
  // aligned to size. Returns at once; busy() says when it is over. False if busy, misaligned or out of range.
  virtual bool startErase(uint32_t addr, uint32_t size) = 0;
  virtual bool busy() = 0;                       // an erase (or program) is still running inside the chip
};

}  // namespace storage
