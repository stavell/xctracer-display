#pragma once
#include <Arduino.h>
#include <atomic>
#include "platform/input/button_fsm.h"
#include "platform/util/spsc_ring.h"

// The single user switch on a GPIO (active low); all the timing logic is in ButtonFsm.
//
// Sampled every 10 ms from a FreeRTOS software timer, not from the main loop: a redraw takes about
// 108 ms, so a short press that starts and ends inside one redraw would never be seen by a poll()
// per loop iteration. The timer callback (timer service task, so it must stay short and never block)
// feeds the FSM and queues events; poll() runs on the loop task and pops one event per call.
// There is one switch, so one instance: the timer callback finds it through a static pointer.
class ButtonGpio : public IInput {
 public:
  explicit ButtonGpio(uint8_t pin) : pin_(pin) {}
  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    self_ = this;
    timer_.begin(SAMPLE_MS, onTimer);
    timer_.start();
  }
  ButtonEvent poll(uint32_t) override {
    ButtonEvent e = BTN_NONE;
    events_.pop(e);
    return e;
  }
  uint32_t heldMs(uint32_t now) override {
    if (!held_.load(std::memory_order_acquire)) return 0;
    int32_t d = (int32_t)(now - pressedAt_.load(std::memory_order_relaxed));
    return d > 0 ? (uint32_t)d : 0;  // the callback may be newer than the caller's `now`
  }

 private:
  static const uint32_t SAMPLE_MS = 10;
  static void onTimer(TimerHandle_t) { self_->sample(millis()); }
  void sample(uint32_t now) {
    ButtonEvent e = fsm_.feed(digitalRead(pin_) == LOW, now);
    if (e != BTN_NONE) events_.push(e);  // full: dropped, the loop is badly stalled anyway
    uint32_t held = fsm_.heldMs(now);
    if (held > 0) {
      pressedAt_.store(now - held, std::memory_order_relaxed);  // start first, then the flag
      held_.store(true, std::memory_order_release);
    } else {
      held_.store(false, std::memory_order_release);
    }
  }

  uint8_t pin_;
  ButtonFsm fsm_;  // touched by the timer callback only
  SoftwareTimer timer_;
  SpscRing<ButtonEvent, 8> events_;
  std::atomic<bool> held_{false};
  std::atomic<uint32_t> pressedAt_{0};
  static inline ButtonGpio* self_ = nullptr;
};
