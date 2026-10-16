#pragma once
// "Hold these two keys together for N ms": the deliberate gesture that opens the launcher on the six-button
// hardware (A + B, 1.5 s). Pure, fed with the current key states. Fires once per hold: both keys must be
// released (or at least one of them) before it can fire again.
#include <stdint.h>

class ComboHold {
 public:
  explicit ComboHold(uint32_t holdMs = 1500) : holdMs_(holdMs) {}
  // True exactly once, on the first call at which both have been down for holdMs.
  bool feed(bool a, bool b, uint32_t now) {
    if (!(a && b)) { active_ = false; fired_ = false; return false; }
    if (!active_) { active_ = true; since_ = now; }
    if (!fired_ && now - since_ >= holdMs_) { fired_ = true; return true; }
    return false;
  }
  uint32_t heldMs(uint32_t now) const { return active_ ? now - since_ : 0; }   // for a progress hint
  void reset() { active_ = fired_ = false; }

 private:
  uint32_t holdMs_;
  bool active_ = false, fired_ = false;
  uint32_t since_ = 0;
};
