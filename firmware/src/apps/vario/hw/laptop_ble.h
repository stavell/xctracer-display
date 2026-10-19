#pragma once
#include <bluefruit.h>
#include <atomic>
#include "apps/vario/app/ports.h"
#include "platform/util/spsc_ring.h"

// BLE peripheral for the laptop: one write-only command characteristic. Drawing commands paint into a
// private frame buffer in the panel's native layout (a whole frame is thousands of small writes); the
// loop copies it to the panel on FLUSH. Everything else is decoded and copied into a queue for the app
// thread. The callback never touches the panel. Protocol: see protocol.h and host/display_link.py.
class LaptopBle : public ILaptop {
 public:
  explicit LaptopBle(IPanel& panel, uint32_t graceMs = 10000) : panel_(panel), grace_(graceMs) {}
  void begin();                                      // after Bluefruit.begin(); starts advertising
  bool pop(Command& out) override { return q_.pop(out); }
  bool hostDrawing(uint32_t nowMs) override;
  void service(uint32_t nowMs) override;
  void disconnect();
  void stopAdvertising();
  void startAdvertising();                           // idempotent; the link comes back after a game

 private:
  static void onWrite(uint16_t h, BLECharacteristic* c, uint8_t* data, uint16_t len);
  static void onConnect(uint16_t h);
  static void onDisconnect(uint16_t h, uint8_t reason);
  void handle(const uint8_t* d, uint16_t len);
  void blitByte(uint8_t b);

  static const size_t FRAME_LEN = 400 * 240 / 8;

  IPanel& panel_;
  uint32_t grace_;
  BLEService svc_{"D15A0001-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  BLECharacteristic chr_{"D15A0002-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  volatile uint16_t conn_ = BLE_CONN_HANDLE_INVALID;

  struct { bool open; uint16_t x, y, w, h; uint32_t pos; } blit_ = {};   // callback task only
  uint8_t frame_[FRAME_LEN];             // written by the callback task, copied by the loop on flush
  std::atomic<bool> flushPending_{false};
  std::atomic<uint32_t> lastDraw_{0};
  std::atomic<bool> drawn_{false};   // a draw command has arrived at all (0 is a valid millis())

  SpscRing<Command, 4> q_;
};
