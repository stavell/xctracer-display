#pragma once
// The laptop link's opcode dispatch: pure, no hardware. A packet written by the laptop starts with an opcode byte;
// consumers (the vario's drawing and commands, the pet snapshot, ...) register for opcode ranges and get the packets
// that start with one of them. The BLE glue (platform/hw/laptop_link.cpp) calls dispatch() from the write callback, so a
// sink only COPIES bytes (CLAUDE.md: no parsing, maths, drawing or flash in a BLE callback): decoding happens later,
// in the loop, from what the sink stored.
#include <stdint.h>

class IPacketSink {
 public:
  virtual ~IPacketSink() {}
  // One whole write, len >= 1, d[0] is the opcode. nowMs is the time of the write (the glue's millis()).
  virtual void onPacket(const uint8_t* d, uint16_t len, uint32_t nowMs) = 0;
};

class OpcodeDispatch {
 public:
  static const int MAX = 6;
  // Opcodes lo..hi (inclusive) go to `sink`. False when the table is full, the range is empty or it overlaps one taken.
  bool add(uint8_t lo, uint8_t hi, IPacketSink& sink) {
    if (n_ >= MAX || lo > hi) return false;
    for (int i = 0; i < n_; i++)
      if (lo <= e_[i].hi && hi >= e_[i].lo) return false;
    e_[n_++] = Entry{lo, hi, &sink};
    return true;
  }
  // True when a sink took the packet; unknown opcodes and empty packets are dropped.
  bool dispatch(const uint8_t* d, uint16_t len, uint32_t nowMs) const {
    if (len == 0) return false;
    for (int i = 0; i < n_; i++) {
      if (d[0] >= e_[i].lo && d[0] <= e_[i].hi) { e_[i].sink->onPacket(d, len, nowMs); return true; }
    }
    return false;
  }

 private:
  struct Entry { uint8_t lo, hi; IPacketSink* sink; };
  Entry e_[MAX] = {};
  int n_ = 0;
};
