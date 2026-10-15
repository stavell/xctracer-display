#include "vario.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

static const int VARIO_AVG_S = 20;

static ScanItem items[MAX_ITEMS];
static int nItems = 0;
static bool listMode = false;
static VarioState state = VS_IDLE;
static uint16_t centralHandle = BLE_CONN_HANDLE_INVALID;

struct Saved { uint32_t magic; char filter[24]; bool has; char name[24]; ble_gap_addr_t addr; int16_t tz; };
static const uint32_t SAVED_MAGIC = 0x56415232;
static Saved sv;

static BLEClientService tracerSvc(0xFFE0);
static BLEClientCharacteristic tracerChr(0xFFE1);

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
    strcpy(sv.filter, "*");
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

static bool sameAddr(const ble_gap_addr_t& a, const ble_gap_addr_t& b) { return memcmp(a.addr, b.addr, 6) == 0; }

// ---- scanning ----------------------------------------------------------------
static void startScan(bool fast) {
  Bluefruit.Scanner.stop();
  if (fast) Bluefruit.Scanner.setInterval(160, 120);
  else Bluefruit.Scanner.setInterval(1600, 80);   // low duty while looking for the saved vario
  Bluefruit.Scanner.start(0);
}

static void onScan(ble_gap_evt_adv_report_t* r) {
  char name[24] = "";
  uint8_t n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                  (uint8_t*)name, sizeof(name) - 1);
  if (!n) n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                  (uint8_t*)name, sizeof(name) - 1);
  name[n] = 0;
  if (!n) { Bluefruit.Scanner.resume(); return; }

  if (!listMode) {   // auto mode: connect to the saved device only
    // match the saved address, or the saved name (covers devices that change address)
    if (sv.has && state == VS_SEARCHING && (sameAddr(r->peer_addr, sv.addr) || strcmp(name, sv.name) == 0)) {
      sv.addr = r->peer_addr;   // keep the newest address
      state = VS_CONNECTING;
      Bluefruit.Central.connect(r);
      return;
    }
    Bluefruit.Scanner.resume();
    return;
  }
  if (!glob(sv.filter, name)) { Bluefruit.Scanner.resume(); return; }
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

// ---- $XCTRC stream -------------------------------------------------------------
static const int RING = 8;
static VarioData ring[RING];
static volatile uint8_t ringHead = 0, ringTail = 0;
static char lineBuf[160];
static int lineLen = 0;
static char lastRaw[96] = "";
static volatile uint32_t lines = 0, badLines = 0;

static VarioData cur;
static uint32_t curMs = 0;

static bool parseXctrc(char* s, VarioData& o) {
  // verify "*HH" checksum: XOR of everything between '$' and '*'
  char* star = strrchr(s, '*');
  if (s[0] != '$' || !star || strlen(star) < 3) return false;
  uint8_t cs = 0;
  for (char* p = s + 1; p < star; p++) cs ^= (uint8_t)*p;
  if (cs != (uint8_t)strtoul(star + 1, nullptr, 16)) return false;
  *star = 0;
  char* f[24];
  int n = 0;
  char* p = s;
  while (n < 24) {
    f[n++] = p;
    char* c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  if (n < 19 || strcmp(f[0], "$XCTRC") != 0) return false;
  auto num = [&](int i) -> float { return f[i][0] ? (float)atof(f[i]) : NAN; };
  o.hh = atoi(f[4]); o.mm = atoi(f[5]); o.ss = atoi(f[6]);
  o.lat = atof(f[8]);
  o.lon = atof(f[9]);
  o.fix = !(o.lat == 0.0 && o.lon == 0.0);
  o.alt = num(10);
  o.speedKmh = o.fix ? num(11) : NAN;
  o.course = o.fix ? num(12) : NAN;
  o.vario = num(13);
  o.pressure = num(17);
  o.battPct = f[18][0] ? atoi(f[18]) : -1;
  return true;
}

static void pushLine(char* s) {
  VarioData d;
  strncpy(lastRaw, s, sizeof lastRaw - 1);
  lastRaw[sizeof lastRaw - 1] = 0;
  char tmp[160];
  strncpy(tmp, s, sizeof tmp - 1);
  tmp[sizeof tmp - 1] = 0;
  if (!parseXctrc(tmp, d)) { badLines++; return; }
  uint8_t nh = (ringHead + 1) % RING;
  if (nh == ringTail) return;   // full, drop
  ring[ringHead] = d;
  ringHead = nh;
  lines++;
}

static uint32_t injectMs = 0;
static bool simOnFwd();   // defined with the simulator below
static bool injecting() { return simOnFwd() || (injectMs && millis() - injectMs < 3000); }

void varioInject(const char* line) {
  injectMs = millis();
  char tmp[160];
  strncpy(tmp, line, sizeof tmp - 1);
  tmp[sizeof tmp - 1] = 0;
  pushLine(tmp);
}

static void onNotify(BLEClientCharacteristic*, uint8_t* data, uint16_t len) {
  if (injecting()) { lineLen = 0; return; }   // replay / simulator in charge: ignore the live tracer
  for (uint16_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) { lineBuf[lineLen] = 0; pushLine(lineBuf); }
      lineLen = 0;
    } else if (lineLen < (int)sizeof lineBuf - 1) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;   // overlong garbage
    }
  }
}

static void onCentralConnect(uint16_t h) {
  centralHandle = h;
  lineLen = 0;
  if (!tracerSvc.discover(h) || !tracerChr.discover() || !tracerChr.enableNotify()) {
    Serial.println("vario: service 0xFFE0/0xFFE1 not found, dropping");
    Bluefruit.disconnect(h);
    return;
  }
  state = VS_CONNECTED;
  Serial.println("vario: connected, streaming");
}

static void onCentralDisconnect(uint16_t, uint8_t reason) {
  centralHandle = BLE_CONN_HANDLE_INVALID;
  state = sv.has ? VS_SEARCHING : VS_IDLE;
  Serial.printf("vario: disconnected (0x%02x)\n", reason);
  if (sv.has && !listMode) startScan(false);
}

// ---- derived flight data -----------------------------------------------------------
static uint32_t dataMs = 1000;     // flight clock driven by the data's own time stamps
static int lastKey = -1;           // seconds of day of the last sample
static VarioSecond hist[VARIO_HIST];
static int histN = 0;
static bool flying = false;
static uint32_t flightStartMs = 0, slowSinceMs = 0, fastSinceMs = 0;
static bool circling = false;
static uint32_t lastTurnMs = 0;

static float wrap180(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

// ---- wind ------------------------------------------------------------------------------
static VarioWind wnd = {false, 0, 0, 0};
static double wx = 0, wy = 0;          // east, north wind vector, m/s
static uint32_t wndMs = 0;

static double det3(const double m[3][3]) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
       - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
       + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// Fit a circle to ground velocity vectors: centre = wind, radius = airspeed (Kasa fit).
static void updateWind(uint32_t now) {
  const int W = 30;
  if (histN < W) return;
  double vx[W], vy[W], ang[W];
  for (int i = 0; i < W; i++) {
    const VarioSecond& s = hist[histN - W + i];
    if (!s.fix || !isfinite(s.speedKmh) || !isfinite(s.course) || s.speedKmh < 8) return;
    double ms = s.speedKmh / 3.6;
    vx[i] = ms * sin(s.course * DEG_TO_RAD);
    vy[i] = ms * cos(s.course * DEG_TO_RAD);
    ang[i] = fmod(s.course + 360.0, 360.0);
  }
  // angular coverage = 360 - largest gap between sorted headings
  for (int i = 1; i < W; i++) {                      // insertion sort
    double a = ang[i]; int j = i - 1;
    while (j >= 0 && ang[j] > a) { ang[j + 1] = ang[j]; j--; }
    ang[j + 1] = a;
  }
  double gap = 0;
  for (int i = 0; i < W; i++) gap = max(gap, fmod(ang[(i + 1) % W] - ang[i] + 360.0, 360.0));
  if (360.0 - gap < 200.0) return;

  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sz = 0, sxz = 0, syz = 0;
  for (int i = 0; i < W; i++) {
    double z = vx[i] * vx[i] + vy[i] * vy[i];
    sx += vx[i]; sy += vy[i]; sxx += vx[i] * vx[i]; syy += vy[i] * vy[i]; sxy += vx[i] * vy[i];
    sz += z; sxz += vx[i] * z; syz += vy[i] * z;
  }
  double A[3][3] = {{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, (double)W}};
  double B[3] = {sxz, syz, sz};
  double D = det3(A);
  if (fabs(D) < 1e-6) return;
  double sol[3];
  for (int k = 0; k < 3; k++) {
    double M[3][3];
    memcpy(M, A, sizeof M);
    for (int i = 0; i < 3; i++) M[i][k] = B[i];
    sol[k] = det3(M) / D;
  }
  double a = sol[0] / 2, b = sol[1] / 2;
  double r2 = sol[2] + a * a + b * b;
  if (r2 <= 0) return;
  double r = sqrt(r2);
  double res = 0;
  for (int i = 0; i < W; i++) { double e = hypot(vx[i] - a, vy[i] - b) - r; res += e * e; }
  res = sqrt(res / W);
  double wsKmh = hypot(a, b) * 3.6;
  if (r * 3.6 < 14 || r * 3.6 > 72 || res > 0.2 * r || wsKmh > 72) return;   // not a believable airspeed circle

  if (!wnd.valid || now - wndMs > 120000) { wx = a; wy = b; }
  else { wx = 0.88 * wx + 0.12 * a; wy = 0.88 * wy + 0.12 * b; }   // smooth over ~8 s
  wndMs = now;
  wnd.valid = true;
}

VarioWind varioWind() {
  VarioWind w = wnd;
  if (w.valid) {
    w.speedKmh = hypot(wx, wy) * 3.6;
    w.toDeg = fmod(atan2(wx, wy) / DEG_TO_RAD + 360.0, 360.0);
    w.ageS = (dataMs - wndMs) / 1000;
  }
  return w;
}

static void tickSecond(uint32_t now) {
  VarioSecond s = {cur.fix, cur.lat, cur.lon, cur.alt, cur.vario, cur.speedKmh, cur.course};
  if (histN == VARIO_HIST) { memmove(hist, hist + 1, sizeof(VarioSecond) * (VARIO_HIST - 1)); histN--; }
  hist[histN++] = s;

  // takeoff / landing from ground speed
  float sp = cur.fix ? cur.speedKmh : 0;
  if (sp > 10) { if (!fastSinceMs) fastSinceMs = now; slowSinceMs = 0; }
  else { fastSinceMs = 0; if (!slowSinceMs) slowSinceMs = now; }
  if (!flying && fastSinceMs && now - fastSinceMs > 5000) { flying = true; flightStartMs = fastSinceMs; }
  if (flying && slowSinceMs && now - slowSinceMs > 60000) flying = false;

  // circling: mean absolute turn rate over the last 8 s
  if (histN >= 9 && cur.fix) {
    float sum = 0;
    for (int i = histN - 8; i < histN; i++) sum += fabsf(wrap180(hist[i].course - hist[i - 1].course));
    if (sum / 8.0f > 6.0f && sp > 8) { circling = true; lastTurnMs = now; }
  }
  if (circling && now - lastTurnMs > 16000) circling = false;   // straight again for 16 s
  updateWind(now);
}

// ---- simulator -------------------------------------------------------------------------
static bool simOn = false;
static uint32_t simT0 = 0, simLast = 0;
static double simX = 0, simY = 0, simAlt = 1500;
static float simCourse = 90;

static void simStep(uint32_t now) {
  if (now - simLast < 330) return;
  float dt = (now - simLast) / 1000.0f;
  simLast = now;
  float t = fmodf((now - simT0) / 1000.0f, 150.0f);
  const float tcx = 900, tcy = 0;   // thermal centre 900 m east of the start
  float spd, turn, v;
  if (t < 40 || t >= 110) {                   // glide
    spd = 38; turn = 0; v = -1.1f;
    if (t >= 110) { float want = 90; turn = wrap180(want - simCourse) * 0.3f; }
  } else {                                    // circle in the thermal
    spd = 30; turn = 18;
    float dx = simX - tcx, dy = simY - tcy;
    float d = sqrtf(dx * dx + dy * dy);
    v = 2.6f * expf(-(d / 55.0f) * (d / 55.0f)) - 0.5f;
  }
  simCourse = fmodf(simCourse + turn * dt + 360, 360);
  float ms = spd / 3.6f;
  const float windE = -3.33f, windN = 0;   // 12 km/h toward 270
  float gx = ms * sinf(simCourse * DEG_TO_RAD) + windE;
  float gy = ms * cosf(simCourse * DEG_TO_RAD) + windN;
  simX += gx * dt;
  simY += gy * dt;
  float gcourse = fmodf(atan2f(gx, gy) / DEG_TO_RAD + 360, 360);
  float gspeed = sqrtf(gx * gx + gy * gy) * 3.6f;
  simAlt += v * dt;
  VarioData d;
  d.fix = true;
  d.lat = 42.0 + simY / 111320.0;
  d.lon = 23.0 + simX / (111320.0 * cos(42.0 * DEG_TO_RAD));
  d.alt = simAlt;
  d.speedKmh = gspeed;
  d.course = gcourse;
  d.vario = v + 0.08f * sinf(now / 300.0f);
  d.pressure = 1013.25f * powf(1 - simAlt / 44330.0f, 5.255f);
  d.battPct = 80;
  uint32_t secs = 12 * 3600 + (now - simT0) / 1000;
  d.hh = (secs / 3600) % 24; d.mm = (secs / 60) % 60; d.ss = secs % 60;
  uint8_t nh = (ringHead + 1) % RING;
  if (nh != ringTail) { ring[ringHead] = d; ringHead = nh; lines++; }
}

void varioSim(bool on) {
  simOn = on;
  simT0 = simLast = millis();
  simX = simY = 0; simAlt = 1500; simCourse = 90;
  histN = 0; flying = false; circling = false; lastKey = -1;
}
bool varioSimOn() { return simOn; }
static bool simOnFwd() { return simOn; }

// ---- public ---------------------------------------------------------------------------------
void varioBegin() {
  loadCfg();
  tracerSvc.begin();
  tracerChr.begin(&tracerSvc);
  tracerChr.setNotifyCallback(onNotify);
  Bluefruit.Central.setConnectCallback(onCentralConnect);
  Bluefruit.Central.setDisconnectCallback(onCentralDisconnect);
  Bluefruit.Scanner.setRxCallback(onScan);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.useActiveScan(true);   // device names often live in the scan response
  state = sv.has ? VS_SEARCHING : VS_IDLE;
  if (state == VS_SEARCHING) startScan(false);
}

void varioUpdate() {
  uint32_t now = millis();
  static uint32_t connectingSince = 0;
  if (state == VS_CONNECTING) {
    if (!connectingSince) connectingSince = now;
    if (now - connectingSince > 8000) {
      connectingSince = 0;
      state = sv.has ? VS_SEARCHING : VS_IDLE;
      if (!listMode) startScan(false);
    }
  } else {
    connectingSince = 0;
  }

  if (simOn) simStep(now);
  while (ringTail != ringHead) {
    cur = ring[ringTail];
    ringTail = (ringTail + 1) % RING;
    curMs = now;
    int key = cur.hh * 3600 + cur.mm * 60 + cur.ss;
    if (key != lastKey) {   // a new data second: advance the flight clock and the 1 Hz history
      if (lastKey >= 0) dataMs += (uint32_t)min((key - lastKey + 86400) % 86400, 5) * 1000u;
      lastKey = key;
      tickSecond(dataMs);
    }
  }
}

VarioState varioState() { return state; }
const char* varioDeviceName() { return sv.has ? sv.name : ""; }
bool varioHasSaved() { return sv.has; }

void varioListOpen() { nItems = 0; listMode = true; startScan(true); }

void varioListClose() {
  listMode = false;
  if (state == VS_CONNECTED || state == VS_CONNECTING) { Bluefruit.Scanner.stop(); return; }
  if (sv.has) { state = VS_SEARCHING; startScan(false); }
  else Bluefruit.Scanner.stop();
}

int varioListCount() { return nItems; }

const ScanItem& varioListItem(int i) {
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
  if (centralHandle != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(centralHandle);
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

void varioDisconnect() { if (centralHandle != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(centralHandle); }

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
  strncpy(sv.filter, f[0] ? f : "*", sizeof sv.filter - 1);
  sv.filter[sizeof sv.filter - 1] = 0;
  saveCfg();
}

int16_t varioTz() { return sv.tz; }
void varioSetTz(int16_t m) { sv.tz = m; saveCfg(); }

const VarioData& varioData() { return cur; }
uint32_t varioAgeMs() { return curMs ? millis() - curMs : 0xFFFFFFFF; }
bool varioFresh() { return curMs && millis() - curMs < 3000; }
uint32_t varioLines() { return lines; }
uint32_t varioBadLines() { return badLines; }
const char* varioLastRaw() { return lastRaw; }

float varioAvg() {
  int n = min(histN, VARIO_AVG_S);
  if (!n) return isnan(cur.vario) ? 0 : cur.vario;
  float s = 0;
  int c = 0;
  for (int i = histN - n; i < histN; i++) if (!isnan(hist[i].vario)) { s += hist[i].vario; c++; }
  return c ? s / c : 0;
}

float varioGlide() {
  int n = min(histN, 20);
  if (n < 5) return NAN;
  float sv_ = 0, ss = 0;
  int c = 0;
  for (int i = histN - n; i < histN; i++) {
    if (!hist[i].fix || isnan(hist[i].vario) || isnan(hist[i].speedKmh)) continue;
    sv_ += hist[i].vario; ss += hist[i].speedKmh / 3.6f; c++;
  }
  if (c < 5) return NAN;
  float v = sv_ / c, sp = ss / c;
  if (v > -0.2f) return NAN;
  float g = sp / -v;
  return g > 99 ? 99 : g;
}

bool varioFlying() { return flying; }
uint32_t varioFlightSec() { return flying ? (dataMs - flightStartMs) / 1000 : 0; }
bool varioCircling() { return circling; }
int varioHistCount() { return histN; }
const VarioSecond& varioHist(int i) { return hist[i]; }

int varioLocalMinutes() {
  int m = cur.hh * 60 + cur.mm + sv.tz;
  return ((m % 1440) + 1440) % 1440;
}
