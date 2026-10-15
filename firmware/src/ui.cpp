#include "ui.h"
#include <math.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "vario.h"
#include "battery.h"

static int W = 400, H = 240;   // logical size, follows the display rotation
static const int HDR = 24;
static const uint16_t BLK = 0, WHT = 1;
static const float PI_F = 3.14159265f;

#define F9   (&FreeSans9pt7b)
#define F9B  (&FreeSansBold9pt7b)
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

static void header(const char* title = nullptr) {
  d->fillRect(0, 0, W, HDR, BLK);
  char b[16];
  if (title) {
    text(6, 17, title, F9, AL, WHT);
  } else if (varioFresh()) {
    int m = varioLocalMinutes();
    snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60);
  } else {
    snprintf(b, sizeof b, "--:--");
  }
  if (!title) {
    text(6, 19, b, F12B, AL, WHT);
    const VarioData& v = varioData();
    if (varioFresh() && v.battPct >= 0) snprintf(b, sizeof b, "XC %d%%", v.battPct);
    else snprintf(b, sizeof b, "XC --");
    text(76, 17, b, F9, AL, WHT);
    if (varioSimOn()) text(128, 17, "SIM", F9, AL, WHT);
  }
  int pct = batteryPct();
  snprintf(b, sizeof b, "%d%%", pct);
  text(W - 44, 17, b, F9, AR, WHT);
  d->drawRect(W - 40, 6, 30, 13, WHT);
  d->fillRect(W - 10, 9, 3, 7, WHT);
  d->fillRect(W - 38, 8, (26 * pct) / 100, 9, WHT);
}

// ---- VARIO -------------------------------------------------------------------
static float barPos(float v) {   // -1..1, fine resolution near zero
  if (!isfinite(v)) return 0;
  float a = fabsf(v) / 5.0f;
  if (a > 1) a = 1;
  float p = powf(a, 0.65f);
  return v < 0 ? -p : p;
}

// Thin vertical vario along the left edge: instant value filled from the centre, tick marks
// and a small block for the averaged value on its right side.
static void thinVario(int x, int w, float v, float avg) {
  const int top = HDR + 4, bot = H - 4;
  const int cy = (top + bot) / 2, half = (bot - top) / 2;
  d->drawRect(x, top, w, bot - top, BLK);
  for (int k = -4; k <= 4; k++) {
    int y = cy - (int)(half * barPos((float)k));
    d->drawFastHLine(x + w, y, k == 0 ? 6 : 3, BLK);
  }
  d->drawFastHLine(x, cy, w, BLK);
  if (isfinite(v)) {
    int y = cy - (int)(half * barPos(v));
    if (v >= 0) d->fillRect(x + 2, y, w - 4, cy - y + 1, BLK);
    else d->fillRect(x + 2, cy, w - 4, y - cy + 1, BLK);
  }
  int ya = cy - (int)(half * barPos(avg));
  d->fillRect(x + w + 1, ya - 2, 7, 5, BLK);
}

static const char* cardinal16(float deg) {
  static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return n[((int)((deg + 11.25f) / 22.5f)) % 16];
}

// Heading-up compass in the Garmin style: the dial rotates so the current track is under the
// fixed pointer at the top; big N/E/S/W and smaller NE/SE/SW/NW sit on the ring, ticks every 10
// degrees (longer every 30), the track in degrees is the big number in the centre.
static void compassDial(int cx, int cy, int R, float course) {
  d->drawCircle(cx, cy, R, BLK);
  d->drawCircle(cx, cy, R - 1, BLK);
  d->fillTriangle(cx - 8, cy - R - 4, cx + 8, cy - R - 4, cx, cy - R + 11, BLK);   // direction of travel
  if (!isfinite(course)) { text(cx, cy + 8, "--", F18B, AC); return; }
  const int rl = R - 24;   // label radius
  for (int a = 0; a < 360; a += 10) {
    if (a % 45 == 0) continue;   // letters sit there
    float th = (a - course) * PI_F / 180.0f;
    int len = (a % 30 == 0) ? 9 : 5;
    d->drawLine(cx + (R - 2) * sinf(th), cy - (R - 2) * cosf(th),
                cx + (R - 2 - len) * sinf(th), cy - (R - 2 - len) * cosf(th), BLK);
  }
  static const char* L[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  for (int i = 0; i < 8; i++) {
    float th = (i * 45 - course) * PI_F / 180.0f;
    int lx = cx + rl * sinf(th), ly = cy - rl * cosf(th);
    if (i == 0) {
      d->fillRect(lx - 12, ly - 12, 25, 25, BLK);
      text(lx, ly + 6, "N", F12B, AC, WHT);
    } else if (i % 2 == 0) {
      text(lx, ly + 6, L[i], F12B, AC);
    } else {
      text(lx, ly + 5, L[i], F9, AC);
    }
  }
  char b[8];
  text(cx, cy + 8, fnum(b, sizeof b, course, "%.0f"), F18B, AC);
}

// The wind arrow lives in the compass corner the wind comes from and points the way it blows,
// relative to the heading: top-right corner + arrow down-left = wind from front-right.
static void windCorner(int cx, int cy, int R, float course, const VarioWind& w) {
  if (!w.valid || !isfinite(course)) return;
  float from = fmodf(w.toDeg + 180.0f, 360.0f);
  float psi = fmodf(from - course + 360.0f, 360.0f);      // 0 = from straight ahead
  int right = psi < 180.0f ? 1 : -1;
  int front = (psi < 90.0f || psi >= 270.0f) ? -1 : 1;     // -1 = top
  int off = (int)(0.86f * (R + 10));
  int ax = cx + right * off, ay = cy + front * off;
  float th = (psi + 180.0f) * PI_F / 180.0f;               // screen direction the wind blows
  float ux = sinf(th), uy = -cosf(th), px = -uy, py = ux;
  const int L = 17;                                        // half length of the arrow
  int tx = ax - L * ux, ty = ay - L * uy;                  // tail
  int hx = ax + 3 * ux, hy = ay + 3 * uy;                  // base of the head
  int ex = ax + L * ux, ey = ay + L * uy;                  // tip
  for (int k = -1; k <= 1; k++) d->drawLine(tx + k * px, ty + k * py, hx + k * px, hy + k * py, BLK);
  d->fillTriangle(ex, ey, hx + 9 * px, hy + 9 * py, hx - 9 * px, hy - 9 * py, BLK);
}

static void varioStatus() {
  VarioState st = varioState();
  const char* title = st == VS_CONNECTED ? "CONNECTED" : st == VS_CONNECTING ? "CONNECTING"
                      : st == VS_SEARCHING ? "SEARCHING" : "NO VARIO";
  int cx = W / 2;
  text(cx, 130, title, F18B, AC);
  char b[48];
  if (varioHasSaved()) snprintf(b, sizeof b, "%.18s", varioDeviceName());
  else snprintf(b, sizeof b, "filter: %.12s", varioFilter());
  text(cx, 162, b, F12B, AC);
  if (st == VS_CONNECTED) {
    text(cx, 200, "waiting for data...", F9, AC);
    snprintf(b, sizeof b, "%lu ok, %lu bad", (unsigned long)varioLines(), (unsigned long)varioBadLines());
    text(cx, 220, b, F9, AC);
  }
  text(cx, H - 12, "hold: choose device", F9, AC);
}

// Portrait layout, 240 x 400. Priorities: GPS speed + glide, altitude + vertical speed, compass + wind.
// Cells have a short inverted title band like the top toolbar.
static void cell(int x, int y, int w, const char* title, const char* value) {
  d->fillRect(x, y, w, 17, BLK);
  text(x + 5, y + 14, title, F9, AL, WHT);
  text(x + 4, y + 58, value, F24B);
}

static void liveScreen(const VarioData& v) {
  char b[24];
  const int barX = 3, barW = 9;              // thin vario on the left edge
  const int cx0 = 22, cellW = 107;           // two cells to the right of it
  const int row1 = HDR + 2, row2 = HDR + 66, gridEnd = HDR + 130;

  // no GPS fix: speed and track read 0 (the title says NO FIX) instead of dashes
  float spd = isfinite(v.speedKmh) ? v.speedKmh : 0;
  float crs = isfinite(v.course) ? v.course : 0;
  thinVario(barX, barW, v.vario, varioAvg());
  cell(cx0, row1, cellW, v.fix ? "GPS km/h" : "NO FIX", fnum(b, sizeof b, spd, "%.0f"));
  cell(cx0 + cellW + 4, row1, cellW, "GLIDE", fnum(b, sizeof b, varioGlide(), "%.1f"));
  cell(cx0, row2, cellW, "ALT m", fnum(b, sizeof b, v.alt, "%.0f"));
  cell(cx0 + cellW + 4, row2, cellW, "VS m/s", fnum(b, sizeof b, v.vario, "%+.1f"));

  // compass with the wind arrow in a corner
  VarioWind w = varioWind();
  const int R = 92, cx = cx0 + (W - cx0) / 2, cy = gridEnd + 8 + R + 6;
  compassDial(cx, cy, R, crs);
  windCorner(cx, cy, R, crs, w);

  // wind as one line: "15 km/h NNW"
  const int ly = H - 6;
  if (w.valid) {
    float from = fmodf(w.toDeg + 180.0f, 360.0f);
    fnum(b, sizeof b, w.speedKmh, "%.0f");
    int x = cx0;
    text(x, ly, b, F18B);
    x += textW(b, F18B) + 5;
    text(x, ly, "km/h", F12B);
    x += textW("km/h", F12B) + 10;
    text(x, ly, cardinal16(from), F18B);
    if (w.ageS > 300) text(W - 6, ly, "old", F9, AR);
  } else {
    text(cx0, ly, "0", F18B);
    text(cx0 + textW("0", F18B) + 5, ly, "km/h", F12B);
  }
}

// ---- public ----------------------------------------------------------------
static void syncSize() { W = d->width(); H = d->height(); }

void uiBegin(Adafruit_SharpMem* disp) {
  d = disp;
  syncSize();
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
  int bw = W - 24, bx = 12, by = H / 2 - 36;
  d->fillRect(bx, by, bw, 72, WHT);
  d->drawRect(bx, by, bw, 72, BLK);
  d->drawRect(bx + 1, by + 1, bw - 2, 70, BLK);
  text(W / 2, by + 26, "Keep holding to power off", F9, AC);
  hbar(bx + 14, by + 38, bw - 28, 18, (heldMs - 1500) / 1500.0f);
}

void uiDraw(uint32_t heldMs) {
  syncSize();
  d->fillRect(0, 0, W, H, WHT);
  header();
  if (varioFresh()) liveScreen(varioData());
  else varioStatus();
  powerBanner(heldMs);
  present();
}

void uiDrawList(int sel, uint32_t heldMs) {
  syncSize();
  d->fillRect(0, 0, W, H, WHT);
  header("SELECT VARIO");
  char b[40];
  snprintf(b, sizeof b, "filter: %.16s", varioFilter());
  text(6, HDR + 20, b, F9);
  int n = varioListCount();
  const int rowH = 42, y0 = HDR + 30;
  int rows = n + 1;
  int vis = (H - y0 - 30) / rowH;
  int first = sel >= vis ? sel - vis + 1 : 0;   // scroll so the selection stays visible
  for (int k = 0; k < vis && first + k < rows; k++) {
    int i = first + k, y = y0 + k * rowH;
    bool on = (i == sel);
    if (on) d->fillRect(0, y, W, rowH - 2, BLK);
    uint16_t col = on ? WHT : BLK;
    if (i == 0) {
      text(10, y + 27, "< back", F12B, AL, col);
    } else {
      const ScanItem& it = varioListItem(i - 1);
      char nm[20];
      snprintf(nm, sizeof nm, "%.17s", it.name);
      text(10, y + 18, nm, F12B, AL, col);
      snprintf(b, sizeof b, "%d dBm", it.rssi);
      text(10, y + 36, b, F9, AL, col);
    }
  }
  if (n == 0) text(W / 2, y0 + 2 * rowH, "scanning...", F12B, AC);
  text(W / 2, H - 12, "press: next   hold: select", F9, AC);
  powerBanner(heldMs);
  present();
}

void uiMessage(const char* l1, const char* l2) {
  syncSize();
  d->fillRect(0, 0, W, H, WHT);
  text(W / 2, H / 2 - (l2 ? 8 : 0), l1, F18B, AC);
  if (l2) text(W / 2, H / 2 + 28, l2, F12B, AC);
  dirty = true;
  present();
}

void uiBlank() {
  syncSize();
  d->fillRect(0, 0, W, H, WHT);
  dirty = true;
  present();
}
