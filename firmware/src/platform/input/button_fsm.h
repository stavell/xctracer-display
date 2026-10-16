#pragma once
#include <stdint.h>
#include "platform/ports.h"

// Single-switch state machine, pure so it can be tested on the PC. Debounced. Release before
// LONG_MS = SHORT, release between LONG_MS and ARM_MS = LONG, release between ARM_MS and HOLD_MS
// = cancelled, still held at HOLD_MS = HOLD (fires once, while held).
class ButtonFsm {
 public:
  static const uint32_t LONG_MS = 700, ARM_MS = 1500, HOLD_MS = 3000;
  explicit ButtonFsm(uint32_t debounceMs = 25) : debounce_(debounceMs) {}

  ButtonEvent feed(bool down, uint32_t now) {
    if (down != raw_) { raw_ = down; changed_ = now; }
    if (now - changed_ < debounce_) return BTN_NONE;
    if (down && !stable_) { stable_ = true; pressedAt_ = now; holdFired_ = false; }
    if (down && stable_ && !holdFired_ && now - pressedAt_ >= HOLD_MS) {
      holdFired_ = true;
      return BTN_HOLD;
    }
    if (!down && stable_) {
      stable_ = false;
      uint32_t t = now - pressedAt_;
      if (holdFired_) return BTN_NONE;
      if (t < LONG_MS) return BTN_SHORT;
      if (t < ARM_MS) return BTN_LONG;
    }
    return BTN_NONE;
  }
  uint32_t heldMs(uint32_t now) const { return stable_ ? now - pressedAt_ : 0; }
  bool down() const { return stable_; }                 // debounced state, true from the first stable sample

 private:
  uint32_t debounce_;
  bool raw_ = false, stable_ = false, holdFired_ = false;
  uint32_t changed_ = 0, pressedAt_ = 0;
};
