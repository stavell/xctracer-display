#include "ui.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

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

static void text(int x, int y, const char* s, const GFXfont* f, Align a = AL, uint16_t col = BLK) {
  d->setFont(f);
  d->setTextColor(col);
  int16_t x1, y1;
  uint16_t w, h;
  d->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int cx = x - x1;
  if (a == AC) cx = x - (int)w / 2 - x1;
  else if (a == AR) cx = x - (int)w - x1;
  d->setCursor(cx, y);
  d->print(s);
}

static int textW(const char* s, const GFXfont* f) {
  d->setFont(f);
  int16_t x1, y1;
  uint16_t w, h;
  d->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static void degree(int x, int y, uint16_t col = BLK) {
  d->drawCircle(x, y, 3, col);
  d->drawCircle(x, y, 2, col);
}

static void hbar(int x, int y, int w, int h, float frac) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d->drawRect(x, y, w, h, BLK);
  d->fillRect(x + 2, y + 2, (int)((w - 4) * frac), h - 4, BLK);
}

static const char* NAMES[PAGE_COUNT] = {"ENVIRONMENT", "COMPASS", "MOTION", "LIGHT & GESTURE", "SOUND"};

static void header(Page p) {
  d->fillRect(0, 0, W, HDR, BLK);
  text(8, 17, NAMES[p], F9, AL, WHT);
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 200 - (PAGE_COUNT - 1) * 8 + i * 16 + 40;
    if (i == (int)p) d->fillCircle(x, 12, 4, WHT);
    else d->drawCircle(x, 12, 4, WHT);
  }
  const Readings& r = readings();
  char b[8];
  snprintf(b, sizeof b, "%d%%", r.battPct);
  text(W - 48, 17, b, F9, AR, WHT);
  d->drawRect(W - 42, 6, 30, 13, WHT);
  d->fillRect(W - 12, 9, 3, 7, WHT);
  d->fillRect(W - 40, 8, (26 * r.battPct) / 100, 9, WHT);
}

// ---- ENVIRONMENT -----------------------------------------------------------
static void tile(int col, int row, const char* label, const char* value, const char* unit, bool deg = false) {
  int x = col * 200, y = HDR + row * 72;
  text(x + 12, y + 18, label, F9);
  text(x + 12, y + 58, value, F24B);
  int vw = textW(value, F24B);
  int ux = x + 12 + vw + 6;
  if (deg) { degree(ux + 2, y + 58 - 22); ux += 8; }
  text(ux, y + 58, unit, F12B);
}

static void pageEnv() {
  const Readings& r = readings();
  char v[16];
  auto f = [&](float val, const char* fmt) {
    if (isnan(val)) snprintf(v, sizeof v, "--");
    else snprintf(v, sizeof v, fmt, val);
    return v;
  };
  tile(0, 0, "TEMPERATURE", f(r.tempC, "%.1f"), "C", true);
  tile(1, 0, "HUMIDITY", f(r.hum, "%.0f"), "%");
  tile(0, 1, "PRESSURE", f(r.pressHpa, "%.1f"), "hPa");
  tile(1, 1, "ALTITUDE", f(r.altM, "%.0f"), "m");
  if (!isnan(r.lux) && r.lux >= 10000) tile(0, 2, "LIGHT", f(r.lux / 1000.0f, "%.1f"), "klux");
  else tile(0, 2, "LIGHT", f(r.lux, "%.0f"), "lux");
  char bv[8];
  snprintf(bv, sizeof bv, "%.2fV", r.battV);
  char bp[8];
  snprintf(bp, sizeof bp, "%d", r.battPct);
  tile(1, 2, "BATTERY", bp, "%");
  text(W - 10, HDR + 2 * 72 + 58, bv, F9, AR);
  d->drawFastVLine(200, HDR, H - HDR, BLK);
  d->drawFastHLine(0, HDR + 72, W, BLK);
  d->drawFastHLine(0, HDR + 144, W, BLK);
}

// ---- COMPASS ---------------------------------------------------------------
static const char* cardinal(float h) {
  static const char* n[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return n[((int)((h + 22.5f) / 45.0f)) % 8];
}

static void pageCompass() {
  const Readings& r = readings();
  const CalState& cs = calState();
  if (cs.active) {
    text(200, 70, "CALIBRATING", F18B, AC);
    text(200, 100, "Rotate slowly through all directions", F9, AC);
    const char* ax[3] = {"X", "Y", "Z"};
    float sp[3] = {cs.spanX, cs.spanY, cs.spanZ};
    for (int i = 0; i < 3; i++) {
      int y = 124 + i * 28;
      text(60, y + 13, ax[i], F9);
      hbar(90, y, 250, 18, sp[i] / 70.0f);
    }
    text(200, 228, "hold = save    press = cancel", F9, AC);
    return;
  }
  const int cx = 200, cy = 134, rad = 88;
  d->drawCircle(cx, cy, rad, BLK);
  d->drawCircle(cx, cy, rad - 1, BLK);
  float hd = isnan(r.heading) ? 0 : r.heading;
  for (int deg = 0; deg < 360; deg += 10) {
    if (deg % 90 == 0) continue;
    float th = (deg - hd) * PI_F / 180.0f;
    int len = (deg % 30 == 0) ? 12 : 6;
    float s = sinf(th), c = cosf(th);
    d->drawLine(cx + (rad - 2) * s, cy - (rad - 2) * c, cx + (rad - 2 - len) * s, cy - (rad - 2 - len) * c, BLK);
  }
  static const char* L[4] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    float th = (i * 90 - hd) * PI_F / 180.0f;
    int px = cx + (rad - 22) * sinf(th), py = cy - (rad - 22) * cosf(th);
    if (i == 0) d->fillCircle(px, py, 13, BLK);
    text(px, py + 7, L[i], F12B, AC, i == 0 ? WHT : BLK);
  }
  d->fillTriangle(cx - 8, HDR + 2, cx + 8, HDR + 2, cx, HDR + 18, BLK);   // fixed pointer
  d->fillCircle(cx, cy, 3, BLK);
  char b[12];
  snprintf(b, sizeof b, "%.0f", hd);
  text(10, 80, b, F24B);
  degree(10 + textW(b, F24B) + 8, 80 - 24);
  text(10, 112, isnan(r.heading) ? "--" : cardinal(hd), F18B);
  snprintf(b, sizeof b, "%.0f uT", r.fieldUt);
  text(W - 10, 80, b, F12B, AR);
  text(W - 10, 100, "field", F9, AR);
  if (!cs.valid) text(10, 200, "NOT CALIBRATED", F9);
  text(10, 228, "hold: calibrate", F9);
}

// ---- MOTION ----------------------------------------------------------------
static void gbar(int x, int y, int w, float v, float full, const char* label) {
  text(x, y + 12, label, F9);
  int bx = x + 22, bw = w - 22;
  d->drawRect(bx, y, bw, 16, BLK);
  d->drawFastVLine(bx + bw / 2, y, 16, BLK);
  float f = v / full;
  if (f > 1) f = 1;
  if (f < -1) f = -1;
  int half = bw / 2;
  if (f >= 0) d->fillRect(bx + half, y + 2, (int)(half * f), 12, BLK);
  else d->fillRect(bx + half + (int)(half * f), y + 2, (int)(-half * f), 12, BLK);
}

static void pageMotion() {
  const Readings& r = readings();
  const int cx = 108, cy = 136, rad = 84;
  const float g = 9.81f;
  d->drawCircle(cx, cy, rad, BLK);
  d->drawCircle(cx, cy, rad - 1, BLK);
  d->drawCircle(cx, cy, 22, BLK);
  d->drawFastHLine(cx - rad, cy, 2 * rad, BLK);
  d->drawFastVLine(cx, cy - rad, 2 * rad, BLK);
  float bx = r.ax / g * 2.0f * (rad - 14), by = r.ay / g * 2.0f * (rad - 14);
  float m = sqrtf(bx * bx + by * by), lim = rad - 14;
  if (m > lim) { bx *= lim / m; by *= lim / m; }
  d->fillCircle(cx + (int)bx, cy - (int)by, 12, BLK);
  d->fillCircle(cx + (int)bx, cy - (int)by, 4, WHT);

  char b[16];
  snprintf(b, sizeof b, "%+.0f", r.pitch);
  text(220, 56, "PITCH", F9);
  text(396, 56, b, F18B, AR);
  snprintf(b, sizeof b, "%+.0f", r.roll);
  text(220, 92, "ROLL", F9);
  text(396, 92, b, F18B, AR);
  float mag = sqrtf(r.ax * r.ax + r.ay * r.ay + r.az * r.az) / g;
  snprintf(b, sizeof b, "%.2f g", mag);
  text(220, 124, "ACCEL", F9);
  text(396, 124, b, F12B, AR);
  text(220, 152, "GYRO  deg/s", F9);
  gbar(220, 160, 176, r.gx, 250, "X");
  gbar(220, 184, 176, r.gy, 250, "Y");
  gbar(220, 208, 176, r.gz, 250, "Z");
}

// ---- LIGHT & GESTURE -------------------------------------------------------
static void arrow(int cx, int cy, int dir) {   // 1 down 2 up 3 left 4 right
  const int L = 36, T = 22;
  switch (dir) {
    case 1: d->fillRect(cx - 10, cy - L, 20, L, BLK); d->fillTriangle(cx - T, cy, cx + T, cy, cx, cy + T + 8, BLK); break;
    case 2: d->fillRect(cx - 10, cy, 20, L, BLK); d->fillTriangle(cx - T, cy, cx + T, cy, cx, cy - T - 8, BLK); break;
    case 3: d->fillRect(cx, cy - 10, L, 20, BLK); d->fillTriangle(cx, cy - T, cx, cy + T, cx - T - 8, cy, BLK); break;
    case 4: d->fillRect(cx - L, cy - 10, L, 20, BLK); d->fillTriangle(cx, cy - T, cx, cy + T, cx + T + 8, cy, BLK); break;
  }
}

static void pageLight() {
  const Readings& r = readings();
  char b[16];
  text(12, 46, "LUX", F9);
  if (isnan(r.lux)) snprintf(b, sizeof b, "--");
  else if (r.lux >= 10000) snprintf(b, sizeof b, "%.1fk", r.lux / 1000.0f);
  else snprintf(b, sizeof b, "%.0f", r.lux);
  text(12, 84, b, F24B);
  text(12, 116, "RGB", F9);
  uint16_t mx = max(max(r.r, r.g), max(r.b, (uint16_t)1));
  const char* nm[3] = {"R", "G", "B"};
  uint16_t v[3] = {r.r, r.g, r.b};
  for (int i = 0; i < 3; i++) {
    int y = 124 + i * 24;
    text(12, y + 12, nm[i], F9);
    hbar(32, y, 170, 16, (float)v[i] / mx);
  }
  text(12, 212, "PROXIMITY", F9);
  hbar(12, 218, 190, 16, r.prox / 255.0f);
  d->drawFastVLine(215, HDR, H - HDR, BLK);
  text(308, 46, "GESTURE", F9, AC);
  if (r.gesture && millis() - r.gestureMs < 3000) {
    arrow(308, 140, r.gesture);
  } else {
    text(308, 130, "swipe over", F12B, AC);
    text(308, 154, "the sensor", F12B, AC);
  }
}

// ---- SOUND -----------------------------------------------------------------
static const int HISTN = 180;
static float hist[HISTN];
static int histPos = 0;
static bool histFull = false;

static float dbFrac(float db) {
  float f = (db + 80.0f) / 70.0f;
  return f < 0 ? 0 : (f > 1 ? 1 : f);
}

static void pageSound() {
  const Readings& r = readings();
  hist[histPos] = r.micDb;
  histPos = (histPos + 1) % HISTN;
  if (histPos == 0) histFull = true;
  char b[16];
  snprintf(b, sizeof b, "%.0f", r.micDb);
  text(12, 70, b, F24B);
  text(12 + textW(b, F24B) + 8, 70, "dBFS", F12B);
  int bw = 376;
  hbar(12, 86, bw, 24, dbFrac(r.micDb));
  int px = 12 + 2 + (int)((bw - 4) * dbFrac(r.micPeakDb));
  d->fillRect(px - 1, 82, 3, 32, BLK);
  text(12, 128, "-80", F9);
  text(W - 12, 128, "-10", F9, AR);
  int gx = 12, gy = 142, gw = 376, gh = 86;
  d->drawRect(gx, gy, gw, gh, BLK);
  int n = histFull ? HISTN : histPos;
  int prevx = -1, prevy = 0;
  for (int i = 0; i < n; i++) {
    int idx = (histPos + HISTN - n + i) % HISTN;
    int x = gx + 1 + (int)((gw - 2) * (float)(HISTN - n + i) / (HISTN - 1));
    int y = gy + gh - 2 - (int)((gh - 4) * dbFrac(hist[idx]));
    if (prevx >= 0) d->drawLine(prevx, prevy, x, y, BLK);
    prevx = x; prevy = y;
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

uint32_t uiIntervalMs(Page p) { return p == PAGE_ENV ? 1000 : 200; }

static void present() {
  uint8_t* buf = d->getBuffer();
  if (!dirty && snap && memcmp(buf, snap, bufLen) == 0) return;
  d->refresh();
  if (snap) memcpy(snap, buf, bufLen);
  dirty = false;
}

void uiDraw(Page p, uint32_t heldMs) {
  d->fillRect(0, 0, W, H, WHT);
  header(p);
  switch (p) {
    case PAGE_ENV: pageEnv(); break;
    case PAGE_COMPASS: pageCompass(); break;
    case PAGE_MOTION: pageMotion(); break;
    case PAGE_LIGHT: pageLight(); break;
    case PAGE_SOUND: pageSound(); break;
    default: break;
  }
  if (heldMs >= 1500) {   // power-off banner
    d->fillRect(40, 92, 320, 64, WHT);
    d->drawRect(40, 92, 320, 64, BLK);
    d->drawRect(41, 93, 318, 62, BLK);
    text(200, 120, "Keep holding to power off", F9, AC);
    hbar(60, 130, 280, 16, (heldMs - 1500) / 1500.0f);
  }
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
