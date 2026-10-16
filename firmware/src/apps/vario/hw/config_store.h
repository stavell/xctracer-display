#pragma once
#include <bluefruit.h>
#include "platform/settings/box_settings.h"

// The vario's own settings kept in the nRF's internal flash: tracer name filter and remembered tracer. (The time
// zone, the mounting and the compass calibration are the box's: platform/settings/box_settings.h.)
class ConfigStore {
 public:
  void begin();                                   // mounts the filesystem and loads
  // One-time hand-over of what older firmware kept here (time zone, /magcal3.bin, /mount.bin) to the box's store.
  // The old files stay where they are, unread afterwards.
  void migrateTo(BoxSettings& dst);

  const char* filter() const { return sv_.filter; }
  void setFilter(const char* glob);
  bool hasSaved() const { return sv_.has; }
  const char* name() const { return sv_.name; }
  const ble_gap_addr_t& addr() const { return sv_.addr; }
  void saveDevice(const char* name, const ble_gap_addr_t& addr);
  void updateAddr(const ble_gap_addr_t& addr) { sv_.addr = addr; }   // not persisted until the next save
  void forget();

 private:
  struct Saved {
    uint32_t magic;
    char filter[24];
    bool has;
    char name[24];
    ble_gap_addr_t addr;
    int16_t tz;                                 // legacy: now BoxSettings; kept so the file keeps its size
  };
  void save();
  Saved sv_;
};
