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

static void header() {
  d->fillRect(0, 0, W, HDR, BLK);
  char b[16];
  if (varioFresh()) {
    int m = varioLocalMinutes();
    snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60);
  } else {
    snprintf(b, sizeof b, "--:--");
  }
  text(8, 19, b, F12B, AL, WHT);
  const VarioData& v = varioData();
  if (varioFresh() && v.battPct >= 0) snprintf(b, sizeof b, "XC %d%%", v.battPct);
  else snprintf(b, sizeof b, "XC --");
  text(150, 17, b, F9, AL, WHT);
  if (varioSimOn()) text(230, 17, "SIM", F9, AL, WHT);
  int pct = batteryPct();
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

// thin vertical vario: instant value filled from the centre, tick marks on the left,
// hollow triangle on the right for the averaged value
static void thinVario(int x, int w, float v, float avg) {
  const int top = HDR + 6, bot = H - 6;
  const int cy = (top + bot) / 2, half = (bot - top) / 2;
  d->drawRect(x, top, w, bot - top, BLK);
  for (int k = -4; k <= 4; k++) {
    int y = cy - (int)(half * barPos((float)k));
    d->drawFastHLine(x - (k == 0 ? 6 : 4), y, k == 0 ? 6 : 4, BLK);
  }
  d->drawFastHLine(x, cy, w, BLK);
  if (isfinite(v)) {
    int y = cy - (int)(half * barPos(v));
    if (v >= 0) d->fillRect(x + 2, y, w - 4, cy - y + 1, BLK);
    else d->fillRect(x + 2, cy, w - 4, y - cy + 1, BLK);
  }
  int ya = cy - (int)(half * barPos(avg));
  d->fillTriangle(x + w + 1, ya, x + w + 10, ya - 6, x + w + 10, ya + 6, WHT);
  d->drawTriangle(x + w + 1, ya, x + w + 10, ya - 6, x + w + 10, ya + 6, BLK);
}

// Heading-up compass: the pointer at the top is the direction of travel, N rides on the ring,
// the thick arrow shows where the wind blows toward.
static void compassWind(int cx, int cy, int r, float course, const VarioWind& wnd) {
  d->drawCircle(cx, cy, r, BLK);
  d->drawCircle(cx, cy, r - 1, BLK);
  d->fillTriangle(cx - 6, cy - r - 2, cx + 6, cy - r - 2, cx, cy - r + 9, BLK);   // direction of travel
  if (isfinite(course)) {
    for (int a = 0; a < 360; a += 30) {   // ticks rotate with the ground track
      float th = (a - course) * PI_F / 180.0f;
      d->drawLine(cx + (r - 2) * sinf(th), cy - (r - 2) * cosf(th),
                  cx + (r - 8) * sinf(th), cy - (r - 8) * cosf(th), BLK);
    }
    float th = -course * PI_F / 180.0f;
    int nx = cx + (r - 17) * sinf(th), ny = cy - (r - 17) * cosf(th);
    d->fillCircle(nx, ny, 10, BLK);
    text(nx, ny + 5, "N", F9, AC, WHT);
  }
  if (wnd.valid && isfinite(course) && isfinite(wnd.toDeg)) {
    float th = (wnd.toDeg - course) * PI_F / 180.0f;
    float ux = sinf(th), uy = -cosf(th);       // screen direction of the wind
    float px = -uy, py = ux;                   // perpendicular
    int tx = cx - 20 * ux, ty = cy - 20 * uy;  // tail
    int hx = cx + 24 * ux, hy = cy + 24 * uy;  // base of the head
    int ex = cx + 40 * ux, ey = cy + 40 * uy;  // tip
    for (int k = -1; k <= 1; k++) d->drawLine(tx + k * px, ty + k * py, hx + k * px, hy + k * py, BLK);
    d->fillTriangle(ex, ey, hx + 9 * px, hy + 9 * py, hx - 9 * px, hy - 9 * py, BLK);
  }
  if (isfinite(course)) {   // numeric track, boxed so the arrow does not hide it
    char b[8];
    fnum(b, sizeof b, course, "%.0f");
    int tw = textW(b, F12B);
    d->fillRect(cx - tw / 2 - 3, cy + r - 40, tw + 6, 22, WHT);
    text(cx, cy + r - 22, b, F12B, AC);
  } else {
    text(cx, cy + 6, "--", F12B, AC);
  }
}

static const char* cardinal16(float deg) {
  static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return n[((int)((deg + 11.25f) / 22.5f)) % 16];
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

static void liveScreen(const VarioData& v) {
  char b[24], t[24];
  float avg = varioAvg();
  // left column: altitude, vertical speed, ground speed + glide ratio
  text(8, 40, "ALTITUDE m", F9);
  text(8, 98, fnum(b, sizeof b, v.alt, "%.0f"), F24B, AL, BLK, 2);
  text(8, 120, "VERTICAL SPEED m/s", F9);
  text(8, 174, fnum(b, sizeof b, v.vario, "%+.1f"), F24B, AL, BLK, 2);
  text(8, 196, "GPS km/h", F9);
  text(8, 230, fnum(b, sizeof b, v.speedKmh, "%.0f"), F24B);
  text(112, 196, "GLIDE", F9);
  text(112, 230, fnum(b, sizeof b, varioGlide(), "%.1f"), F24B);
  // thin vario between the columns
  thinVario(214, 16, v.vario, avg);
  // right column: compass with wind arrow, wind speed and direction
  VarioWind w = varioWind();
  compassWind(322, 98, 60, v.course, w);
  text(250, 184, w.valid ? "WIND km/h" : "WIND  (circle to measure)", F9);
  text(250, 228, w.valid ? fnum(b, sizeof b, w.speedKmh, "%.0f") : "--", F24B);
  if (w.valid) {
    float from = fmodf(w.toDeg + 180.0f, 360.0f);
    text(396, 204, "from", F9, AR);
    snprintf(t, sizeof t, "%s %s", cardinal16(from), fnum(b, sizeof b, from, "%.0f"));
    text(396, 226, t, F12B, AR);
    if (w.ageS > 300) {
      snprintf(t, sizeof t, "%lum ago", (unsigned long)(w.ageS / 60));
      text(396, 184, t, F9, AR);
    }
  }
}

// ---- public ----------------------------------------------------------------
void uiBegin(Adafruit_SharpMem* disp) {
  d = disp;
  bufLen = (size_t)W * H / 8;
  snap = (uint8_t*)malloc(bufLen);
  dirty = true;
}

void uiInvalidate() { dirty = true; }

uint32_t uiIntervalMs() { return 500; }

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

void uiDraw(uint32_t heldMs) {
  d->fillRect(0, 0, W, H, WHT);
  header();
  if (varioFresh()) liveScreen(varioData());
  else varioStatus();
  powerBanner(heldMs);
  present();
}

void uiDrawList(int sel, uint32_t heldMs) {
  d->fillRect(0, 0, W, H, WHT);
  header();
  text(200, 17, "SELECT VARIO", F9, AC, WHT);
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
