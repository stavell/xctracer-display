#pragma once
#include "platform/util/types.h"

// Board -> device (case) mounting: one of the 24 proper rotations that map axes to axes (signed axis
// permutations with determinant +1). The index is stored in flash and set with `ctl.py orient <0..23>`.
//
//   device[r] = sign[r] * board[perm[r]]        (device = R * board; index 0 is the identity)
//
// "Board" frame = the frame after the per-chip fixed mapping in hw/; "device" frame = x right of the
// screen, y top of the screen, z out of the screen. The table is mirrored in host/orient_analyze.py (its
// self-test parses mount.cpp and compares), so keep the order.
constexpr int MOUNT_COUNT = 24;

bool mountValid(int index);
Vec3 mountApply(int index, const Vec3& boardVec);          // a bad index acts as the identity
Vec3 mountInverse(int index, const Vec3& deviceVec);       // device -> board (test data, calibration moves)
// Re-express a vector given in the device frame of mounting `from` in the device frame of mounting `to`.
Vec3 mountRemap(int from, int to, const Vec3& v);
