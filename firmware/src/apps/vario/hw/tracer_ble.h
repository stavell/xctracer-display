#pragma once
#include <bluefruit.h>
#include <atomic>
#include "apps/vario/app/ports.h"
#include "platform/util/spsc_ring.h"
#include "apps/vario/hw/config_store.h"

// BLE central: finds the vario by name filter, remembers it, subscribes to its $XCTRC stream (service
// 0xFFE0, characteristic 0xFFE1). The Bluetooth callbacks only copy data into queues or set flags;
// parsing, matching, config writes and Scanner/connect calls all happen in update() on the loop task.
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
  void describe(char* buf, size_t n) const override;
  void disconnect();
  void pause() override;                             // leaving the vario app (box image)
  void resume() override;

 private:
  struct Item {
    char name[24];
    ble_gap_addr_t addr;
    int8_t rssi;
  };
  struct ScanReport {
    char name[24];
    ble_gap_addr_t addr;
    int8_t rssi;
  };
  struct LineMsg { char s[160]; };
  enum ScanMode { SCAN_OFF, SCAN_FAST_RECONNECT, SCAN_LOW, SCAN_LIST };
  static const int MAX_ITEMS = 8, LINE = 160;
  static const uint32_t FAST_RECONNECT_MS = 30000;

  static void onScan(ble_gap_evt_adv_report_t* r);
  static void onConnect(uint16_t h);
  static void onDisconnect(uint16_t h, uint8_t reason);
  static void onNotify(BLEClientCharacteristic* c, uint8_t* data, uint16_t len);
  void handleNotify(const uint8_t* data, uint16_t len);
  void startScan(ScanMode m, uint32_t nowMs);
  void handleReport(const ScanReport& r, uint32_t nowMs);
  void sortedItems(Item* out) const;

  ConfigStore& cfg_;
  BLEClientService svc_;
  BLEClientCharacteristic chr_;
  std::atomic<ViewModel::Link> link_{ViewModel::LINK_NONE};
  std::atomic<bool> rescan_{false};
  volatile uint16_t conn_ = BLE_CONN_HANDLE_INVALID;
  bool listMode_ = false;
  bool paused_ = false;
  ScanMode scanMode_ = SCAN_OFF;
  uint32_t fastUntil_ = 0;
  uint32_t lastNow_ = 0;        // the `now` of the latest update(), for calls that get no time
  Item items_[MAX_ITEMS];
  int nItems_ = 0;
  uint32_t connectingSince_ = 0;
  ble_gap_addr_t connAddr_ = {};
  bool haveConnAddr_ = false;

  char lineBuf_[LINE];          // callback task only
  int lineLen_ = 0;
  SpscRing<LineMsg, 8> lines_;
  SpscRing<ScanReport, 8> reports_;
};
