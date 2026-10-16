#pragma once
#include <stdint.h>
#include "core/types.h"

// Glides a displayed heading toward a target along the shortest arc. Display animation only: the time
// is the UI tick time, not flight data. Exponential approach, so the result does not depend on the
// frame rate. Pure float maths.
class HeadingSmoother {
 public:
  static constexpr float SETTLE_DEG = 0.5f;       // closer than this counts as arrived (and snaps)
  static constexpr uint32_t GAP_MS = 2000;        // a longer pause between updates snaps

  explicit HeadingSmoother(float tauMs = 250.0f) : tau_(tauMs) {}
  void setTau(float tauMs) { tau_ = tauMs; }
  // Deadband for a noisy source (the magnetometer): the target only moves when the new value is more than
  // `deg` from it, or when it has stayed on one side by more than DRIFT_DEG for `holdMs`. 0 = off.
  // Without it sensor noise keeps the glide (and the fast redraw) running when the unit lies still.
  void setDeadband(float deg, uint32_t holdMs);
  static constexpr float DRIFT_DEG = 0.15f;

  // Returns the displayed heading in [0, 360), NaN for a non-finite target.
  float update(float targetDeg, uint32_t nowMs);
  float value() const { return out_; }
  bool settled() const;      // arrived at the target, or nothing to show
  void reset();

 private:
  float tau_;
  float out_ = NAN;
  float target_ = NAN;
  uint32_t lastMs_ = 0;
  float dead_ = 0;
  uint32_t holdMs_ = 0;
  uint32_t driftSince_ = 0;
  int driftSign_ = 0;
};
