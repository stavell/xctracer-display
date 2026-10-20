#pragma once
// The system sheet of the box image: a small overlay the host draws on top of ANY app after a deliberate gesture (the user
// switch held 3 s, or A + B held 1.5 s). It is the one way out of an app: Back, Home (the launcher), Sleep (the clock sleep),
// Power off (System OFF). Pure logic + drawing, no hardware; the AppHost owns one and feeds it (platform/app_host.h).
//
// Input, single switch: a short press moves to the next row (wraps), a long press does the row. Six buttons: UP / DOWN move,
// A does the row, B is Back. Both work at any moment (a fitted switch and fitted buttons are not exclusive). Without input for
// kIdleMs the sheet closes by itself (= Back); the switch being held counts as input. Holding the switch on to kOffHoldMs
// (counted from the first press, so 3 s after the sheet opened) is the expert path to Power off.
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/ui.h"
#include "platform/ports.h"

class SystemSheet {
 public:
  enum Row : uint8_t { R_BACK, R_HOME, R_SLEEP, R_OFF };
  enum Act : uint8_t { A_NONE, A_CLOSE, A_HOME, A_SLEEP, A_OFF };
  static const uint32_t kIdleMs = 6000, kOffHoldMs = 6000, kHintHoldMs = 3000;
  static const int MAX_ROWS = 4;

  bool isOpen() const { return open_; }
  // start: the row the cursor begins on (R_HOME falls back to R_BACK when there is no Home row); hasHome: false in the launcher.
  void open(Row start, bool hasHome, uint32_t nowMs) {
    open_ = true;
    n_ = 0;
    rows_[n_++] = R_BACK;
    if (hasHome) rows_[n_++] = R_HOME;
    rows_[n_++] = R_SLEEP;
    rows_[n_++] = R_OFF;
    cur_ = 0;
    for (int i = 0; i < n_; i++) if (rows_[i] == start) cur_ = i;
    lastInput_ = nowMs;
    held_ = 0;
    dirty_ = true;
  }
  void close() { open_ = false; held_ = 0; }

  int count() const { return n_; }
  Row row(int i) const { return rows_[i]; }
  Row cursorRow() const { return rows_[cur_]; }
  int cursor() const { return cur_; }
  // The power-off bar under the hint: 12 steps from the 3 s hold to the 6 s one, 0 before.
  int barStep() const { return held_ < kHintHoldMs ? 0 : 1 + (int)((held_ - kHintHoldMs) * 11 / (kOffHoldMs - kHintHoldMs)); }
  bool keepHolding() const { return open_ && held_ >= kHintHoldMs; }

  // A recognised press of the switch.
  Act input(ButtonEvent ev, uint32_t nowMs) {
    if (!open_ || (ev != BTN_SHORT && ev != BTN_LONG)) return A_NONE;
    lastInput_ = nowMs;
    if (ev == BTN_SHORT) { move(1); return A_NONE; }
    return act();
  }
  // A key edge of the six buttons.
  Act key(Key k, bool down, uint32_t nowMs) {
    if (!open_ || !down) return A_NONE;
    if (k == KEY_UP || k == KEY_DOWN) { lastInput_ = nowMs; move(k == KEY_DOWN ? 1 : -1); return A_NONE; }
    if (k == KEY_ACTION) { lastInput_ = nowMs; return act(); }
    if (k == KEY_B) { lastInput_ = nowMs; return finish(A_CLOSE); }
    return A_NONE;
  }
  // Once per host iteration with the time the switch has been down (0 when up).
  Act tick(uint32_t nowMs, uint32_t heldMs) {
    if (!open_) return A_NONE;
    const uint32_t before = held_;
    held_ = heldMs;
    if (before != held_ && (before >= kHintHoldMs || held_ >= kHintHoldMs) && barStep() != lastStep_) dirty_ = true;
    if ((before >= kHintHoldMs) != (held_ >= kHintHoldMs)) dirty_ = true;
    lastStep_ = barStep();
    if (heldMs > 0) lastInput_ = nowMs;
    if (heldMs >= kOffHoldMs) return finish(A_OFF);
    if (nowMs - lastInput_ >= kIdleMs) return finish(A_CLOSE);
    return A_NONE;
  }
  bool takeDirty() { const bool d = dirty_; dirty_ = false; return d; }

  // The overlay over whatever is on the canvas. sixButtons: hint wording of the buttons instead of the switch.
  void draw(const Bitmap1& bm, bool sixButtons);

 private:
  void move(int d) { cur_ = (cur_ + d + n_) % n_; dirty_ = true; }
  Act act() {
    switch (rows_[cur_]) {
      case R_BACK: return finish(A_CLOSE);
      case R_HOME: return finish(A_HOME);
      case R_SLEEP: return finish(A_SLEEP);
      default: return finish(A_OFF);
    }
  }
  Act finish(Act a) { open_ = false; held_ = 0; return a; }

  bool open_ = false, dirty_ = false;
  Row rows_[MAX_ROWS] = {};
  int n_ = 0, cur_ = 0;
  uint32_t lastInput_ = 0, held_ = 0;
  int lastStep_ = 0;
  UiFonts f_;
};
