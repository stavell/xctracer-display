#include "platform/settings/box_settings.h"
#include <string.h>
#include "platform/sensors/mount.h"

namespace {
uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
float unbits(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }
}  // namespace

int BoxSettings::tzMinutes() const {
  const int v = (int)(int32_t)s_.get(SLOT_TZ);
  return v >= -24 * 60 && v <= 24 * 60 ? v : 0;
}

void BoxSettings::setTzMinutes(int m) {
  if (m < -24 * 60) m = -24 * 60;
  if (m > 24 * 60) m = 24 * 60;
  s_.set(SLOT_TZ, (uint32_t)(int32_t)m);
}

bool BoxSettings::loadMagCal(MagCal& out) const {
  if (s_.get(SLOT_MAG_VALID) != 1) return false;
  MagCal c;
  c.valid = true;
  c.off[0] = unbits(s_.get(SLOT_MAG_X));
  c.off[1] = unbits(s_.get(SLOT_MAG_Y));
  c.off[2] = unbits(s_.get(SLOT_MAG_Z));
  c.radius = unbits(s_.get(SLOT_MAG_R));
  for (int i = 0; i < 3; i++) if (!isfinite(c.off[i])) return false;       // damaged: as if there were none
  if (!isfinite(c.radius)) return false;
  out = c;
  return true;
}

void BoxSettings::saveMagCal(const MagCal& cal) {
  s_.beginBatch();
  s_.set(SLOT_MAG_X, bits(cal.off[0]));
  s_.set(SLOT_MAG_Y, bits(cal.off[1]));
  s_.set(SLOT_MAG_Z, bits(cal.off[2]));
  s_.set(SLOT_MAG_R, bits(cal.radius));
  s_.set(SLOT_MAG_VALID, 1);
  s_.endBatch();
}

int BoxSettings::mountIndex() const {
  const uint32_t v = s_.get(SLOT_MOUNT);
  return v < (uint32_t)MOUNT_COUNT ? (int)v : 0;
}

bool BoxSettings::setMountIndex(int index) {
  if (!mountValid(index)) return false;
  const int cur = mountIndex();
  if (index == cur) return true;
  s_.beginBatch();
  MagCal cal;
  if (loadMagCal(cal)) {
    Vec3 o; o.x = cal.off[0]; o.y = cal.off[1]; o.z = cal.off[2];
    o = mountRemap(cur, index, o);
    cal.off[0] = o.x; cal.off[1] = o.y; cal.off[2] = o.z;
    saveMagCal(cal);
  }
  s_.set(SLOT_MOUNT, (uint32_t)index);
  s_.endBatch();
  return true;
}

void BoxSettings::importLegacy(const Legacy& l) {
  if (migrated()) return;
  s_.beginBatch();
  if (l.haveCal && l.cal.valid) saveMagCal(l.cal);
  if (l.haveMount && mountValid(l.mount)) s_.set(SLOT_MOUNT, (uint32_t)l.mount);
  if (l.haveTz) setTzMinutes(l.tz);
  s_.set(SLOT_MIGRATED, 1);
  s_.endBatch();
}
