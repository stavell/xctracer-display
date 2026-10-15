#pragma once
#include <Arduino.h>
#include "app/button_fsm.h"

// The single user switch on a GPIO (active low); all the timing logic is in ButtonFsm.
class ButtonGpio : public IInput {
 public:
  explicit ButtonGpio(uint8_t pin) : pin_(pin) {}
  void begin() { pinMode(pin_, INPUT_PULLUP); }
  ButtonEvent poll(uint32_t now) override { return fsm_.feed(digitalRead(pin_) == LOW, now); }
  uint32_t heldMs(uint32_t now) override { return fsm_.heldMs(now); }
 private:
  uint8_t pin_;
  ButtonFsm fsm_;
};
