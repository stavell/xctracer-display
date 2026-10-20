#pragma once
// The laptop link's pet consumer: opcode 0x30 packets land in a Mailbox (BLE callback: copy only); the Agents app reads it
// through IPetFeed. Also remembers which snapshot the app last drew, for the status the box reports back.
#include "platform/link/opcode_dispatch.h"
#include "platform/pet/pet_snapshot.h"
#include "platform/ports.h"

namespace pet {

class Feed : public IPacketSink, public IPetFeed {
 public:
  void onPacket(const uint8_t* d, uint16_t len, uint32_t nowMs) override { box_.put(d, len, nowMs); }
  bool latest(Snapshot& out, uint32_t& seq) override { return box_.latest(out, seq); }
  uint32_t lastWriteMs() override { return box_.lastWriteMs(); }
  void markShown(uint8_t snapshotSeq) override { shownSeq_ = snapshotSeq; shown_ = true; }
  bool anyShown() const { return shown_; }
  uint8_t shownSeq() const { return shownSeq_; }
  Mailbox& mailbox() { return box_; }

 private:
  Mailbox box_;
  uint8_t shownSeq_ = 0;
  bool shown_ = false;
};

}  // namespace pet
