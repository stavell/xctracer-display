#include "ui.h"
#include <math.h>
#include <Fonts/FreeSans9pt7b.h>
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

// Thin vertical vario along the right edge: instant value filled from the centre, tick marks
// on the left, small block on the left for the averaged value.
static void thinVario(int x, int w, float v, float avg) {
  const int top = HDR + 4, bot = H - 4;
  const int cy = (top + bot) / 2, half = (bot - top) / 2;
  d->drawRect(x, top, w, bot - top, BLK);
  for (int k = -4; k <= 4; k++) {
    int y = cy - (int)(half * barPos((float)k));
    d->drawFastHLine(x - (k == 0 ? 6 : 3), y, k == 0 ? 6 : 3, BLK);
  }
  d->drawFastHLine(x, cy, w, BLK);
  if (isfinite(v)) {
    int y = cy - (int)(half * barPos(v));
    if (v >= 0) d->fillRect(x + 2, y, w - 4, cy - y + 1, BLK);
    else d->fillRect(x + 2, cy, w - 4, y - cy + 1, BLK);
  }
  int ya = cy - (int)(half * barPos(avg));
  d->fillRect(x - 8, ya - 2, 7, 5, BLK);
}

// Heading-up compass: the pointer at the top is the direction of travel, N rides on the ring,
// the thick arrow shows where the wind blows toward (up = tailwind, down = headwind).
static void compassWind(int cx, int cy, int r, float course, const VarioWind& wnd) {
  d->drawCircle(cx, cy, r, BLK);
  d->drawCircle(cx, cy, r - 1, BLK);
  d->fillTriangle(cx - 7, cy - r - 3, cx + 7, cy - r - 3, cx, cy - r + 10, BLK);   // direction of travel
  if (isfinite(course)) {
    for (int a = 0; a < 360; a += 30) {   // ticks rotate with the ground track
      float th = (a - course) * PI_F / 180.0f;
      d->drawLine(cx + (r - 2) * sinf(th), cy - (r - 2) * cosf(th),
                  cx + (r - 9) * sinf(th), cy - (r - 9) * cosf(th), BLK);
    }
    float th = -course * PI_F / 180.0f;
    int nx = cx + (r - 20) * sinf(th), ny = cy - (r - 20) * cosf(th);
    d->fillCircle(nx, ny, 11, BLK);
    text(nx, ny + 5, "N", F9, AC, WHT);
  }
  if (wnd.valid && isfinite(course) && isfinite(wnd.toDeg)) {
    float th = (wnd.toDeg - course) * PI_F / 180.0f;
    float ux = sinf(th), uy = -cosf(th);          // screen direction the wind blows
    float px = -uy, py = ux;                      // perpendicular
    int tx = cx - 0.30f * r * ux, ty = cy - 0.30f * r * uy;   // tail
    int hx = cx + 0.42f * r * ux, hy = cy + 0.42f * r * uy;   // base of the head
    int ex = cx + 0.72f * r * ux, ey = cy + 0.72f * r * uy;   // tip
    int hw = 12;
    for (int k = -2; k <= 2; k++) d->drawLine(tx + k * px, ty + k * py, hx + k * px, hy + k * py, BLK);
    d->fillTriangle(ex, ey, hx + hw * px, hy + hw * py, hx - hw * px, hy - hw * py, BLK);
  }
  if (!isfinite(course)) text(cx, cy + 6, "--", F12B, AC);
}

static const char* cardinal16(float deg) {
  static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return n[((int)((deg + 11.25f) / 22.5f)) % 16];
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
static void cell(int x, int y, const char* label, const char* value) {
  text(x, y + 15, label, F9);
  text(x, y + 57, value, F24B);
}

static void liveScreen(const VarioData& v) {
  char b[24], t[24];
  const int stripX = W - 10;                 // thin vario strip, 8 px wide
  const int rightCol = 122;
  const int row1 = HDR, row2 = HDR + 76, gridEnd = HDR + 152;

  cell(6, row1, "GPS km/h", fnum(b, sizeof b, v.speedKmh, "%.0f"));
  cell(rightCol, row1, "GLIDE", fnum(b, sizeof b, varioGlide(), "%.1f"));
  cell(6, row2, "ALTITUDE m", fnum(b, sizeof b, v.alt, "%.0f"));
  cell(rightCol, row2, "VERT m/s", fnum(b, sizeof b, v.vario, "%+.1f"));
  d->drawFastHLine(0, row2, stripX - 10, BLK);
  d->drawFastHLine(0, gridEnd, stripX - 10, BLK);
  d->drawFastVLine(rightCol - 6, HDR + 2, gridEnd - HDR - 4, BLK);
  thinVario(stripX, 8, v.vario, varioAvg());

  // compass + wind
  VarioWind w = varioWind();
  const int r = 74, cx = (stripX - 10) / 2, cy = gridEnd + 10 + r;
  compassWind(cx, cy, r, v.course, w);

  // track in the free corner outside the ring
  text(6, gridEnd + 24, "TRK", F9);
  text(6, gridEnd + 44, fnum(b, sizeof b, v.course, "%.0f"), F12B);

  const int sy = cy + r + 14;                // wind strip: label row, then value / direction / head-tail
  text(6, sy, w.valid ? "WIND km/h" : "WIND: circle to measure", F9);
  text(6, H - 4, w.valid ? fnum(b, sizeof b, w.speedKmh, "%.0f") : "--", F24B);
  if (w.valid) {
    text(stripX - 8, sy, w.ageS > 300 ? "from (old)" : "from", F9, AR);
    float from = fmodf(w.toDeg + 180.0f, 360.0f);
    snprintf(t, sizeof t, "%s %s", cardinal16(from), fnum(b, sizeof b, from, "%.0f"));
    text(stripX - 8, H - 26, t, F12B, AR);
    if (isfinite(v.course)) {
      float along = w.speedKmh * cosf((w.toDeg - v.course) * PI_F / 180.0f);   // + = tailwind
      if (fabsf(along) < 1.0f) snprintf(t, sizeof t, "cross");
      else snprintf(t, sizeof t, "%s %.0f", along > 0 ? "tail" : "head", fabsf(along));
      text(stripX - 8, H - 4, t, F12B, AR);
    }
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
