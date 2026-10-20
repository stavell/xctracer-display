#include "platform/hw/laptop_link.h"

static LaptopLink* self = nullptr;

void LaptopLink::begin() {
  self = this;
  svc_.begin();
  chr_.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  chr_.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr_.setMaxLen(244);
  chr_.setWriteCallback(onWrite);
  chr_.begin();
  status_.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  status_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  status_.setFixedLen(pet::STATUS_BYTES);
  status_.begin();
  uint8_t zero[pet::STATUS_BYTES] = {};
  status_.write(zero, sizeof zero);
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(svc_);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(1600, 3200);   // 1 s - 2 s, units of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void LaptopLink::onConnect(uint16_t h) { if (self) self->conn_ = h; }
void LaptopLink::onDisconnect(uint16_t, uint8_t) { if (self) self->conn_ = BLE_CONN_HANDLE_INVALID; }
void LaptopLink::onWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  if (self) self->dispatch_.dispatch(data, len, millis());
}

void LaptopLink::reportStatus(const uint8_t* data, uint16_t len) {
  status_.write(data, len);
  const uint16_t h = conn_;
  if (h != BLE_CONN_HANDLE_INVALID && status_.notifyEnabled(h)) status_.notify(h, data, len);
}

void LaptopLink::disconnect() {
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
}

void LaptopLink::startAdvertising() {
  Bluefruit.Advertising.restartOnDisconnect(true);
  if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
}

void LaptopLink::stopAdvertising() {
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
}
