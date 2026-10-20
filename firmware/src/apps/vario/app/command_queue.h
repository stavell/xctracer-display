#pragma once
// The vario's consumer of the laptop link: its non-drawing packets (protocol.h: filter, TZ, SIM, XCTRC, PAGE, CAL, orientation, the
// diagnostics opcodes) are decoded into Commands and queued for the app thread. Pure; the BLE callback only calls onPacket().
#include "apps/vario/app/command_decode.h"
#include "platform/link/opcode_dispatch.h"
#include "platform/util/spsc_ring.h"

class CommandQueue : public IPacketSink {
 public:
  void onPacket(const uint8_t* d, uint16_t len, uint32_t) override {
    Command c;
    if (decodeCommand(d, len, c)) q_.push(c);
  }
  bool pop(Command& out) { return q_.pop(out); }

 private:
  SpscRing<Command, 4> q_;
};
