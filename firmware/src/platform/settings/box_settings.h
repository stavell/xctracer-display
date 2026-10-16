#pragma once
// The settings every app of the box shares, on top of the key store: time zone, how the board sits in the case
// (mounting index) and the magnetometer's hard-iron calibration. They belong to the box, not to the vario: the
// compass tool and the Settings app use them too, and the vario-only image keeps them in the same place.
// Pure C++ (no flash access of its own).
#include "platform/ports.h"
#include "platform/util/types.h"

class BoxSettings {
 public:
  explicit BoxSettings(IKeyStore& store) : s_(store) {}

  int tzMinutes() const;                           // minutes east of UTC, 0 when none was set
  void setTzMinutes(int minutes);                  // kept within +-24 h (ctl.py may send any int16)

  bool loadMagCal(MagCal& out) const;              // false when none is stored
  void saveMagCal(const MagCal& cal);              // one flash write

  int mountIndex() const;                          // 0..23, 0 when none was set
  // Change how the board sits in the case. The stored offsets are in the device frame, so they move along
  // (platform/sensors/mount.h mountRemap): a calibration made in the old mounting stays right. False for a bad index.
  bool setMountIndex(int index);

  // What older firmware kept in separate files of the vario's config store.
  struct Legacy {
    bool haveCal = false, haveMount = false, haveTz = false;
    MagCal cal;
    int mount = 0, tz = 0;
  };
  bool migrated() const { return s_.get(SLOT_MIGRATED) != 0; }
  // One-time import at the first start of firmware that knows this store; later calls do nothing, so a value
  // changed since is never overwritten by a stale file.
  void importLegacy(const Legacy& l);

 private:
  IKeyStore& s_;
};
