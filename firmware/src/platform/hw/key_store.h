#pragma once
#include "platform/ports.h"

// A few persistent numbers (boot app, high scores, shared settings) in one small file in the nRF's internal flash,
// written atomically (temp file + rename) and only when a value actually changes.
class FlashKeyStore : public IKeyStore {
 public:
  void begin();                                     // after the filesystem is mounted (ConfigStore::begin does it)
  uint32_t get(StoreSlot slot) const override { return slot < SLOTS ? v_[slot] : 0; }
  void set(StoreSlot slot, uint32_t value) override;
  void beginBatch() override { batch_++; }
  void endBatch() override;

 private:
  static const int SLOTS = 16;                      // fixed, so the file keeps its size when slots are added
  void persist();
  uint32_t v_[SLOTS] = {};
  int batch_ = 0;
  bool dirty_ = false;
};
