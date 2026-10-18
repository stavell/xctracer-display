#pragma once
// The map pack inside the flash as an IBlockReader: pack offsets are shifted by the pack base and bounded by the pack
// length from the flash header, so the terrain code only ever sees the pack's own bytes. Pure C++.
#include "platform/storage/flash_port.h"
#include "platform/storage/pack_header.h"
#include "platform/terrain/block_reader.h"

namespace storage {

class FlashBlockReader : public terrain::IBlockReader {
 public:
  explicit FlashBlockReader(IFlash& flash) : flash_(flash) {}
  void attach(uint32_t packLen) { len_ = packLen; }
  void detach() { len_ = 0; }
  bool attached() const { return len_ != 0; }
  uint32_t length() const { return len_; }
  // While the loader rewrites the flash nothing may be read as map data.
  void setLocked(bool locked) { locked_ = locked; }
  bool locked() const { return locked_; }
  uint32_t reads() const { return reads_; }
  uint32_t failures() const { return failures_; }

  bool read(uint32_t offset, void* dst, uint32_t len) override {
    reads_++;
    if (locked_ || offset > len_ || len > len_ - offset || !flash_.read(kPackBase + offset, dst, len)) {
      failures_++;
      return false;
    }
    return true;
  }

 private:
  IFlash& flash_;
  uint32_t len_ = 0;
  bool locked_ = false;
  uint32_t reads_ = 0, failures_ = 0;
};

}  // namespace storage
