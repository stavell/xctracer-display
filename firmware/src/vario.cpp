#include "vario.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

static ScanItem items[MAX_ITEMS];
static int nItems = 0;
static bool listMode = false;
static VarioState state = VS_IDLE;
static uint16_t centralHandle = BLE_CONN_HANDLE_INVALID;

struct Saved { uint32_t magic; char filter[24]; bool has; char name[24]; ble_gap_addr_t addr; };
static const uint32_t SAVED_MAGIC = 0x56415231;
static Saved sv;
static char lastRaw[96] = "";
static uint32_t lastRawMs = 0;

static void saveCfg() {
  InternalFS.remove("/vario.cfg");
  File f(InternalFS);
  if (f.open("/vario.cfg", FILE_O_WRITE)) { f.write((uint8_t*)&sv, sizeof sv); f.close(); }
}

static void loadCfg() {
  memset(&sv, 0, sizeof sv);
  File f(InternalFS);
  bool ok = false;
  if (f.open("/vario.cfg", FILE_O_READ)) {
    ok = f.read((uint8_t*)&sv, sizeof sv) == sizeof sv && sv.magic == SAVED_MAGIC;
    f.close();
  }
  if (!ok) {
    memset(&sv, 0, sizeof sv);
    sv.magic = SAVED_MAGIC;
    strcpy(sv.filter, "XC*");
  }
}

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

static bool sameAddr(const ble_gap_addr_t& a, const ble_gap_addr_t& b) {
  return memcmp(a.addr, b.addr, 6) == 0;
}

static void startScan(bool fast) {
  Bluefruit.Scanner.stop();
  if (fast) Bluefruit.Scanner.setInterval(160, 120);   // 100 ms / 75 ms
  else Bluefruit.Scanner.setInterval(1600, 80);        // 1 s / 50 ms, low duty
  Bluefruit.Scanner.start(0);
}

static void onScan(ble_gap_evt_adv_report_t* r) {
  char name[24] = "";
  uint8_t n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                  (uint8_t*)name, sizeof(name) - 1);
  if (!n) n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                  (uint8_t*)name, sizeof(name) - 1);
  name[n] = 0;
  if (!n || !glob(sv.filter, name)) { Bluefruit.Scanner.resume(); return; }

  if (!listMode) {   // auto mode: connect to the saved device only
    if (sv.has && sameAddr(r->peer_addr, sv.addr) && state == VS_SEARCHING) {
      state = VS_CONNECTING;
      Bluefruit.Central.connect(r);
      return;
    }
    Bluefruit.Scanner.resume();
    return;
  }
  int idx = -1;
  for (int i = 0; i < nItems; i++) if (sameAddr(items[i].addr, r->peer_addr)) idx = i;
  if (idx < 0 && nItems < MAX_ITEMS) idx = nItems++;
  if (idx >= 0) {
    strncpy(items[idx].name, name, sizeof items[idx].name);
    items[idx].addr = r->peer_addr;
    items[idx].rssi = r->rssi;
    items[idx].seenMs = millis();
  }
  Bluefruit.Scanner.resume();
}

static void onCentralConnect(uint16_t h) {
  centralHandle = h;
  state = VS_CONNECTED;
  Serial.println("vario: connected");
}

static void onCentralDisconnect(uint16_t, uint8_t reason) {
  centralHandle = BLE_CONN_HANDLE_INVALID;
  state = sv.has ? VS_SEARCHING : VS_IDLE;
  Serial.printf("vario: disconnected (0x%02x)\n", reason);
}

void varioBegin() {
  loadCfg();
  Bluefruit.Central.setConnectCallback(onCentralConnect);
  Bluefruit.Central.setDisconnectCallback(onCentralDisconnect);
  Bluefruit.Scanner.setRxCallback(onScan);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.useActiveScan(true);   // device names often live in the scan response
  state = sv.has ? VS_SEARCHING : VS_IDLE;
  if (state == VS_SEARCHING) startScan(false);
}

void varioUpdate() {
  // if a connect attempt timed out, resume searching
  static uint32_t connectingSince = 0;
  if (state == VS_CONNECTING) {
    if (!connectingSince) connectingSince = millis();
    if (millis() - connectingSince > 8000) {
      connectingSince = 0;
      state = sv.has ? VS_SEARCHING : VS_IDLE;
      if (!listMode) startScan(false);
    }
  } else {
    connectingSince = 0;
  }
}

VarioState varioState() { return state; }
const char* varioDeviceName() { return sv.has ? sv.name : ""; }
bool varioHasSaved() { return sv.has; }

void varioListOpen() {
  nItems = 0;
  listMode = true;
  startScan(true);
}

void varioListClose() {
  listMode = false;
  if (state == VS_CONNECTED || state == VS_CONNECTING) { Bluefruit.Scanner.stop(); return; }
  if (sv.has) { state = VS_SEARCHING; startScan(false); }
  else Bluefruit.Scanner.stop();
}

int varioListCount() { return nItems; }

const ScanItem& varioListItem(int i) {
  // sorted view: selection sort on demand, strongest first
  static ScanItem sorted[MAX_ITEMS];
  memcpy(sorted, items, sizeof(ScanItem) * nItems);
  for (int a = 0; a < nItems; a++)
    for (int b = a + 1; b < nItems; b++)
      if (sorted[b].rssi > sorted[a].rssi) { ScanItem t = sorted[a]; sorted[a] = sorted[b]; sorted[b] = t; }
  return sorted[i];
}

bool varioListConnect(int i) {
  if (i < 0 || i >= nItems) return false;
  ScanItem it = varioListItem(i);
  Bluefruit.Scanner.stop();
  listMode = false;
  if (state == VS_CONNECTED) Bluefruit.disconnect(centralHandle);
  sv.has = true;
  strncpy(sv.name, it.name, sizeof sv.name);
  sv.addr = it.addr;
  saveCfg();
  state = VS_CONNECTING;
  if (!Bluefruit.Central.connect(&it.addr)) {
    state = VS_SEARCHING;
    startScan(false);
    return false;
  }
  return true;
}

void varioDisconnect() {
  if (centralHandle != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(centralHandle);
}

void varioForget() {
  sv.has = false;
  sv.name[0] = 0;
  saveCfg();
  Bluefruit.Scanner.stop();
  varioDisconnect();
  state = VS_IDLE;
}

const char* varioFilter() { return sv.filter; }
void varioSetFilter(const char* f) {
  strncpy(sv.filter, f, sizeof sv.filter - 1);
  sv.filter[sizeof sv.filter - 1] = 0;
  saveCfg();
}

const char* varioLastRaw() { return lastRaw; }
uint32_t varioLastRawMs() { return lastRawMs; }
