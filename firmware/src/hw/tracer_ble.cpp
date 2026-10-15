#include "hw/tracer_ble.h"

static TracerBle* self = nullptr;

static bool glob(const char* p, const char* s) {
  while (*p) {
    if (*p == '*') {
      while (*p == '*') p++;
      if (!*p) return true;
      for (; *s; s++) if (glob(p, s)) return true;
      return false;
    }
    if (!*s) return false;
    if (*p != '?' && tolower(*p) != tolower(*s)) return false;
    p++; s++;
  }
  return !*s;
}

static bool sameAddr(const ble_gap_addr_t& a, const ble_gap_addr_t& b) { return memcmp(a.addr, b.addr, 6) == 0; }

void TracerBle::begin() {
  self = this;
  svc_.begin();
  chr_.begin(&svc_);
  chr_.setNotifyCallback(onNotify);
  Bluefruit.Central.setConnectCallback(onConnect);
  Bluefruit.Central.setDisconnectCallback(onDisconnect);
  Bluefruit.Scanner.setRxCallback(onScan);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.useActiveScan(true);   // device names often live in the scan response
  link_ = cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
  if (cfg_.hasSaved()) startScan(false);
}

void TracerBle::startScan(bool fast) {
  Bluefruit.Scanner.stop();
  if (fast) Bluefruit.Scanner.setInterval(160, 120);
  else Bluefruit.Scanner.setInterval(1600, 80);   // low duty while looking for the saved vario
  Bluefruit.Scanner.start(0);
}

void TracerBle::onScan(ble_gap_evt_adv_report_t* r) { if (self) self->handleScan(r); }

void TracerBle::handleScan(ble_gap_evt_adv_report_t* r) {
  char name[24] = "";
  uint8_t n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                  (uint8_t*)name, sizeof(name) - 1);
  if (!n) n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                  (uint8_t*)name, sizeof(name) - 1);
  name[n] = 0;
  if (!n) { Bluefruit.Scanner.resume(); return; }

  if (!listMode_) {   // auto mode: connect to the saved device only, by address or by name
    // Match the saved address. Matching by name is only a fallback for devices whose address changes
    // (private addresses): with a static address two units sharing a name must not be mixed up.
    bool privateAddr = cfg_.addr().addr_type == BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE ||
                       cfg_.addr().addr_type == BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_NON_RESOLVABLE;
    if (cfg_.hasSaved() && link_ == ViewModel::LINK_SEARCHING &&
        (sameAddr(r->peer_addr, cfg_.addr()) || (privateAddr && strcmp(name, cfg_.name()) == 0))) {
      cfg_.updateAddr(r->peer_addr);   // keep the newest address
      connAddr_ = r->peer_addr;
      haveConnAddr_ = true;
      link_ = ViewModel::LINK_CONNECTING;
      Bluefruit.Central.connect(r);
      return;
    }
    Bluefruit.Scanner.resume();
    return;
  }
  if (!glob(cfg_.filter(), name)) { Bluefruit.Scanner.resume(); return; }
  int idx = -1;
  for (int i = 0; i < nItems_; i++) if (sameAddr(items_[i].addr, r->peer_addr)) idx = i;
  if (idx < 0 && nItems_ < MAX_ITEMS) idx = nItems_++;
  if (idx >= 0) {
    strncpy(items_[idx].name, name, sizeof items_[idx].name - 1);
    items_[idx].name[sizeof items_[idx].name - 1] = 0;
    items_[idx].addr = r->peer_addr;
    items_[idx].rssi = r->rssi;
  }
  Bluefruit.Scanner.resume();
}

void TracerBle::onConnect(uint16_t h) {
  if (!self) return;
  self->conn_ = h;
  self->lineLen_ = 0;
  if (!self->svc_.discover(h) || !self->chr_.discover() || !self->chr_.enableNotify()) {
    Bluefruit.disconnect(h);   // not a vario: no 0xFFE0 / 0xFFE1
    return;
  }
  self->link_ = ViewModel::LINK_CONNECTED;
}

void TracerBle::onDisconnect(uint16_t, uint8_t) {
  if (!self) return;
  self->conn_ = BLE_CONN_HANDLE_INVALID;
  self->link_ = self->cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
  if (self->cfg_.hasSaved() && !self->listMode_) self->startScan(false);
}

void TracerBle::onNotify(BLEClientCharacteristic*, uint8_t* data, uint16_t len) {
  if (self) self->handleNotify(data, len);
}

// Callback context: only assemble lines and copy them into the queue.
void TracerBle::handleNotify(const uint8_t* data, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (lineLen_ > 0) {
        lineBuf_[lineLen_] = 0;
        uint8_t nh = (head_ + 1) % RING;
        if (nh != tail_) { memcpy(ring_[head_], lineBuf_, lineLen_ + 1); head_ = nh; }   // full: drop
      }
      lineLen_ = 0;
    } else if (lineLen_ < LINE - 1) {
      lineBuf_[lineLen_++] = c;
    } else {
      lineLen_ = 0;   // overlong garbage
    }
  }
}

bool TracerBle::popLine(char* buf, size_t n) {
  if (tail_ == head_) return false;
  strncpy(buf, ring_[tail_], n - 1);
  buf[n - 1] = 0;
  tail_ = (tail_ + 1) % RING;
  return true;
}

void TracerBle::update(uint32_t now) {
  if (link_ == ViewModel::LINK_CONNECTING) {
    if (!connectingSince_) connectingSince_ = now;
    if (now - connectingSince_ > 8000) {
      connectingSince_ = 0;
      link_ = cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
      if (!listMode_) startScan(false);
    }
  } else {
    connectingSince_ = 0;
  }
}

void TracerBle::listOpen() { nItems_ = 0; listMode_ = true; startScan(true); }

void TracerBle::listClose() {
  listMode_ = false;
  if (link_ == ViewModel::LINK_CONNECTED || link_ == ViewModel::LINK_CONNECTING) { Bluefruit.Scanner.stop(); return; }
  if (cfg_.hasSaved()) { link_ = ViewModel::LINK_SEARCHING; startScan(false); }
  else Bluefruit.Scanner.stop();
}

bool TracerBle::listItem(int i, ListItem& out) {
  if (i < 0 || i >= nItems_) return false;
  Item sorted[MAX_ITEMS];                      // strongest signal first
  memcpy(sorted, items_, sizeof(Item) * nItems_);
  for (int a = 0; a < nItems_; a++)
    for (int b = a + 1; b < nItems_; b++)
      if (sorted[b].rssi > sorted[a].rssi) { Item t = sorted[a]; sorted[a] = sorted[b]; sorted[b] = t; }
  strncpy(out.name, sorted[i].name, sizeof out.name - 1);
  out.name[sizeof out.name - 1] = 0;
  out.rssi = sorted[i].rssi;
  snprintf(out.id, sizeof out.id, "%02X:%02X", sorted[i].addr.addr[1], sorted[i].addr.addr[0]);
  return true;
}

bool TracerBle::listConnect(int i) {
  if (i < 0 || i >= nItems_) return false;
  Item sorted[MAX_ITEMS];
  memcpy(sorted, items_, sizeof(Item) * nItems_);
  for (int a = 0; a < nItems_; a++)
    for (int b = a + 1; b < nItems_; b++)
      if (sorted[b].rssi > sorted[a].rssi) { Item t = sorted[a]; sorted[a] = sorted[b]; sorted[b] = t; }
  Item it = sorted[i];
  connAddr_ = it.addr;
  haveConnAddr_ = true;
  Bluefruit.Scanner.stop();
  listMode_ = false;
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
  cfg_.saveDevice(it.name, it.addr);
  link_ = ViewModel::LINK_CONNECTING;
  if (!Bluefruit.Central.connect(&it.addr)) {
    link_ = ViewModel::LINK_SEARCHING;
    startScan(false);
    return false;
  }
  return true;
}

void TracerBle::disconnect() {
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
}

void TracerBle::describe(char* buf, size_t n) const {
  const ble_gap_addr_t& s = cfg_.addr();
  snprintf(buf, n, "saved=%s %02X:%02X:%02X:%02X:%02X:%02X t%d conn=%02X:%02X:%02X:%02X:%02X:%02X",
           cfg_.name(), s.addr[5], s.addr[4], s.addr[3], s.addr[2], s.addr[1], s.addr[0], (int)s.addr_type,
           haveConnAddr_ ? connAddr_.addr[5] : 0, haveConnAddr_ ? connAddr_.addr[4] : 0,
           haveConnAddr_ ? connAddr_.addr[3] : 0, haveConnAddr_ ? connAddr_.addr[2] : 0,
           haveConnAddr_ ? connAddr_.addr[1] : 0, haveConnAddr_ ? connAddr_.addr[0] : 0);
}
