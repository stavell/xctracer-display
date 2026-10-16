#pragma once
// Storage port of the terrain/map code: random-access reads of the map pack. On the device this will be the
// QSPI flash (or a memory-mapped XIP window); on the PC a memory image or a file. The reader never sees
// more than a few kB at once and must not allocate. Pure C++.
#include <stdint.h>
#include <string.h>

namespace terrain {

struct IBlockReader {
  virtual ~IBlockReader() {}
  // Copy len bytes starting at byte `offset` of the pack into dst. False if the range is not readable.
  virtual bool read(uint32_t offset, void* dst, uint32_t len) = 0;
};

// A pack held in memory (tests, emulator, XIP flash). Does not own the bytes.
struct MemReader : IBlockReader {
  const uint8_t* data;
  uint32_t size;
  uint32_t reads = 0;                      // calls, for tests
  MemReader(const uint8_t* d = nullptr, uint32_t n = 0) : data(d), size(n) {}
  bool read(uint32_t offset, void* dst, uint32_t len) override {
    reads++;
    if (!data || offset > size || len > size - offset) return false;
    memcpy(dst, data + offset, len);
    return true;
  }
};

}  // namespace terrain
