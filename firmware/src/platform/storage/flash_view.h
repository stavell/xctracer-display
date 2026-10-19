#pragma once
// A flash with its last `reserve` bytes cut off. The map pack (header, loader, block reader) sees only this view, so
// nothing it does, not even a wrong length from the host, can reach the flight log kept in the cut-off tail.
#include "platform/storage/flash_port.h"

namespace storage {

class ReservedTailFlash : public IFlash {
 public:
  ReservedTailFlash(IFlash& inner, uint32_t reserve) : f_(inner), reserve_(reserve) {}
  uint32_t jedecId() override { return f_.jedecId(); }
  uint32_t capacity() const override { uint32_t c = f_.capacity(); return c > reserve_ ? c - reserve_ : 0; }
  bool read(uint32_t a, void* dst, uint32_t n) override { return fits(a, n) && f_.read(a, dst, n); }
  bool program(uint32_t a, const void* src, uint32_t n) override { return fits(a, n) && f_.program(a, src, n); }
  bool startErase(uint32_t a, uint32_t size) override { return fits(a, size) && f_.startErase(a, size); }
  bool busy() override { return f_.busy(); }

 private:
  bool fits(uint32_t a, uint32_t n) const { uint32_t c = capacity(); return a <= c && n <= c - a; }
  IFlash& f_;
  uint32_t reserve_;
};

}  // namespace storage
