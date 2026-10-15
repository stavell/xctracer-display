#pragma once
#include <Arduino.h>

// Single switch, debounced. Release before LONG_MS = SHORT, release between
// LONG_MS and ARM_MS = LONG, release between ARM_MS and HOLD_MS = cancelled,
// still held at HOLD_MS = HOLD (fires once, while held).
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG, BTN_HOLD };

class Button {
 public:
  static const uint32_t LONG_MS = 700, ARM_MS = 1500, HOLD_MS = 3000;
  explicit Button(uint8_t pin, uint32_t debounceMs = 25) : pin_(pin), debounceMs_(debounceMs) {}
  void begin() { pinMode(pin_, INPUT_PULLUP); }
  bool down() const { return stable_; }
  uint32_t heldMs() const { return stable_ ? millis() - pressedAt_ : 0; }
  ButtonEvent poll() {
    uint32_t now = millis();
    bool d = digitalRead(pin_) == LOW;
    if (d != raw_) { raw_ = d; changed_ = now; }
    if (now - changed_ < debounceMs_) return BTN_NONE;
    if (d && !stable_) { stable_ = true; pressedAt_ = now; holdFired_ = false; }
    if (d && stable_ && !holdFired_ && now - pressedAt_ >= HOLD_MS) {
      holdFired_ = true;
      return BTN_HOLD;
    }
    if (!d && stable_) {
      stable_ = false;
      uint32_t t = now - pressedAt_;
      if (holdFired_) return BTN_NONE;
      if (t < LONG_MS) return BTN_SHORT;
      if (t < ARM_MS) return BTN_LONG;
    }
    return BTN_NONE;
  }
 private:
  uint8_t pin_;
  uint32_t debounceMs_;
  bool raw_ = false, stable_ = false, holdFired_ = false;
  uint32_t changed_ = 0, pressedAt_ = 0;
};
