#include "platform/sensors/mount.h"

// Same order as itertools.permutations(range(3)) x itertools.product((1, -1), repeat=3), keeping det +1.
// Row = {{perm0, perm1, perm2}, {sign0, sign1, sign2}}. host/orient_analyze.py parses this table.
struct MountRow { int8_t p[3]; int8_t s[3]; };
static const MountRow MOUNT_TABLE[MOUNT_COUNT] = {
  // MOUNT_TABLE_BEGIN
  {{0, 1, 2}, {1, 1, 1}},
  {{0, 1, 2}, {1, -1, -1}},
  {{0, 1, 2}, {-1, 1, -1}},
  {{0, 1, 2}, {-1, -1, 1}},
  {{0, 2, 1}, {1, 1, -1}},
  {{0, 2, 1}, {1, -1, 1}},
  {{0, 2, 1}, {-1, 1, 1}},
  {{0, 2, 1}, {-1, -1, -1}},
  {{1, 0, 2}, {1, 1, -1}},
  {{1, 0, 2}, {1, -1, 1}},
  {{1, 0, 2}, {-1, 1, 1}},
  {{1, 0, 2}, {-1, -1, -1}},
  {{1, 2, 0}, {1, 1, 1}},
  {{1, 2, 0}, {1, -1, -1}},
  {{1, 2, 0}, {-1, 1, -1}},
  {{1, 2, 0}, {-1, -1, 1}},
  {{2, 0, 1}, {1, 1, 1}},
  {{2, 0, 1}, {1, -1, -1}},
  {{2, 0, 1}, {-1, 1, -1}},
  {{2, 0, 1}, {-1, -1, 1}},
  {{2, 1, 0}, {1, 1, -1}},
  {{2, 1, 0}, {1, -1, 1}},
  {{2, 1, 0}, {-1, 1, 1}},
  {{2, 1, 0}, {-1, -1, -1}},
  // MOUNT_TABLE_END
};

bool mountValid(int index) { return index >= 0 && index < MOUNT_COUNT; }

Vec3 mountApply(int index, const Vec3& b) {
  if (!mountValid(index)) return b;
  const MountRow& r = MOUNT_TABLE[index];
  const float in[3] = {b.x, b.y, b.z};
  Vec3 d;
  d.x = r.s[0] * in[r.p[0]];
  d.y = r.s[1] * in[r.p[1]];
  d.z = r.s[2] * in[r.p[2]];
  return d;
}

Vec3 mountInverse(int index, const Vec3& d) {
  if (!mountValid(index)) return d;
  const MountRow& r = MOUNT_TABLE[index];
  const float in[3] = {d.x, d.y, d.z};
  float out[3] = {0, 0, 0};
  for (int k = 0; k < 3; k++) out[r.p[k]] = r.s[k] * in[k];     // board[perm[k]] = sign[k] * device[k]
  Vec3 b;
  b.x = out[0]; b.y = out[1]; b.z = out[2];
  return b;
}

Vec3 mountRemap(int from, int to, const Vec3& v) { return mountApply(to, mountInverse(from, v)); }
