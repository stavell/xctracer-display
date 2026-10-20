#pragma once
#include <bluefruit.h>
#include "platform/link/opcode_dispatch.h"
#include "platform/pet/pet_status.h"

// The BLE peripheral for the laptop, generic part: the service, the write characteristic (WRITE and WRITE_WO_RESP, up to 244 bytes), the
// status characteristic (read + notify, the box's report back), advertising and the connection. A write goes to the OpcodeDispatch table
// (platform/link/opcode_dispatch.h) in the BLE callback; the sinks only copy. What the opcodes mean is the sinks' business: the vario's
// drawing and commands (apps/vario/hw/laptop_ble.h), the pet snapshot (platform/pet/pet_feed.h). Register the sinks before begin().
// Protocol: apps/vario/hw/protocol.h, docs/claude_pet.md, host/display_link.py.
class LaptopLink {
 public:
  bool add(uint8_t lo, uint8_t hi, IPacketSink& sink) { return dispatch_.add(lo, hi, sink); }
  void begin();                                      // after Bluefruit.begin(); starts advertising
  bool connected() const { return conn_ != BLE_CONN_HANDLE_INVALID; }
  // Sets the status characteristic's value and notifies the laptop when it subscribed. Loop context, not the callback.
  void reportStatus(const uint8_t* data, uint16_t len);
  void disconnect();
  void stopAdvertising();
  void startAdvertising();                           // idempotent; the link comes back after a game

 private:
  static void onWrite(uint16_t h, BLECharacteristic* c, uint8_t* data, uint16_t len);
  static void onConnect(uint16_t h);
  static void onDisconnect(uint16_t h, uint8_t reason);

  OpcodeDispatch dispatch_;
  BLEService svc_{"D15A0001-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  BLECharacteristic chr_{"D15A0002-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  BLECharacteristic status_{"D15A0003-5B7A-4C3E-9F10-2A6E8C4B7D01"};
  volatile uint16_t conn_ = BLE_CONN_HANDLE_INVALID;
};
