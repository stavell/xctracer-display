#include "ui.h"
#include <math.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "vario.h"
#include "battery.h"

static const int W = 400, H = 240, HDR = 24;
static const uint16_t BLK = 0, WHT = 1;
static const float PI_F = 3.14159265f;

#define F9   (&FreeSans9pt7b)
#define F12B (&FreeSansBold12pt7b)
#define F18B (&FreeSansBold18pt7b)
#define F24B (&FreeSansBold24pt7b)

static Adafruit_SharpMem* d;
static uint8_t* snap = nullptr;
static size_t bufLen = 0;
static bool dirty = true;

enum Align { AL, AC, AR };

static void text(int x, int y, const char* s, const GFXfont* f, Align a = AL, uint16_t col = BLK, int sc = 1) {
  d->setFont(f);
  d->setTextSize(sc);
  d->setTextColor(col);
  int16_t x1, y1;
  uint16_t w, h;
  d->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int cx = x - x1;
  if (a == AC) cx = x - (int)w / 2 - x1;
  else if (a == AR) cx = x - (int)w - x1;
  d->setCursor(cx, y);
  d->print(s);
  d->setTextSize(1);
}

static int textW(const char* s, const GFXfont* f) {
  d->setFont(f);
  int16_t x1, y1;
  uint16_t w, h;
  d->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// The one place a float becomes text: anything that is not a finite number prints "--".
static const char* fnum(char* b, size_t n, float v, const char* fmt) {
  if (!isfinite(v)) snprintf(b, n, "--");
  else snprintf(b, n, fmt, v);
  return b;
}

static void hbar(int x, int y, int w, int h, float frac) {
  if (!isfinite(frac)) frac = 0;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d->drawRect(x, y, w, h, BLK);
  d->fillRect(x + 2, y + 2, (int)((w - 4) * frac), h - 4, BLK);
}

static const char* NAMES[PAGE_COUNT] = {"VARIO", "VARIO SIMPLE", "VARIO THERMAL"};

static void header(const char* title, int cur) {
  d->fillRect(0, 0, W, HDR, BLK);
  text(8, 17, title, F9, AL, WHT);
  for (int i = 0; cur >= 0 && i < PAGE_COUNT; i++) {
    int x = 270 + i * 11 - (PAGE_COUNT - 1) * 5;
    if (i == cur) d->fillCircle(x, 12, 3, WHT);
    else d->drawCircle(x, 12, 3, WHT);
  }
  int pct = batteryPct();
  char b[8];
  snprintf(b, sizeof b, "%d%%", pct);
  text(W - 48, 17, b, F9, AR, WHT);
  d->drawRect(W - 42, 6, 30, 13, WHT);
  d->fillRect(W - 12, 9, 3, 7, WHT);
  d->fillRect(W - 40, 8, (26 * pct) / 100, 9, WHT);
}

// ---- VARIO -------------------------------------------------------------------
static float barPos(float v) {   // -1..1, fine resolution near zero
  if (!isfinite(v)) return 0;
  float a = fabsf(v) / 5.0f;
  if (a > 1) a = 1;
  float p = powf(a, 0.65f);
  return v < 0 ? -p : p;
}

static void varioBar(float v, float avg) {
  const int x = 8, w = 40, top = HDR + 8, bot = H - 8;
  const int cy = (top + bot) / 2, half = (bot - top) / 2;
  d->drawRect(x, top, w, bot - top, BLK);
  for (int k = -4; k <= 4; k++) {
    int y = cy - (int)(half * barPos((float)k));
    d->drawFastHLine(k == 0 ? x : x + w - 9, y, k == 0 ? w : 9, BLK);
  }
  if (isfinite(v)) {
    int y = cy - (int)(half * barPos(v));
    if (v >= 0) d->fillRect(x + 2, y, w - 14, cy - y + 1, BLK);
    else d->fillRect(x + 2, cy, w - 14, y - cy + 1, BLK);
  }
  int ya = cy - (int)(half * barPos(avg));
  d->fillTriangle(x + w + 1, ya, x + w + 13, ya - 7, x + w + 13, ya + 7, WHT);
  d->drawTriangle(x + w + 1, ya, x + w + 13, ya - 7, x + w + 13, ya + 7, BLK);
}

static void courseRose(int cx, int cy, int r, float course) {
  d->drawCircle(cx, cy, r, BLK);
  d->drawCircle(cx, cy, r - 1, BLK);
  d->fillTriangle(cx - 5, cy - r - 1, cx + 5, cy - r - 1, cx, cy - r + 8, BLK);   // travel direction
  if (!isfinite(course)) { text(cx, cy + 6, "--", F12B, AC); return; }
  float th = -course * PI_F / 180.0f;
  int nx = cx + (r - 12) * sinf(th), ny = cy - (r - 12) * cosf(th);
  d->fillCircle(nx, ny, 9, BLK);
  text(nx, ny + 5, "N", F9, AC, WHT);
  char b[8];
  text(cx, cy + 6, fnum(b, sizeof b, course, "%.0f"), F12B, AC);
}

static void fmtTime(char* b, size_t n, uint32_t s) {
  snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

static void varioStatus() {
  VarioState st = varioState();
  const char* title = st == VS_CONNECTED ? "CONNECTED" : st == VS_CONNECTING ? "CONNECTING"
                      : st == VS_SEARCHING ? "SEARCHING" : "NO VARIO";
  text(200, 80, title, F24B, AC);
  char b[48];
  if (varioHasSaved()) snprintf(b, sizeof b, "%s", varioDeviceName());
  else snprintf(b, sizeof b, "filter: %s", varioFilter());
  text(200, 112, b, F12B, AC);
  if (st == VS_CONNECTED) {
    snprintf(b, sizeof b, "waiting for data...  (%lu ok, %lu bad)", (unsigned long)varioLines(), (unsigned long)varioBadLines());
    text(200, 160, b, F9, AC);
  }
  text(200, 228, "hold: choose device", F9, AC);
}

static void varioHeaderInfo() {   // tracer battery next to the title
  const VarioData& v = varioData();
  char b[16];
  if (v.battPct >= 0) snprintf(b, sizeof b, "XC %d%%", v.battPct);
  else snprintf(b, sizeof b, "XC --");
  text(150, 17, b, F9, AL, WHT);
  if (varioSimOn()) text(250, 17, "SIM", F9, AL, WHT);
}

static void liveCommon(const VarioData& v, bool compact) {
  float avg = varioAvg();
  varioBar(v.vario, avg);
  char b[24], t[24];
  text(66, 42, "AVG m/s", F9);
  text(66, 108, fnum(b, sizeof b, avg, "%+.1f"), F24B, AL, BLK, 2);
  snprintf(t, sizeof t, "now %s", fnum(b, sizeof b, v.vario, "%+.1f"));
  text(66, 134, t, F12B);
  text(66, 160, "ALT m", F9);
  text(66, 198, fnum(b, sizeof b, v.alt, "%.0f"), F24B);
  courseRose(332, 66, 38, v.course);
  text(332, 138, fnum(b, sizeof b, v.speedKmh, "%.0f"), F18B, AC);
  text(332, 154, "km/h", F9, AC);
  if (!compact) {
    snprintf(t, sizeof t, "GR %s", fnum(b, sizeof b, varioGlide(), "%.1f"));
    text(332, 182, t, F12B, AC);
    fmtTime(b, sizeof b, varioFlightSec());
    text(66, 232, b, F12B);
    int m = varioLocalMinutes();
    snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60);
    text(396, 232, b, F12B, AR);
  }
}

static void varioThermal(const VarioData& v) {
  float avg = varioAvg();
  varioBar(v.vario, avg);
  char b[24], t[24];
  fnum(b, sizeof b, avg, "%+.1f");
  text(66, 62, b, F24B);
  text(66 + textW(b, F24B) + 6, 62, "m/s", F9);
  // thermal assistant: last 60 s as dots around the current position, north up
  const int cx = 170, cy = 148, R = 74;
  const float pxPerM = 1.4f;
  d->drawCircle(cx, cy, R, BLK);
  int n = varioHistCount();
  if (n < 2 || !varioHist(n - 1).fix) {
    text(cx, cy + 6, "no GPS", F12B, AC);
  } else {
    const VarioSecond& now = varioHist(n - 1);
    float coslat = cosf((float)now.lat * PI_F / 180.0f);
    float sx = 0, sy = 0, sw = 0;
    int cnt = 0;
    for (int i = max(0, n - 60); i < n; i++) {
      const VarioSecond& h = varioHist(i);
      if (!h.fix) continue;
      float dx = (float)(h.lon - now.lon) * 111320.0f * coslat;
      float dy = (float)(h.lat - now.lat) * 111320.0f;
      int px = cx + (int)(dx * pxPerM), py = cy - (int)(dy * pxPerM);
      if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > (R - 4) * (R - 4)) continue;
      float vv = isfinite(h.vario) ? h.vario : 0;
      if (vv >= 0) d->fillCircle(px, py, min(2 + (int)(vv * 1.5f), 7), BLK);
      else d->drawCircle(px, py, min(2 + (int)(-vv), 5), BLK);
      if (vv > 0.5f) { sx += dx * vv; sy += dy * vv; sw += vv; cnt++; }
    }
    if (cnt >= 3) {   // lift-weighted centre of the thermal, 40 m circle
      int tx = cx + (int)(sx / sw * pxPerM), ty = cy - (int)(sy / sw * pxPerM);
      d->drawCircle(tx, ty, (int)(20 * pxPerM), BLK);
      d->drawCircle(tx, ty, (int)(20 * pxPerM) + 1, BLK);
    }
    float th = (isfinite(now.course) ? now.course : 0) * PI_F / 180.0f;   // own heading
    d->fillTriangle(cx + 9 * sinf(th), cy - 9 * cosf(th),
                    cx + 6 * sinf(th + 2.5f), cy - 6 * cosf(th + 2.5f),
                    cx + 6 * sinf(th - 2.5f), cy - 6 * cosf(th - 2.5f), BLK);
  }
  text(cx, cy - R - 3, "N", F9, AC);
  // altitude, speed
  snprintf(t, sizeof t, "%s m", fnum(b, sizeof b, v.alt, "%.0f"));
  text(396, 62, t, F18B, AR);
  snprintf(t, sizeof t, "%s km/h", fnum(b, sizeof b, v.speedKmh, "%.0f"));
  text(396, 92, t, F12B, AR);
  // altitude trace, last 30 s
  const int gx = 268, gy = 130, gw = 128, gh = 100;
  d->drawRect(gx, gy, gw, gh, BLK);
  text(gx, gy - 4, "ALT 30s", F9);
  int m = min(n, 30);
  if (m >= 2) {
    float lo = 1e9f, hi = -1e9f;
    for (int i = n - m; i < n; i++) {
      float a = varioHist(i).alt;
      if (!isfinite(a)) continue;
      lo = min(lo, a); hi = max(hi, a);
    }
    if (hi < lo) return;   // no finite altitude in the window
    if (hi - lo < 10) { float mid = (hi + lo) / 2; lo = mid - 5; hi = mid + 5; }
    int px = -1, py = 0;
    for (int i = n - m; i < n; i++) {
      float a = varioHist(i).alt;
      if (!isfinite(a)) continue;
      int x = gx + 2 + (int)((gw - 4) * (float)(i - (n - m)) / 29.0f);
      int y = gy + gh - 3 - (int)((gh - 6) * (a - lo) / (hi - lo));
      if (px >= 0) d->drawLine(px, py, x, y, BLK);
      px = x; py = y;
    }
  }
}

static void pageVario(Page p) {
  if (!varioFresh()) { varioStatus(); return; }
  const VarioData& v = varioData();
  if (p == PAGE_VARIO_THERMAL) varioThermal(v);
  else liveCommon(v, p == PAGE_VARIO_SIMPLE);
}

// ---- public ----------------------------------------------------------------
void uiBegin(Adafruit_SharpMem* disp) {
  d = disp;
  bufLen = (size_t)W * H / 8;
  snap = (uint8_t*)malloc(bufLen);
  dirty = true;
}

void uiInvalidate() { dirty = true; }

uint32_t uiIntervalMs(Page) { return 500; }

static void present() {
  uint8_t* buf = d->getBuffer();
  if (!dirty && snap && memcmp(buf, snap, bufLen) == 0) return;
  d->refresh();
  if (snap) memcpy(snap, buf, bufLen);
  dirty = false;
}

static void powerBanner(uint32_t heldMs) {
  if (heldMs < 1500) return;
  d->fillRect(40, 92, 320, 64, WHT);
  d->drawRect(40, 92, 320, 64, BLK);
  d->drawRect(41, 93, 318, 62, BLK);
  text(200, 120, "Keep holding to power off", F9, AC);
  hbar(60, 130, 280, 16, (heldMs - 1500) / 1500.0f);
}

void uiDraw(Page p, uint32_t heldMs) {
  d->fillRect(0, 0, W, H, WHT);
  header(NAMES[p], p);
  varioHeaderInfo();
  pageVario(p);
  powerBanner(heldMs);
  present();
}

void uiDrawList(int sel, uint32_t heldMs) {
  d->fillRect(0, 0, W, H, WHT);
  header("SELECT VARIO", -1);
  char b[40];
  snprintf(b, sizeof b, "filter: %s", varioFilter());
  text(8, HDR + 20, b, F9);
  int n = varioListCount();
  const int rowH = 30, y0 = HDR + 30;
  for (int i = 0; i <= n && i < 6; i++) {
    int y = y0 + i * rowH;
    bool on = (i == sel);
    if (on) d->fillRect(0, y, W, rowH - 2, BLK);
    uint16_t col = on ? WHT : BLK;
    if (i == 0) {
      text(12, y + 21, "< back", F12B, AL, col);
    } else {
      const ScanItem& it = varioListItem(i - 1);
      text(12, y + 21, it.name, F12B, AL, col);
      snprintf(b, sizeof b, "%d dBm", it.rssi);
      text(W - 12, y + 21, b, F9, AR, col);
    }
  }
  if (n == 0) text(200, y0 + 2 * rowH + 20, "scanning...", F12B, AC);
  text(200, 232, "press: next    hold: select", F9, AC);
  powerBanner(heldMs);
  present();
}

void uiMessage(const char* l1, const char* l2) {
  d->fillRect(0, 0, W, H, WHT);
  text(200, l2 ? 112 : 128, l1, F18B, AC);
  if (l2) text(200, 148, l2, F12B, AC);
  dirty = true;
  present();
}

void uiBlank() {
  d->fillRect(0, 0, W, H, WHT);
  dirty = true;
  present();
}
