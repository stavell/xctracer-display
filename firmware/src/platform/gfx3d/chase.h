#pragma once
// Chase camera of the 3D view and the glider sprite. Pure maths and 1-bit drawing, no state beyond the smoothed camera.
//
// The camera hangs kBack metres behind the glider and kUp metres above it, looking along a SMOOTHED heading (it lags the
// glider's heading by a time constant in sim seconds, so turns swing the glider across the screen and the world sweeps
// past) and with a fraction (kRollFrac) of the glider's bank, so the horizon tilts less than the wing does. The glider
// is drawn where its position projects: lower-middle of the screen, off to the side in a turn, with the bank it really has
// (relative to the camera's own roll) and a canopy that shows more of its top when the speed bar is pushed (the wing pitches
// nose down). A shadow on the ground is drawn while the ground is within sight below.
//
// With tilt steering the horizon counter-rotates by the box roll (a window onto the real sky); the camera roll is then the
// box roll and the sprite shows the bank relative to it.
#include "platform/gfx3d/glider.h"
#include "platform/gfx3d/height_cache.h"
#include "platform/gfx3d/voxel.h"
#include "platform/gfx/bitmap1.h"

namespace gfx3d {

class ChaseCam {
 public:
  static constexpr float kBack = 46.0f;            // m behind
  static constexpr float kUp = 20.0f;              // m above
  static constexpr float kPitch = 0.09f;           // rad, as VoxelView's Camera.pitch: puts the horizon ~15 px lower
  static constexpr float kRollFrac = 0.35f;        // share of the bank the horizon follows
  static constexpr float kHeadingTau = 0.5f;       // s of sim time
  static constexpr float kRollTau = 0.3f;

  void reset(const Glider& g) { heading_ = g.heading; roll_ = kRollFrac * g.bank; }
  // dt in SIM seconds (the lag then looks the same at x1, x3 and x5). rollOverride: radians, or NaN for the default.
  void update(const Glider& g, float rollOverride, float dt);
  // The camera for VoxelView (kept above the terrain).
  Camera camera(const Glider& g, const HeightCache& hc) const;
  // Screen position of a world point for a camera; false when it is behind. s: distance along the view axis.
  static bool project(const Camera& c, float x, float y, float z, float* sx, float* sy, float* s);
  // Draw the glider sprite and its ground shadow.
  void drawGlider(const Bitmap1& bm, const Glider& g, const HeightCache& hc, const Camera& cam) const;

  float heading() const { return heading_; }
  float roll() const { return roll_; }

 private:
  float heading_ = 0, roll_ = 0;
};

}  // namespace gfx3d
