#pragma once
// Tilt steering, pure maths: the box's roll and pitch (from the AHRS on the device, from the mouse in the PC player)
// become the glider's commands.
//
//   roll   = rotation of the screen plane about the axis out of the screen, measured from gravity like a steering
//            wheel: positive = the box turned clockwise as the pilot looks at it (right side down).
//   pitch  = positive when the top of the box leans AWAY from the pilot.
//   zero   = the roll / pitch the pilot holds comfortably (set by the calibration step), subtracted first.
//
// Bank:  deadzone 3 degrees (nothing happens), then rising to full at 38 degrees from the zero, with an expo curve
//        out = expo * m^3 + (1 - expo) * m (m = the 0..1 position between deadzone and full): fine control near the
//        middle, full authority at the end.
// Pitch: top away -> speed bar 0..1 (full at 20 degrees), top toward the pilot -> brakes 0..1 (full at 20 degrees),
//        same deadzone and curve.
#include "apps/games/sim/sim_math.h"

namespace sim {

struct ITilt {                    // the tilt source: AHRS on the device, the mouse on the PC
  virtual ~ITilt() {}
  virtual bool read(float* rollDeg, float* pitchDeg) = 0;
};

struct TiltMap {
  static constexpr float kDeadDeg = 3.0f;
  static constexpr float kBankFullDeg = 38.0f;
  static constexpr float kPitchFullDeg = 20.0f;
  static constexpr float kExpo = 0.35f;

  // Signed command -1..1 for an angle already relative to the zero.
  static float curve(float deg, float deadDeg, float fullDeg, float expo = kExpo) {
    const float a = deg < 0 ? -deg : deg;
    if (!(a > deadDeg)) return 0;
    float m = (a - deadDeg) / (fullDeg - deadDeg);
    if (m > 1.0f) m = 1.0f;
    const float out = expo * m * m * m + (1.0f - expo) * m;
    return deg < 0 ? -out : out;
  }
  static float bank(float rollDeg, float zeroDeg) { return curve(rollDeg - zeroDeg, kDeadDeg, kBankFullDeg); }
  static void pitch(float pitchDeg, float zeroDeg, float* bar, float* brake) {
    const float c = curve(pitchDeg - zeroDeg, kDeadDeg, kPitchFullDeg);
    *bar = c > 0 ? c : 0;
    *brake = c < 0 ? -c : 0;
  }
};

}  // namespace sim
