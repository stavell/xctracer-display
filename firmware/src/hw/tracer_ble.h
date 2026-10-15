#pragma once
#include <bluefruit.h>
#include "app/ports.h"
#include "hw/config_store.h"

// BLE central: finds the vario by name filter, remembers it, subscribes to its $XCTRC stream (service
// 0xFFE0, characteristic 0xFFE1). The Bluetooth callbacks only copy text into a queue; all parsing
// happens later in the app thread.
class TracerBle : public ITracer {
 public:
  explicit TracerBle(ConfigStore& cfg) : cfg_(cfg), svc_(0xFFE0), chr_(0xFFE1) {}
  void begin();                                      // after Bluefruit.begin(1, 1)
  void update(uint32_t nowMs) override;
  ViewModel::Link link() const override { return link_; }
  bool hasSaved() const override { return cfg_.hasSaved(); }
  const char* deviceName() const override { return cfg_.name(); }
  const char* filter() const override { return cfg_.filter(); }
  void setFilter(const char* glob) override { cfg_.setFilter(glob); }
  void listOpen() override;
  void listClose() override;
  int listCount() override { return nItems_; }
  bool listItem(int i, ListItem& out) override;
  bool listConnect(int i) override;
  bool popLine(char* buf, size_t n) override;
  void disconnect();

 private:
  struct Item {
    char name[24];
    ble_gap_addr_t addr;
    int8_t rssi;
  };
  static const int MAX_ITEMS = 8, RING = 8, LINE = 160;

  static void onScan(ble_gap_evt_adv_report_t* r);
  static void onConnect(uint16_t h);
  static void onDisconnect(uint16_t h, uint8_t reason);
  static void onNotify(BLEClientCharacteristic* c, uint8_t* data, uint16_t len);
  void handleScan(ble_gap_evt_adv_report_t* r);
  void handleNotify(const uint8_t* data, uint16_t len);
  void startScan(bool fast);

  ConfigStore& cfg_;
  BLEClientService svc_;
  BLEClientCharacteristic chr_;
  volatile ViewModel::Link link_ = ViewModel::LINK_NONE;
  uint16_t conn_ = BLE_CONN_HANDLE_INVALID;
  bool listMode_ = false;
  Item items_[MAX_ITEMS];
  int nItems_ = 0;
  uint32_t connectingSince_ = 0;

  char lineBuf_[LINE];
  int lineLen_ = 0;
  char ring_[RING][LINE];
  volatile uint8_t head_ = 0, tail_ = 0;
};
