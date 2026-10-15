#pragma once
#include <bluefruit.h>
#include "app/ports.h"

// BLE peripheral for the laptop: one write-only command characteristic. Drawing commands paint
// straight into the panel's canvas (a whole frame is thousands of small writes); everything else
// is copied into a queue for the app thread. Protocol: see protocol.h and host/display_link.py.
class LaptopBle : public ILaptop {
 public:
  explicit LaptopBle(IPanel& panel, uint32_t graceMs = 10000) : panel_(panel), grace_(graceMs) {}
  void begin();                                      // after Bluefruit.begin(); starts advertising
  bool pop(Command& out) override;
  bool hostDrawing(uint32_t nowMs) override;
  void service(uint32_t nowMs) override;
  void disconnect();
  void stopAdvertising();

 private:
  static void onWrite(uint16_t h, BLECharacteristic* c, uint8_t* data, uint16_t len);
  static void onConnect(uint16_t h);
  static void onDisconnect(uint16_t h, uint8_t reason);
  void handle(const uint8_t* d, uint16_t len);
  void push(const Command& c);
  void blitByte(uint8_t b);

  IPanel& panel_;
  uint32_t grace_;
  BLEService svc_{"D15A0001-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  BLECharacteristic chr_{"D15A0002-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  volatile uint16_t conn_ = BLE_CONN_HANDLE_INVALID;

  struct { bool open; uint16_t x, y, w, h; uint32_t pos; } blit_ = {};
  volatile bool flushPending_ = false;
  volatile uint32_t lastDraw_ = 0;

  static const int QN = 4;
  Command q_[QN];
  volatile uint8_t qh_ = 0, qt_ = 0;
};
