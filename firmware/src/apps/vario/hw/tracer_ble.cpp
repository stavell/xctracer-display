#include "apps/vario/hw/tracer_ble.h"
#include "apps/vario/core/devmatch.h"

static TracerBle* self = nullptr;

static DevAddr toDev(const ble_gap_addr_t& a) {
  DevAddr d;
  memcpy(d.addr, a.addr, 6);
  d.type = a.addr_type;
  return d;
}

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
  if (cfg_.hasSaved()) startScan(SCAN_LOW, 0);
}

// Loop context only (the scanner is never touched from a callback except resume()).
void TracerBle::startScan(ScanMode m, uint32_t now) {
  Bluefruit.Scanner.stop();
  switch (m) {
    case SCAN_FAST_RECONNECT:   // just lost the vario: full duty for a while
      Bluefruit.Scanner.setInterval(160, 160);
      fastUntil_ = now + FAST_RECONNECT_MS;
      break;
    case SCAN_LIST:
      Bluefruit.Scanner.setInterval(160, 120);
      break;
    default:
      m = SCAN_LOW;
      Bluefruit.Scanner.setInterval(1600, 80);   // low duty while looking for the saved vario
      break;
  }
  scanMode_ = m;
  Bluefruit.Scanner.start(0);
}

// Callback context: parse the name, queue the report, keep the scanner going.
void TracerBle::onScan(ble_gap_evt_adv_report_t* r) {
  if (!self) return;
  ScanReport rep;
  memset(rep.name, 0, sizeof rep.name);
  uint8_t n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                  (uint8_t*)rep.name, sizeof(rep.name) - 1);
  if (!n) n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                  (uint8_t*)rep.name, sizeof(rep.name) - 1);
  rep.name[n] = 0;
  if (n) {
    rep.addr = r->peer_addr;
    rep.rssi = r->rssi;
    self->reports_.push(rep);   // full: drop, the next advertisement repeats it
  }
  Bluefruit.Scanner.resume();
}

// A connect that is still pending when the box goes quiet would otherwise complete later (the SoftDevice waits forever) and
// keep a link up through the clock sleep. Harmless when none is pending.
static void cancelConnect() { sd_ble_gap_connect_cancel(); }

void TracerBle::onConnect(uint16_t h) {
  if (!self) return;
  if (self->paused_) { Bluefruit.disconnect(h); return; }   // a connect that raced pause()
  // A new link supersedes a disconnect the loop has not processed yet (e.g. the old link dropped when a
  // new tracer was picked from the list): without this, update() would demote the fresh link.
  self->rescan_ = false;
  self->conn_ = h;
  self->lineLen_ = 0;
  if (!self->svc_.discover(h) || !self->chr_.discover() || !self->chr_.enableNotify()) {
    self->link_ = self->cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
    Bluefruit.disconnect(h);   // not a vario: no 0xFFE0 / 0xFFE1
    return;
  }
  self->link_ = ViewModel::LINK_CONNECTED;
}

// Callback context: state only. update() demotes the link and restarts the scan.
void TracerBle::onDisconnect(uint16_t h, uint8_t) {
  if (!self || h != self->conn_) return;   // not the current link
  self->conn_ = BLE_CONN_HANDLE_INVALID;
  self->rescan_ = true;
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
        LineMsg m;
        memcpy(m.s, lineBuf_, lineLen_);
        m.s[lineLen_] = 0;
        lines_.push(m);   // full: drop
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
  LineMsg m;
  if (!lines_.pop(m)) return false;
  strncpy(buf, m.s, n - 1);
  buf[n - 1] = 0;
  return true;
}

void TracerBle::handleReport(const ScanReport& r, uint32_t now) {
  if (listMode_) {
    if (!globMatch(cfg_.filter(), r.name)) return;
    int idx = -1;
    for (int i = 0; i < nItems_; i++) if (sameAddr(toDev(items_[i].addr), toDev(r.addr))) idx = i;
    if (idx < 0 && nItems_ < MAX_ITEMS) idx = nItems_++;
    if (idx >= 0) {
      strncpy(items_[idx].name, r.name, sizeof items_[idx].name - 1);
      items_[idx].name[sizeof items_[idx].name - 1] = 0;
      items_[idx].addr = r.addr;
      items_[idx].rssi = r.rssi;
    }
    return;
  }
  if (link_ != ViewModel::LINK_SEARCHING) return;
  if (!shouldAutoConnect(cfg_.hasSaved(), toDev(cfg_.addr()), cfg_.name(), toDev(r.addr), r.name)) return;
  Bluefruit.Scanner.stop();
  cfg_.updateAddr(r.addr);   // keep the newest address
  connAddr_ = r.addr;
  haveConnAddr_ = true;
  link_ = ViewModel::LINK_CONNECTING;
  connectingSince_ = now;
  ble_gap_addr_t a = r.addr;
  if (!Bluefruit.Central.connect(&a)) {
    link_ = ViewModel::LINK_SEARCHING;
    startScan(SCAN_LOW, now);
  }
}

void TracerBle::update(uint32_t now) {
  lastNow_ = now;
  if (rescan_.exchange(false)) {
    if (link_ == ViewModel::LINK_CONNECTED)
      link_ = cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
    // CONNECTING here means the disconnect belongs to a link we replaced: leave the new attempt alone.
    if (link_ == ViewModel::LINK_SEARCHING && !listMode_) startScan(SCAN_FAST_RECONNECT, now);
  }

  ScanReport rep;
  while (reports_.pop(rep)) handleReport(rep, now);

  if (link_ == ViewModel::LINK_CONNECTING) {
    if (!connectingSince_) connectingSince_ = now;
    if (now - connectingSince_ > 8000) {
      connectingSince_ = 0;
      cancelConnect();
      link_ = cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
      if (!listMode_) startScan(SCAN_FAST_RECONNECT, now);
    }
  } else {
    connectingSince_ = 0;
  }

  if (scanMode_ == SCAN_FAST_RECONNECT && (int32_t)(now - fastUntil_) >= 0 && !listMode_ &&
      link_ == ViewModel::LINK_SEARCHING)
    startScan(SCAN_LOW, now);
}

void TracerBle::listOpen() {
  nItems_ = 0;
  listMode_ = true;
  startScan(SCAN_LIST, lastNow_);
}

void TracerBle::listClose() {
  listMode_ = false;
  if (link_ == ViewModel::LINK_CONNECTED || link_ == ViewModel::LINK_CONNECTING) {
    Bluefruit.Scanner.stop();
    scanMode_ = SCAN_OFF;
    return;
  }
  if (cfg_.hasSaved()) { link_ = ViewModel::LINK_SEARCHING; startScan(SCAN_LOW, lastNow_); }
  else { Bluefruit.Scanner.stop(); scanMode_ = SCAN_OFF; }
}

void TracerBle::sortedItems(Item* out) const {   // strongest signal first
  int8_t rssi[MAX_ITEMS];
  int order[MAX_ITEMS];
  for (int i = 0; i < nItems_; i++) rssi[i] = items_[i].rssi;
  orderByRssi(rssi, nItems_, order);
  for (int i = 0; i < nItems_; i++) out[i] = items_[order[i]];
}

bool TracerBle::listItem(int i, ListItem& out) {
  if (i < 0 || i >= nItems_) return false;
  Item sorted[MAX_ITEMS];
  sortedItems(sorted);
  strncpy(out.name, sorted[i].name, sizeof out.name - 1);
  out.name[sizeof out.name - 1] = 0;
  out.rssi = sorted[i].rssi;
  snprintf(out.id, sizeof out.id, "%02X:%02X", sorted[i].addr.addr[1], sorted[i].addr.addr[0]);
  return true;
}

bool TracerBle::listConnect(int i) {
  if (i < 0 || i >= nItems_) return false;
  Item sorted[MAX_ITEMS];
  sortedItems(sorted);
  Item it = sorted[i];
  connAddr_ = it.addr;
  haveConnAddr_ = true;
  Bluefruit.Scanner.stop();
  scanMode_ = SCAN_OFF;
  listMode_ = false;
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
  cfg_.saveDevice(it.name, it.addr);
  link_ = ViewModel::LINK_CONNECTING;
  connectingSince_ = lastNow_;
  if (!Bluefruit.Central.connect(&it.addr)) {
    link_ = ViewModel::LINK_SEARCHING;
    startScan(SCAN_LOW, lastNow_);
    return false;
  }
  return true;
}

void TracerBle::disconnect() {
  if (link_ == ViewModel::LINK_CONNECTING) cancelConnect();   // power off mid-connect (update() rescans after a wake)
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);
}

// The vario app is left (box image). Loop context. Stop the scanner, drop the link, forget what was queued.
// Nothing here runs unless the box image calls it: the vario-only image never pauses.
void TracerBle::pause() {
  if (paused_) return;
  paused_ = true;
  listMode_ = false;
  Bluefruit.Scanner.stop();
  scanMode_ = SCAN_OFF;
  if (link_ == ViewModel::LINK_CONNECTING) cancelConnect();
  connectingSince_ = 0;
  if (conn_ != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(conn_);   // the callback only sets rescan_
  link_ = ViewModel::LINK_NONE;
  LineMsg m;
  while (lines_.pop(m)) {}
  ScanReport r;
  while (reports_.pop(r)) {}
}

// Back in the vario app: look for the saved tracer again (a few seconds until the link is up).
void TracerBle::resume() {
  if (!paused_) return;
  paused_ = false;
  rescan_ = false;                                       // a disconnect that finished meanwhile is not news
  LineMsg m;
  while (lines_.pop(m)) {}
  ScanReport r;
  while (reports_.pop(r)) {}
  link_ = cfg_.hasSaved() ? ViewModel::LINK_SEARCHING : ViewModel::LINK_NONE;
  if (cfg_.hasSaved()) startScan(SCAN_LOW, lastNow_);
}

void TracerBle::describe(char* buf, size_t n) const {
  const ble_gap_addr_t& s = cfg_.addr();
  snprintf(buf, n, "saved=%s %02X:%02X:%02X:%02X:%02X:%02X t%d conn=%02X:%02X:%02X:%02X:%02X:%02X",
           cfg_.name(), s.addr[5], s.addr[4], s.addr[3], s.addr[2], s.addr[1], s.addr[0], (int)s.addr_type,
           haveConnAddr_ ? connAddr_.addr[5] : 0, haveConnAddr_ ? connAddr_.addr[4] : 0,
           haveConnAddr_ ? connAddr_.addr[3] : 0, haveConnAddr_ ? connAddr_.addr[2] : 0,
           haveConnAddr_ ? connAddr_.addr[1] : 0, haveConnAddr_ ? connAddr_.addr[0] : 0);
}
