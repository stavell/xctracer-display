#pragma once
// Debounce for a plain button (the D-pad and A / B): the state changes after the raw level has been
// stable for debounceMs. Pure, one per key.
#include <stdint.h>

class KeyDebounce {
 public:
  explicit KeyDebounce(uint32_t debounceMs = 15) : debounce_(debounceMs) {}
  // Feed the raw level every sample; returns true when the debounced state just changed (see down()).
  bool feed(bool raw, uint32_t now) {
    if (raw != raw_) { raw_ = raw; changed_ = now; }
    if (raw_ != stable_ && now - changed_ >= debounce_) { stable_ = raw_; return true; }
    return false;
  }
  bool down() const { return stable_; }

 private:
  uint32_t debounce_;
  bool raw_ = false, stable_ = false;
  uint32_t changed_ = 0;
};
