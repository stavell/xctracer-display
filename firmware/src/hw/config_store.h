#pragma once
#include <bluefruit.h>
#include "app/ports.h"

// Settings kept in the nRF's internal flash: time zone, tracer name filter, remembered tracer.
class ConfigStore : public ISettings {
 public:
  void begin();                                   // mounts the filesystem and loads
  int tzMinutes() const override { return sv_.tz; }
  void setTzMinutes(int m) override { sv_.tz = (int16_t)m; save(); }
  bool loadMagCal(MagCal& out) override;
  void saveMagCal(const MagCal& cal) override;

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
    int16_t tz;
  };
  void save();
  Saved sv_;
};
