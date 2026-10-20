#pragma once
#include <atomic>
#include "apps/vario/app/command_queue.h"
#include "apps/vario/app/ports.h"
#include "platform/hw/laptop_link.h"

// The vario's side of the laptop link (the link itself is platform/hw/laptop_link.h). Drawing opcodes (CLEAR, BLIT, DATA, FLUSH) paint into a
// private frame buffer in the panel's native layout (a whole frame is thousands of small writes); the loop copies it to the panel on
// FLUSH. The other vario opcodes are decoded and copied into a queue for the app thread (CommandQueue). The callback never touches the
// panel. Protocol: see protocol.h and host/display_link.py.
class LaptopBle : public ILaptop, public IPacketSink {
 public:
  explicit LaptopBle(IPanel& panel, LaptopLink& link, uint32_t graceMs = 10000) : panel_(panel), link_(link), grace_(graceMs) {}
  void begin();                                      // registers the vario's opcodes with the link; call link.begin() after all sinks are in
  bool pop(Command& out) override { return cmds_.pop(out); }
  bool hostDrawing(uint32_t nowMs) override;
  void service(uint32_t nowMs) override;
  void disconnect() { link_.disconnect(); }
  void stopAdvertising() { link_.stopAdvertising(); }
  void startAdvertising() { link_.startAdvertising(); }   // idempotent; the link comes back after a game
  void onPacket(const uint8_t* d, uint16_t len, uint32_t nowMs) override;   // the drawing opcodes, BLE callback context

 private:
  void blitByte(uint8_t b);

  static const size_t FRAME_LEN = 400 * 240 / 8;

  IPanel& panel_;
  LaptopLink& link_;
  uint32_t grace_;
  CommandQueue cmds_;

  struct { bool open; uint16_t x, y, w, h; uint32_t pos; } blit_ = {};   // callback task only
  uint8_t frame_[FRAME_LEN];             // written by the callback task, copied by the loop on flush
  std::atomic<bool> flushPending_{false};
  std::atomic<uint32_t> lastDraw_{0};
  std::atomic<bool> drawn_{false};   // a draw command has arrived at all (0 is a valid millis())
};
