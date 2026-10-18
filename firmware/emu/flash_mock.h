#pragma once
// An in-memory NOR flash for the tests of platform/storage (the PC player and the loader tests use it). Enforces the
// NOR rules: erase sets 0xFF, program only clears bits. Erases take a few busy() polls, like the real chip.
#include <stdint.h>
#include <string.h>
#include <vector>
#include "platform/storage/flash_port.h"

struct MemFlash : storage::IFlash {
  std::vector<uint8_t> mem;
  int busyLeft = 0;
  int erasePolls = 3;                                  // busy() answers true this many times after an erase starts
  uint32_t sectorErases = 0, blockErases = 0, programs = 0;
  long flipAt = -1;                                    // fault injection: the programmed byte at this address gets bit 0 cleared
  bool failProgram = false, failRead = false;
  bool programmedOverNonErased = false;                // a program tried to set a 0 bit back to 1 (caller bug)

  explicit MemFlash(uint32_t cap = 2u * 1024 * 1024) : mem(cap, 0xFF) {}
  uint32_t jedecId() override { return 0xC84015; }     // GD25Q16
  uint32_t capacity() const override { return (uint32_t)mem.size(); }
  bool read(uint32_t a, void* dst, uint32_t n) override {
    if (failRead || a > mem.size() || n > mem.size() - a) return false;
    memcpy(dst, mem.data() + a, n);
    return true;
  }
  bool program(uint32_t a, const void* src, uint32_t n) override {
    if (failProgram || a > mem.size() || n > mem.size() - a) return false;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) {
      uint8_t v = mem[a + i] & s[i];
      if (v != s[i] && mem[a + i] != 0xFF) programmedOverNonErased = true;
      if ((long)(a + i) == flipAt) v &= 0xFE;
      mem[a + i] = v;
    }
    programs++;
    return true;
  }
  bool startErase(uint32_t a, uint32_t size) override {
    if (busyLeft > 0 || (size != storage::kSectorBytes && size != storage::kBlockBytes) || a % size || a >= mem.size() || size > mem.size() - a) return false;
    memset(mem.data() + a, 0xFF, size);
    (size == storage::kSectorBytes ? sectorErases : blockErases)++;
    busyLeft = erasePolls;
    return true;
  }
  bool busy() override {
    if (busyLeft > 0) { busyLeft--; return true; }
    return false;
  }
};
