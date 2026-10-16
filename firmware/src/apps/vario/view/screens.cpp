#include "apps/vario/view/screens.h"
#include "apps/vario/view/terrain_map.h"
#include "apps/vario/view/prof.h"
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"
#include "platform/gfx/font.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

static int W = 400, H = 240;   // logical size, follows the canvas rotation
static const int HDR = 24;
static const int THERMAL_X0 = 22;   // left edge of the page area (right of the thin vario)
static const uint16_t BLK = 0, WHT = 1;
static const float PI_F = 3.14159265f;

#define F9   (&FreeSans9pt7b)
#define F9B  (&FreeSansBold9pt7b)
#define F12B (&FreeSansBold12pt7b)
#define F18B (&FreeSansBold18pt7b)
#define F24B (&FreeSansBold24pt7b)

static Adafruit_GFX* d;
static const Bitmap1* fbm;          // the same canvas as a Bitmap1, when the caller has one: text goes fast
static bool fastTextOn = true;

void viewFastText(bool on) { fastTextOn = on; }

static void begin(Adafruit_GFX& g, const Bitmap1* bm = nullptr) {
  d = &g;
  fbm = fastTextOn ? bm : nullptr;
  W = g.width();
  H = g.height();
}

// Pre-rotated glyph sheets (platform/gfx/font.h), built on first use for the canvas rotation and rebuilt (in
// the same memory) if the rotation changes. nullptr means: use Adafruit_GFX.
static Font1 fastFonts[5];
static const GFXfont* const fastFontSrc[5] = {&FreeSans9pt7b, &FreeSansBold9pt7b, &FreeSansBold12pt7b,
                                              &FreeSansBold18pt7b, &FreeSansBold24pt7b};
const GFXfont* viewGfxFont(int i) { return i >= 0 && i < 5 ? fastFontSrc[i] : nullptr; }
const Font1* viewFont(const GFXfont* f, int rot) {
  for (int i = 0; i < 5; i++)
    if (fastFontSrc[i] == f) return fastFonts[i].build(f, rot) ? &fastFonts[i] : nullptr;
  return nullptr;
}
static const Font1* fastFont(const GFXfont* f) { return fbm ? viewFont(f, fbm->rotation) : nullptr; }

size_t viewFontRam() {
  size_t n = 0;
  for (int i = 0; i < 5; i++) n += fastFonts[i].ramBytes();
  return n;
}

enum Align { AL, AC, AR };

static void text(int x, int y, const char* s, const GFXfont* f, Align a = AL, uint16_t col = BLK, int sc = 1) {
  if (const Font1* ft = sc == 1 ? fastFont(f) : nullptr) {
    int x1, y1, w, h;
    textBounds(s, *ft, x1, y1, w, h, W);
    int cx = x - x1;
    if (a == AC) cx = x - w / 2 - x1;
    else if (a == AR) cx = x - w - x1;
    drawText(*fbm, cx, y, s, *ft, col ? DRAW_SET : DRAW_CLEAR, W);
    return;
  }
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

// Centre a string on (cx, cy) using its real glyph bounds, so letters sit in the middle of boxes.
static void textMid(int cx, int cy, const char* s, const GFXfont* f, uint16_t col = BLK) {
  if (const Font1* ft = fastFont(f)) {
    int x1, y1, w, h;
    textBounds(s, *ft, x1, y1, w, h, W);
    drawText(*fbm, cx - w / 2 - x1, cy - y1 - h / 2, s, *ft, col ? DRAW_SET : DRAW_CLEAR, W);
    return;
  }
  d->setFont(f);
  d->setTextSize(1);
  d->setTextColor(col);
  int16_t x1, y1;
  uint16_t w, h;
  d->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  d->setCursor(cx - (int)w / 2 - x1, cy - y1 - (int)h / 2);
  d->print(s);
}

static int textW(const char* s, const GFXfont* f) {
  if (const Font1* ft = fastFont(f)) return textWidth(s, *ft, W);
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

// Lines and circle outlines: straight into the bitmap when there is one (same pixels as Adafruit_GFX,
// without a virtual call per pixel), through the canvas otherwise.
static void gLine(int x0, int y0, int x1, int y1, uint16_t col) {
  if (fbm) line(*fbm, x0, y0, x1, y1, col ? DRAW_SET : DRAW_CLEAR);
  else d->drawLine(x0, y0, x1, y1, col);
}
static void gCircle(int cx, int cy, int r, uint16_t col) {
  if (fbm) circle(*fbm, cx, cy, r, col ? DRAW_SET : DRAW_CLEAR);
  else d->drawCircle(cx, cy, r, col);
}

static void hbar(int x, int y, int w, int h, float frac) {
  if (!isfinite(frac)) frac = 0;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d->drawRect(x, y, w, h, BLK);
  d->fillRect(x + 2, y + 2, (int)((w - 4) * frac), h - 4, BLK);
}

static void header(const ViewModel& vm, const char* title = nullptr) {
  d->fillRect(0, 0, W, HDR, BLK);
  char b[16];
  if (title) {
    text(6, 17, title, F9, AL, WHT);
  } else {
    if (vm.fresh && vm.localMinutes >= 0) snprintf(b, sizeof b, "%02d:%02d", vm.localMinutes / 60, vm.localMinutes % 60);
    else snprintf(b, sizeof b, "--:--");
    text(6, 19, b, F12B, AL, WHT);
    if (vm.fresh && vm.s.battPct >= 0) snprintf(b, sizeof b, "XC %d%%", vm.s.battPct);
    else snprintf(b, sizeof b, "XC --");
    text(76, 17, b, F9, AL, WHT);
    if (vm.sim) text(128, 17, "SIM", F9, AL, WHT);
  }
  int pct = vm.localBattPct < 0 ? 0 : (vm.localBattPct > 100 ? 100 : vm.localBattPct);
  snprintf(b, sizeof b, "%d%%", pct);
  text(W - 44, 17, b, F9, AR, WHT);
  d->drawRect(W - 40, 6, 30, 13, WHT);
  d->fillRect(W - 10, 9, 3, 7, WHT);
  d->fillRect(W - 38, 8, (26 * pct) / 100, 9, WHT);
}

// Two shades, one meaning each everywhere: LIGHT for faces, zones and backgrounds, MID for older or
// secondary data. Text, numbers, arrows and the newest trail are never drawn on them.
static const int LIGHT = 13, MID = 8;

// Dithered pixel: one table lookup, for grey lines and dots (rects and spans use the byte-wise paths).
static inline void greyPx(const Bitmap1& bm, int x, int y, int level) {
  static uint8_t thr[64];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 64; i++) thr[i] = (uint8_t)bayer8(i & 7, i >> 3);
    init = true;
  }
  bm.pixel(x, y, thr[(y & 7) * 8 + (x & 7)] < 4 * level ? DRAW_SET : DRAW_CLEAR);
}

// Segment of w x w grey squares, like thickLine does in black.
static void greyLine(const Bitmap1& bm, int x0, int y0, int x1, int y1, int w, int level) {
  int dx = x1 - x0, dy = y1 - y0;
  int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  if (steps < 1) steps = 1;
  for (int i = 0; i <= steps; i++) {
    int x = x0 + (int)lroundf((float)dx * i / steps), y = y0 + (int)lroundf((float)dy * i / steps);
    for (int j = 0; j < w; j++)
      for (int k = 0; k < w; k++) greyPx(bm, x + k, y + j, level);
  }
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
static void thinVario(const Bitmap1* bm, int x, int w, float v, float avg) {
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
    else if (bm) fillRect(*bm, x + 2, cy + 1, w - 4, y - cy, Gray(MID));   // sink: grey, climb stays black
    else d->fillRect(x + 2, cy, w - 4, y - cy + 1, BLK);
  }
  int ya = cy - (int)(half * barPos(avg));
  d->fillRect(x + w + 1, ya - 2, 7, 5, BLK);
}

static const char* cardinal16(float deg) {
  static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return n[((int)((deg + 11.25f) / 22.5f)) % 16];
}

// Heading-up compass in the Garmin style: the dial rotates so the current heading is under the fixed
// pointer at the top; big N/E/S/W and smaller NE/SE/SW/NW sit on the ring, ticks every 10 degrees
// (longer every 30), the heading in degrees is the big number in the centre with its source above it
// (TRK = GPS track, MAG = magnetic compass). With no heading the dial stays north-up and shows "--".
static void compassDial(int cx, int cy, int R, float heading, HeadingSource src) {
  const bool have = isfinite(heading);
  const float course = have ? heading : 0.0f;
  PROF_BEGIN(P_BEZEL);
  gCircle(cx, cy, R, BLK);
  gCircle(cx, cy, R - 1, BLK);
  d->fillTriangle(cx - 8, cy - R - 4, cx + 8, cy - R - 4, cx, cy - R + 11, BLK);   // direction of travel
  PROF_END(P_BEZEL);
  PROF_BEGIN(P_TICKS);
  const int rl = R - 26;   // label radius, clear of the grey ring (R-1 .. R-12)
  for (int a = 0; a < 360; a += 10) {
    if (a % 45 == 0) continue;   // letters sit there
    float th = (a - course) * PI_F / 180.0f;
    int len = (a % 30 == 0) ? 9 : 5;
    gLine(cx + (R - 2) * sinf(th), cy - (R - 2) * cosf(th),
                cx + (R - 2 - len) * sinf(th), cy - (R - 2 - len) * cosf(th), BLK);
  }
  PROF_END(P_TICKS);
  PROF_BEGIN(P_LABELS);
  static const char* L[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  for (int i = 0; i < 8; i++) {
    float th = (i * 45 - course) * PI_F / 180.0f;
    int lx = cx + rl * sinf(th), ly = cy - rl * cosf(th);
    if (i == 0) {
      d->fillRect(lx - 12, ly - 12, 25, 25, BLK);
      textMid(lx, ly, "N", F12B, WHT);
    } else if (i % 2 == 0) {
      textMid(lx, ly, L[i], F12B);
    } else {
      textMid(lx, ly, L[i], F9);
    }
  }
  PROF_END(P_LABELS);
  PROF_BEGIN(P_HDGNUM);
  char b[8];
  if (have) {
    if (src != HDG_NONE) text(cx, cy - 14, src == HDG_MAG ? "MAG" : "TRK", F9, AC);
    text(cx, cy + 12, fnum(b, sizeof b, heading, "%.0f"), F18B, AC);
  } else {
    text(cx, cy + 8, "--", F18B, AC);
  }
  PROF_END(P_HDGNUM);
}

// Arrow centred on (ax, ay) pointing along screen angle th (radians clockwise from up), half length L.
static void drawArrow(int ax, int ay, float th, int L, int hw = 9) {
  if (!isfinite(th)) return;                               // NaN -> int is undefined behaviour
  float ux = sinf(th), uy = -cosf(th), px = -uy, py = ux;
  int tx = ax - L * ux, ty = ay - L * uy;                  // tail
  int hx = ax + 3 * ux, hy = ay + 3 * uy;                  // base of the head
  int ex = ax + L * ux, ey = ay + L * uy;                  // tip
  if (hw > 9) {                                            // big arrow: a solid 5 px shaft (parallel lines leave gaps on diagonals)
    int qx = (int)lroundf(2 * px), qy = (int)lroundf(2 * py);
    d->fillTriangle(tx + qx, ty + qy, tx - qx, ty - qy, hx + qx, hy + qy, BLK);
    d->fillTriangle(tx - qx, ty - qy, hx - qx, hy - qy, hx + qx, hy + qy, BLK);
  } else {
    for (int k = -1; k <= 1; k++) gLine(tx + k * px, ty + k * py, hx + k * px, hy + k * py, BLK);
  }
  d->fillTriangle(ex, ey, hx + hw * px, hy + hw * py, hx - hw * px, hy - hw * py, BLK);
}

// The wind arrow lives in the compass corner the wind comes from and points the way it blows,
// relative to the heading: top-right corner + arrow down-left = wind from front-right.
static void windCorner(int cx, int cy, int R, float course, const Wind& w) {
  if (!w.valid || !isfinite(course)) return;
  float from = fmodf(w.toDeg + 180.0f, 360.0f);
  float psi = fmodf(from - course + 360.0f, 360.0f);      // 0 = from straight ahead
  int right = psi < 180.0f ? 1 : -1;
  int front = (psi < 90.0f || psi >= 270.0f) ? -1 : 1;     // -1 = top
  int off = (int)(0.86f * (R + 10));
  int ax = cx + right * off, ay = cy + front * off;
  drawArrow(ax, ay, (psi + 180.0f) * PI_F / 180.0f, 17);   // points the way the wind blows
}

static void cell(int x, int y, int w, const char* title, const char* value);

// Light grey ring (outer radius R, inner Ri) under the dial's ticks. Dithered, so it goes in before
// any GFX drawing; the letters and the heading sit on the white inside.
// Ri = 0 makes it a disc. Clipped to the box (cl0, ct, cl1, cb) so a disc cannot spill over a frame.
static void greyRing(const Bitmap1& bm, int cx, int cy, int R, int Ri, int level,
                     int cl0 = -9999, int ct = -9999, int cl1 = 9999, int cb = 9999) {
  fillRing(bm, cx, cy, R, Ri, Gray(level), ClipBox(cl0, ct, cl1, cb));
}

// No vario data: a standalone compass. Top line = link state and device, then the big dial, then
// ambient temperature and humidity. bm (optional) takes the dithered grey; text never sits on it.
static void drawStandalone(const ViewModel& vm, const Bitmap1* bm) {
  const int cx = W / 2, R = 104, cy = 170, cellW = 114, cellY = 304, cellH = 64;
  char top[48], b[24];
  const char* title = vm.link == ViewModel::LINK_CONNECTED ? "CONNECTED"
                      : vm.link == ViewModel::LINK_CONNECTING ? "CONNECTING"
                      : vm.link == ViewModel::LINK_SEARCHING ? "SEARCHING" : "NO VARIO";
  if (vm.link == ViewModel::LINK_CONNECTED) snprintf(top, sizeof top, "%s  no data yet", title);
  else if (vm.link == ViewModel::LINK_NONE) snprintf(top, sizeof top, "%s  hold: choose device", title);
  else if (vm.hasSaved) snprintf(top, sizeof top, "%s  %.14s", title, vm.device);
  else snprintf(top, sizeof top, "%s  filter: %.10s", title, vm.filter);

  PROF_BEGIN(P_RING);
  if (bm) greyRing(*bm, cx, cy, R - 1, R - 12, LIGHT);
  PROF_END(P_RING);
  text(cx, 42, top, F9, AC);
  compassDial(cx, cy, R, vm.heading, vm.headingSrc);

  cell(4, cellY, cellW, "TEMP", fnum(b, sizeof b, vm.ambientC, "%.1f"));
  int tw = textW("TEMP", F9);
  gCircle(4 + 5 + tw + 6, cellY + 6, 2, WHT);                       // F9 has no degree sign
  text(4 + 5 + tw + 10, cellY + 14, "C", F9, AL, WHT);
  cell(4 + cellW + 4, cellY, cellW, "HUMIDITY %", fnum(b, sizeof b, vm.humidityPct, "%.0f"));

  if (vm.link == ViewModel::LINK_CONNECTED) {
    snprintf(b, sizeof b, "%lu ok, %lu bad", (unsigned long)vm.linesOk, (unsigned long)vm.linesBad);
    text(cx, H - 12, b, F9, AC);
  } else if (vm.link != ViewModel::LINK_NONE) {
    text(cx, H - 12, "hold: choose device", F9, AC);
  }
}

static int dashPhase = 0;

// Solid w x w squares stamped along the segment (no gaps); optionally dashed for the older memory.
static void thickLine(int x0, int y0, int x1, int y1, int w, bool dashed) {
  if (w <= 1 && !dashed) { gLine(x0, y0, x1, y1, BLK); return; }
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
  if (steps < 1) steps = 1;
  int o = w / 2;
  for (int i = 0; i <= steps; i++) {
    if (dashed && ((dashPhase++ / 3) & 1)) continue;     // 3 px on, 3 px off
    d->fillRect(x0 + (int)lroundf(dx * i / steps) - o, y0 + (int)lroundf(dy * i / steps) - o, w, w, BLK);
  }
}

static int climbWidth(float v) { return v < 0.2f ? 1 : (v < 1.0f ? 2 : (v < 2.5f ? 3 : 4)); }

// Liang-Barsky: clip a segment to a rectangle; false if nothing of it is inside.
static bool clipSeg(float& x0, float& y0, float& x1, float& y1, float xmin, float ymin, float xmax, float ymax) {
  float dx = x1 - x0, dy = y1 - y0, t0 = 0, t1 = 1;
  float p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) { if (q[i] < 0) return false; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
    else { if (r < t0) return false; if (r < t1) t1 = r; }
  }
  float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy, nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
  x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
  return true;
}

static float gRR[ViewModel::TRAIL_MAX + 1], gUU[ViewModel::TRAIL_MAX + 1];   // static: far too big for the 4 KB loop stack

static float gMr[ViewModel::MARKS_MAX], gMu[ViewModel::MARKS_MAX];

// The map in screen metres (r right, u up) around the view centre, turned heading-up. Live motion: the
// trail's points up to the display time, then the centre itself as the last point, so the line ends under
// the arrow. Returns the point count (the trail points plus the centre, if live) and the map rotation.
static int mapPrepare(const ViewModel& vm, float& hdgOut) {
  const int n0 = vm.trailCount;
  const bool live = vm.mapLive;
  const int m = n0 + (live ? 1 : 0);
  float hdg = 0;
  if (vm.mapHeadingUp) {
    if (live && isfinite(vm.mapHdg)) hdg = vm.mapHdg;
    else if (n0 >= 2) {
      // direction of travel in the air frame: the last ~3 s of the trace (GPS course if barely moving)
      const AirPoint& a = vm.trail[n0 >= 4 ? n0 - 4 : 0];
      float dx = vm.trail[n0 - 1].x - a.x, dy = vm.trail[n0 - 1].y - a.y;
      if (hypotf(dx, dy) > 3.0f) hdg = atan2f(dx, dy);
      else if (isfinite(vm.s.course)) hdg = vm.s.course * PI_F / 180.0f;
    }
  }
  if (!isfinite(hdg)) hdg = 0;                                    // never rotate by NaN
  hdgOut = hdg;
  const float ct = cosf(hdg), st = sinf(hdg);
  const float ox = live ? vm.mapCx : 0, oy = live ? vm.mapCy : 0;
  for (int i = 0; i < n0; i++) {
    float x = vm.trail[i].x - ox, y = vm.trail[i].y - oy;
    gRR[i] = x * ct - y * st;
    gUU[i] = y * ct + x * st;
  }
  if (live) { gRR[n0] = 0; gUU[n0] = 0; }
  const int nm = vm.markCount;
  for (int i = 0; i < nm; i++) {
    float x = vm.marks[i].x - ox, y = vm.marks[i].y - oy;
    gMr[i] = x * ct - y * st;
    gMu[i] = y * ct + x * st;
  }
  return m;
}

// zoom: fit the last 30 s, plus the newest thermal when it is within 800 m; fixed steps, so the picture
// does not breathe (the app glides between steps)
static float mapPickScale(int m, int nm, int mw, int mh) {
  float smax = 1e9f;
  const int margin = 14;
  auto fit = [&](float r, float u) {
    if (fabsf(r) > 1) smax = fminf(smax, (mw / 2 - margin) / fabsf(r));
    if (fabsf(u) > 1) smax = fminf(smax, (mh / 2 - margin) / fabsf(u));
  };
  for (int i = m > 30 ? m - 30 : 0; i < m; i++) fit(gRR[i], gUU[i]);
  if (nm > 0 && hypotf(gMr[0], gMu[0]) <= 800) fit(gMr[0], gMu[0]);
  static const float zoom[] = {2.2f, 1.6f, 1.1f, 0.7f, 0.45f, 0.28f, 0.18f, 0.11f};
  float s = zoom[7];
  for (float z : zoom) if (z <= smax) { s = z; break; }
  return s;
}

void thermalMapSize(int canvasW, int canvasH, int& mw, int& mh) {
  mw = canvasW - THERMAL_X0 - 2;
  mh = canvasH - 6 - 25 - 5 - (HDR + 130 + 2);
}

float thermalMapScale(const ViewModel& vm, int mw, int mh) {
  float hdg;
  int m = mapPrepare(vm, hdg);
  return mapPickScale(m, vm.markCount, mw, mh);
}

// Thermal assistant. Heading-up map: the last 3 minutes of track in the air-mass frame (shifted by the
// wind drift; the last 90 s solid and thicker where you were climbing, older track dashed) plus markers
// for the thermals you remember, each with its average climb. The newest marker, the thermal you last
// worked, also gets an arrow at the frame edge with the distance when it is off the map, and the zoom
// stays wide enough to show it. Fixed zoom levels, so the picture does not breathe.
static void drawThermalMap(const ViewModel& vm, const Bitmap1* bm, int mx, int my, int mw, int mh) {
  d->drawRect(mx, my, mw, mh, BLK);
  float hdg;
  const int n = mapPrepare(vm, hdg);                            // trail points, plus the centre when live
  if (n < 2) { text(mx + mw / 2, my + mh / 2 + 6, "no GPS track", F12B, AC); return; }
  const int ox = mx + mw / 2, oy = my + mh / 2;                 // you, in the centre
  const int nm = vm.markCount;
  const float s = (isfinite(vm.mapScale) && vm.mapScale > 0) ? vm.mapScale : mapPickScale(n, nm, mw, mh);
  auto vario = [&](int i) { return i < vm.trailCount ? vm.trail[i].vario : vm.mapTailVario; };

  // Grey first (it overwrites whatever is under it), then everything black on top. Remembered thermals
  // get a LIGHT disc, bigger for a stronger climb; the older track is MID grey, 2 px or more.
  static int gSx[ViewModel::MARKS_MAX], gSy[ViewModel::MARKS_MAX];
  static bool gIn[ViewModel::MARKS_MAX];
  for (int i = nm - 1; i >= 0; i--) {
    gSx[i] = (int)lroundf(ox + gMr[i] * s);
    gSy[i] = (int)lroundf(oy - gMu[i] * s);
    gIn[i] = gSx[i] > mx + 14 && gSx[i] < mx + mw - 14 && gSy[i] > my + 14 && gSy[i] < my + mh - 14;
    if (bm && gIn[i]) {
      float cl = isfinite(vm.marks[i].avgClimb) ? fminf(fmaxf(vm.marks[i].avgClimb, 0.0f), 4.0f) : 0.0f;
      int rg = (i == 0 ? 12 : 9) + (int)lroundf(2.5f * cl);
      greyRing(*bm, gSx[i], gSy[i], rg, 0, LIGHT, mx + 2, my + 2, mx + mw - 3, my + mh - 3);
    }
  }

  // trail, oldest first, clipped to the frame; anything older than 90 s is the memory
  PROF_BEGIN(P_TRAIL);
  dashPhase = 0;
  const float cx0 = mx + 4, cy0 = my + 4, cx1 = mx + mw - 4, cy1 = my + mh - 4;
  for (int pass = bm ? 0 : 1; pass < 2; pass++)
    for (int i = 1; i < n; i++) {
      const bool old = (n - 1 - i) > 90;
      if (bm && old != (pass == 0)) continue;
      float x0 = ox + gRR[i - 1] * s, y0 = oy - gUU[i - 1] * s, x1 = ox + gRR[i] * s, y1 = oy - gUU[i] * s;
      if (!clipSeg(x0, y0, x1, y1, cx0, cy0, cx1, cy1)) continue;
      const int ax = (int)lroundf(x0), ay = (int)lroundf(y0), bx = (int)lroundf(x1), by = (int)lroundf(y1);
      if (bm && old) {
        int cw = climbWidth(vario(i));
        greyLine(*bm, ax, ay, bx, by, cw < 2 ? 2 : cw, MID);
      } else {
        thickLine(ax, ay, bx, by, climbWidth(vario(i)), old);
      }
    }

  PROF_END(P_TRAIL);
  PROF_BEGIN(P_MARKS);
  // remembered thermals: a fixed-size ring (the grey disc is already down), the newest one heavier
  char lb[16];
  for (int i = nm - 1; i >= 0; i--) {
    float tx = ox + gMr[i] * s, ty = oy - gMu[i] * s;
    if (gIn[i]) {
      int rad = i == 0 ? 10 : 7;
      gCircle(gSx[i], gSy[i], rad, BLK);
      if (i == 0) { gCircle(gSx[i], gSy[i], rad - 1, BLK); d->fillCircle(gSx[i], gSy[i], 3, BLK); }
      else d->fillCircle(gSx[i], gSy[i], 2, BLK);
    } else if (i == 0) {                                        // newest thermal off the map: edge arrow + distance
      float vx = tx - ox, vy = ty - oy, len = hypotf(vx, vy);
      if (len > 1) {
        float ux = vx / len, uy = vy / len;
        float t = fminf(fabsf(ux) > 1e-3f ? (mw / 2 - 16) / fabsf(ux) : 1e9f, fabsf(uy) > 1e-3f ? (mh / 2 - 16) / fabsf(uy) : 1e9f);
        int ex = ox + (int)lroundf(ux * t), ey = oy + (int)lroundf(uy * t);
        d->fillCircle(ex, ey, 14, WHT);
        d->fillCircle(ex, ey, 11, BLK);                          // badge with a white arrow pointing outward
        float th2 = atan2f(ux, -uy);
        float sx = sinf(th2), cy = -cosf(th2), qx = -cy, qy = sx;
        int hx = ex + (int)lroundf(4 * sx), hy = ey + (int)lroundf(4 * cy);
        d->fillTriangle(ex + (int)lroundf(9 * sx), ey + (int)lroundf(9 * cy),
                        hx + (int)lroundf(6 * qx), hy + (int)lroundf(6 * qy),
                        hx - (int)lroundf(6 * qx), hy - (int)lroundf(6 * qy), WHT);
        float dist = hypotf(gMr[0], gMu[0]);
        if (dist >= 1000) snprintf(lb, sizeof lb, "%.1f km", dist / 1000.0f);
        else snprintf(lb, sizeof lb, "%d m", (int)(dist + 0.5f));
        int lx = ex - (int)lroundf(ux * 34), ly = ey - (int)lroundf(uy * 22);
        lx = lx < mx + 28 ? mx + 28 : (lx > mx + mw - 28 ? mx + mw - 28 : lx);
        ly = ly < my + 12 ? my + 12 : (ly > my + mh - 12 ? my + mh - 12 : ly);
        textMid(lx, ly, lb, F9);
      }
    }
  }
  // strength labels, newest first; a label that would land on another one is skipped
  int lxs[ViewModel::MARKS_MAX], lys[ViewModel::MARKS_MAX], nl = 0;
  for (int i = 0; i < nm; i++) {
    if (!gIn[i]) continue;
    snprintf(lb, sizeof lb, "%+.1f", vm.marks[i].avgClimb);
    int w = textW(lb, F9), rad = i == 0 ? 10 : 7;
    int lx = gSx[i] + rad + 3, ly = gSy[i] + 5;                 // right of the ring, baseline
    if (lx + w > mx + mw - 4) lx = gSx[i] - rad - 3 - w;        // no room: left of it
    bool clash = false;
    for (int k = 0; k < nl; k++) if (abs(lx - lxs[k]) < 36 && abs(ly - lys[k]) < 15) clash = true;
    if (clash) continue;
    lxs[nl] = lx; lys[nl] = ly; nl++;
    d->fillRect(lx - 2, ly - 13, w + 4, 16, WHT);               // keep it legible over the trail
    text(lx, ly, lb, F9);
  }

  PROF_END(P_MARKS);
  // you: a small arrow pointing up
  d->fillTriangle(ox, oy - 9, ox - 6, oy + 6, ox + 6, oy + 6, BLK);
  d->fillTriangle(ox, oy - 3, ox - 2, oy + 3, ox + 2, oy + 3, WHT);

  // north marker (top-left) and wind arrow (top-right), both in the map's frame
  float nth = -hdg;                                              // screen direction of north
  int nax = mx + 28, nay = my + 30;
  d->fillRect(mx + 4, my + 4, 46, 50, WHT);                     // white patch: no grey trail behind the N
  drawArrow(nax, nay, nth, 11);
  textMid(nax + (int)lroundf(21 * sinf(nth)), nay - (int)lroundf(21 * cosf(nth)), "N", F9);
  if (vm.wind.valid) d->fillRect(mx + mw - 42, my + 6, 38, 36, WHT);
  if (vm.wind.valid) drawArrow(mx + mw - 22, my + 24, vm.wind.toDeg * PI_F / 180.0f - hdg, 14);

  static const int bars[] = {10, 20, 50, 100, 200, 500};
  int barM = bars[0];
  for (int bsel : bars) if (bsel * s >= 30 && bsel * s <= 90) { barM = bsel; break; }
  int bl = (int)lroundf(barM * s), by = my + mh - 8;
  d->fillRect(mx + 5, by - 19, bl + 6, 25, WHT);                 // keep the scale bar readable over the trail
  d->drawFastHLine(mx + 8, by, bl, BLK);
  d->drawFastVLine(mx + 8, by - 3, 7, BLK);
  d->drawFastVLine(mx + 8 + bl, by - 3, 7, BLK);
  snprintf(lb, sizeof lb, "%d m", barM);
  text(mx + 8, by - 7, lb, F9);
}

// Wind profile page. The altitude axis runs up the left; each 100 m band that has data is a row with
// a north-up arrow (the way the wind blows) and the speed in km/h, in brackets once it is over 20
// minutes old. The range follows the flight: lowest to highest band with data, widened to include the
// current altitude. Rows use the page height (16..36 px each); when more rows than fit at 16 px exist,
// the window nearest the current altitude is shown. A triangle and a thin line mark where you are.
// With temperatures, a column on the right traces them on the same altitude scale (3 px thick where
// the air warms with height, an inversion), the lapse rate sits in a header strip above it and its
// scale below. Without any temperature the page is laid out exactly as before.
static void drawWindProfile(const ViewModel& vm, const Bitmap1* bm, int mx, int my, int mw, int mh) {
  d->drawRect(mx, my, mw, mh, BLK);
  const int n = vm.profileCount;
  if (n < 1) {
    text(mx + mw / 2, my + mh / 2 - 4, "no wind measured yet", F9, AC);
    text(mx + mw / 2, my + mh / 2 + 20, "circle or turn to measure", F9, AC);
    return;
  }
  bool haveT = false, anyWind = false;
  for (int i = 0; i < n; i++) {
    if (isfinite(vm.profile[i].tempC)) haveT = true;
    if (isfinite(vm.profile[i].speedKmh)) anyWind = true;
  }
  const int hs = haveT ? 20 : 0, fs = haveT ? 14 : 0;           // header and footer strips for the temperature column
  const int top = my + 8 + hs, areaH = mh - 16 - hs - fs;
  int lo = WindProfile::bandOf(vm.profile[0].altM), hi = WindProfile::bandOf(vm.profile[n - 1].altM);
  const bool haveAlt = isfinite(vm.profileAlt);
  const int cur = haveAlt ? WindProfile::bandOf(vm.profileAlt) : -1;
  if (haveAlt) { if (cur < lo) lo = cur; if (cur > hi) hi = cur; }
  int rows = hi - lo + 1;
  const int maxRows = areaH / 16;
  if (rows > maxRows) {                                           // window nearest the current altitude
    int centre = haveAlt ? cur : (lo + hi) / 2;
    int nlo = centre - maxRows / 2;
    if (nlo < lo) nlo = lo;
    if (nlo > hi - maxRows + 1) nlo = hi - maxRows + 1;
    lo = nlo;
    hi = lo + maxRows - 1;
    rows = maxRows;
  }
  int rh = areaH / rows;
  rh = rh < 16 ? 16 : (rh > 36 ? 36 : rh);
  const int y0 = top + (areaH - rows * rh) / 2;                   // block top, centred when rows are capped
  const bool big = rh >= 24;
  const GFXfont* nf = big ? F12B : F9;
  char b[16];

  // altitude axis: a label and tick on every 500 m line
  for (int k = ((lo * 100 + 499) / 500) * 500; k <= (hi + 1) * 100; k += 500) {
    int y = y0 + ((hi + 1) * 100 - k) * rh / 100;
    d->drawFastHLine(mx + 44, y, 6, BLK);
    text(mx + 38, y + 5, fnum(b, sizeof b, (float)k, "%.0f"), F9, AR);
  }
  d->drawFastVLine(mx + 50, y0, rows * rh, BLK);

  // An inversion layer is a LIGHT band across the temperature column, behind the thick trace. Grey goes
  // down first; the trace (no text) is drawn over it.
  if (bm && haveT) {
    int pband = -10;
    for (int i = 0; i < n; i++) {
      int band = WindProfile::bandOf(vm.profile[i].altM);
      if (band < lo || band > hi || !isfinite(vm.profile[i].tempC)) continue;
      if (band == pband + 1 && inversionAt(vm.profile, n, i))
        fillRect(*bm, mx + mw - 88, y0 + (hi - band) * rh + rh / 2, 80, rh, Gray(LIGHT));
      pband = band;
    }
  }

  // current altitude: line first, so the row text can sit on a white patch over it
  if (haveAlt) {
    float f = ((hi + 1) * 100 - vm.profileAlt) * rh / 100.0f;
    int ya = y0 + (int)lroundf(f < 0 ? 0 : (f > rows * rh ? rows * rh : f));
    d->drawFastHLine(mx + 52, ya, mw - 56, BLK);
    d->fillTriangle(mx + 53, ya, mx + 43, ya - 6, mx + 43, ya + 6, BLK);
  }

  const int ax = mx + 74, tx = mx + 94;
  for (int i = 0; i < n; i++) {
    int band = WindProfile::bandOf(vm.profile[i].altM);
    if (band < lo || band > hi) continue;
    int cy = y0 + (hi - band) * rh + rh / 2;
    const ProfileBand& pb = vm.profile[i];
    if (!isfinite(pb.speedKmh)) continue;                         // temperature only
    fnum(b, sizeof b, pb.speedKmh, "%.0f");
    if (pb.ageS > 20 * 60) {
      char c[16];
      snprintf(c, sizeof c, "(%s)", b);
      memcpy(b, c, sizeof b);
    }
    int w = textW(b, nf);
    d->fillRect(tx - 3, cy - 8, w + 6, 16, WHT);
    text(tx, cy + (big ? 7 : 5), b, nf);
    float th = isfinite(pb.toDeg) ? pb.toDeg * PI_F / 180.0f : NAN;
    if (big) d->fillRect(ax - 14, cy - 14, 28, 28, WHT);
    drawArrow(ax, cy, th, big ? 12 : 7, big ? 7 : 5);
  }

  if (!haveT) {
    // north indicator (top right) and the unit
    d->fillRect(mx + mw - 54, my + 3, 52, 28, WHT);
    drawArrow(mx + mw - 14, my + 17, 0, 9, 5);
    textMid(mx + mw - 36, my + 17, "N", F9);
    text(mx + mw - 6, my + 46, "km/h", F9, AR);
    return;
  }

  // header: the unit over the wind columns, the lapse rate over the temperature; footer: north arrow
  // under the wind columns, the temperature scale under its column
  text(mx + 56, my + 17, anyWind ? "km/h" : "no wind", F9);
  textMid(mx + 62, my + mh - 11, "N", F9);
  drawArrow(mx + 76, my + mh - 11, 0, 7, 5);
  const int colL = mx + mw - 84, colR = mx + mw - 8;
  float lr = lapseRate(vm.profile, n);
  fnum(b, sizeof b, lr, "%+.1f");
  if (isfinite(lr)) strncat(b, "/100m", sizeof b - strlen(b) - 1);
  text(mx + mw - 6, my + 17, b, F9, AR);

  float tmin = 1e9f, tmax = -1e9f;
  for (int i = 0; i < n; i++) {
    int band = WindProfile::bandOf(vm.profile[i].altM);
    if (band < lo || band > hi) continue;
    float t = vm.profile[i].tempC;
    if (!isfinite(t)) continue;
    if (t < tmin) tmin = t;
    if (t > tmax) tmax = t;
  }
  if (tmin > tmax) return;                                        // no temperature inside the shown altitudes
  if (tmax - tmin < 4.0f) { float mid = (tmin + tmax) / 2; tmin = mid - 2; tmax = mid + 2; }
  const float xs = (colR - colL) / (tmax - tmin);
  int px = 0, py = 0, pband = -10;
  for (int i = 0; i < n; i++) {
    int band = WindProfile::bandOf(vm.profile[i].altM);
    float t = vm.profile[i].tempC;
    if (band < lo || band > hi || !isfinite(t)) continue;
    int x = colL + (int)lroundf((t - tmin) * xs), y = y0 + (hi - band) * rh + rh / 2;
    if (band == pband + 1) thickLine(px, py, x, y, inversionAt(vm.profile, n, i) ? 3 : 1, false);
    d->fillRect(x - 1, y - 1, 2, 2, BLK);
    px = x; py = y; pband = band;
  }
  // scale below the column: the two ends of the temperature axis
  const int yb = my + mh - 18;
  d->drawFastHLine(colL, yb, colR - colL + 1, BLK);
  d->drawFastVLine(colL, yb - 2, 5, BLK);
  d->drawFastVLine(colR, yb - 2, 5, BLK);
  fnum(b, sizeof b, tmin, "%.0f"); strncat(b, "C", sizeof b - strlen(b) - 1);
  text(colL, my + mh - 4, b, F9);
  fnum(b, sizeof b, tmax, "%.0f"); strncat(b, "C", sizeof b - strlen(b) - 1);
  text(colR, my + mh - 4, b, F9, AR);
}

// Home page: the way back to the takeoff point. Heading-up arrow, distance, required glide ratio and
// the estimated height on arrival (wind and a typical sink rate included).
static void drawHome(const ViewModel& vm, int mx, int my, int mw, int mh) {
  d->drawRect(mx, my, mw, mh, BLK);
  const int cx = mx + mw / 2;
  if (!vm.homeValid) {
    text(cx, my + mh / 2 + 6, "no home yet", F12B, AC);
    return;
  }
  char b[24], c[24];
  text(cx, my + 20, vm.homeProvisional ? "HOME (provisional)" : "HOME", F9, AC);
  const bool north = !isfinite(vm.homeRelBearing);
  if (north) text(mx + 6, my + 44, "N up", F9);
  float ang = north ? vm.homeBearing : vm.homeRelBearing;
  drawArrow(cx, my + 64, isfinite(ang) ? ang * PI_F / 180.0f : NAN, 32, 13);
  if (!isfinite(ang)) text(cx, my + 80, "--", F18B, AC);   // no position now: nothing to point at

  // distance, centred as number + unit
  const float dm = vm.homeDist;
  const bool km = isfinite(dm) && dm >= 1000;
  fnum(b, sizeof b, km ? dm / 1000.0f : dm, km ? "%.1f" : "%.0f");
  const char* unit = km ? "km" : "m";
  int wn = textW(b, F24B), wu = textW(unit, F12B), x = cx - (wn + 6 + wu) / 2;
  text(x, my + 136, b, F24B);
  text(x + wn + 6, my + 136, unit, F12B);

  snprintf(c, sizeof c, "GR req %s", fnum(b, sizeof b, vm.homeGlideReq, "%.1f"));
  text(cx, my + 164, c, F12B, AC);
  if (vm.homeReachable || !isfinite(vm.homeDist)) {
    snprintf(c, sizeof c, "arrival %s m", fnum(b, sizeof b, vm.homeArrival, "%+.0f"));
    text(cx, my + 192, c, F12B, AC);
  } else {
    d->fillRect(mx + 4, my + 172, mw - 8, 28, BLK);
    textMid(cx, my + 186, "OUT OF REACH", F12B, WHT);
  }
}

// ---- MAP PAGE ------------------------------------------------------------------
// The frame right of the thin vario and below the header, all of it map.
void mapPageSize(int canvasW, int canvasH, int& mw, int& mh) {
  mw = canvasW - THERMAL_X0;
  mh = canvasH - HDR;
}

// Copy the cached relief layer (view/terrain_map.h) into the frame. Both are in the panel's native layout when the
// canvas is the portrait panel, then whole bytes move; anything else goes through the generic blit.
static void blitMapCache(const Bitmap1& dst, int x0, int y0, const Bitmap1& src) {
  const int sw = src.width(), sh = src.height();                 // logical size of the layer
  const int off = dst.w - y0 - sh;                               // native column of the layer's first column
  if (dst.rotation == 1 && src.rotation == 1 && (sh & 7) == 0 && off >= 0 && (off & 7) == 0 && x0 >= 0 &&
      x0 + sw <= dst.h && dst.stride >= (off >> 3) + (sh >> 3)) {
    for (int r = 0; r < sw; r++) memcpy(dst.buf + (size_t)(x0 + r) * dst.stride + (off >> 3), src.buf + (size_t)r * src.stride, sh >> 3);
    return;
  }
  Bitmap1::blit(dst, x0, y0, src, 0, 0, sw, sh, DRAW_COPY);
}

static void mapNotice(int mx, int my, int mw, int mh, const char* l1, const char* l2) {
  const int bw = mw - 24, bx = mx + 12, by = my + mh / 2 - 30;
  d->fillRect(bx, by, bw, 60, WHT);
  d->drawRect(bx, by, bw, 60, BLK);
  d->drawRect(bx + 1, by + 1, bw - 2, 58, BLK);
  textMid(mx + mw / 2, by + (l2 ? 20 : 30), l1, F12B);
  if (l2) textMid(mx + mw / 2, by + 42, l2, F9);
}

// A round ground distance for the scale bar: 30..90 px long.
static void mapScaleBar(int x, int y, float mPerPx) {
  static const int meters[] = {500, 1000, 2000, 5000, 10000, 20000};
  int m = meters[0];
  for (int c : meters) if (c / mPerPx >= 30 && c / mPerPx <= 90) { m = c; break; }
  const int len = (int)lroundf(m / mPerPx);
  char b[16];
  if (m >= 1000) snprintf(b, sizeof b, "%d km", m / 1000);
  else snprintf(b, sizeof b, "%d m", m);
  d->fillRect(x - len - 8, y - 22, len + 12, 28, WHT);
  d->drawFastHLine(x - len, y, len, BLK);
  d->drawFastVLine(x - len, y - 3, 7, BLK);
  d->drawFastVLine(x, y - 3, 7, BLK);
  text(x - len, y - 7, b, F9);
}

// Ground track (white casing, black core) then the pilot, in the cached window's frame. The window can be a few
// pixels behind the pilot (it is re-rendered only after ~10 px or ~5 degrees), the arrow shows the true spot.
static void mapTrackAndPilot(const ViewModel& vm, int mx, int my, int mw, int mh, float px, float py, float th) {
  const TerrainMap& m = *vm.map;
  const Bitmap1* bm = fbm;
  const float xmin = mx + 2, ymin = my + 2, xmax = mx + mw - 3, ymax = my + mh - 3;
  const int n = m.trackCount();
  float lx = 0, ly = 0;
  bool have = false;
  for (int pass = 0; pass < 2; pass++) {
    have = false;
    for (int i = 0; i <= n; i++) {                                // the points, then the pilot as the last one
      float x, y;
      double la, lo;
      if (i < n) { if (!m.trackAt(i, &la, &lo) || !m.project(la, lo, &x, &y)) { have = false; continue; } }
      else { x = px; y = py; }
      x += mx; y += my;
      if (have) {
        float x0 = lx, y0 = ly, x1 = x, y1 = y;
        if (clipSeg(x0, y0, x1, y1, xmin, ymin, xmax, ymax)) {
          const int ax = (int)lroundf(x0), ay = (int)lroundf(y0), bx = (int)lroundf(x1), by = (int)lroundf(y1);
          const int lw = pass == 0 ? 4 : 2;                       // casing 4 px, core 2 px
          for (int a = -(lw - 1) / 2; a <= lw / 2; a++)
            for (int b = -(lw - 1) / 2; b <= lw / 2; b++) line(*bm, ax + a, ay + b, bx + a, by + b, pass == 0 ? DRAW_SET : DRAW_CLEAR);
        }
      }
      lx = x; ly = y; have = true;
    }
  }
  const int ax = mx + (int)lroundf(px), ay = my + (int)lroundf(py);
  if (ax < mx + 8 || ax > mx + mw - 8 || ay < my + 8 || ay > my + mh - 8) return;
  fillDisc(*bm, ax, ay, 13, DRAW_SET);                            // white badge, so the arrow reads over any relief
  circle(*bm, ax, ay, 13, DRAW_CLEAR);
  drawArrow(ax, ay, th, 9, 7);
}

static void drawMapPage(const ViewModel& vm, const Bitmap1* bm) {
  int mx = THERMAL_X0, my = HDR, mw, mh;
  mapPageSize(W, H, mw, mh);
  thinVario(bm, 3, 9, vm.s.vario, vm.avgVario);
  char b[24], lb[24];
  TerrainMap* m = vm.map;
  if (!bm || !m) { d->drawRect(mx, my, mw, mh, BLK); mapNotice(mx, my, mw, mh, "NO MAP", nullptr); return; }
  if (!vm.s.fix) { d->drawRect(mx, my, mw, mh, BLK); mapNotice(mx, my, mw, mh, "NO FIX", "waiting for GPS"); return; }
  if (!m->valid()) { d->drawRect(mx, my, mw, mh, BLK); mapNotice(mx, my, mw, mh, "NO MAP", "reading the pack"); return; }
  blitMapCache(*bm, mx, my, m->cache());
  d->drawRect(mx, my, mw, mh, BLK);

  float px, py;
  if (!m->project(vm.s.lat, vm.s.lon, &px, &py)) { mapNotice(mx, my, mw, mh, "NO MAP", nullptr); return; }
  const float hdgMap = m->headingDeg();                            // the cached window's "up", degrees
  const float th = isfinite(vm.heading) ? (vm.heading - hdgMap) * PI_F / 180.0f : 0.0f;
  mapTrackAndPilot(vm, mx, my, mw, mh, px, py, th);

  // north marker (top left) and wind arrow (top right), both in the map's frame
  const float nth = -hdgMap * PI_F / 180.0f;
  const int nax = mx + 26, nay = my + 28;
  d->fillRect(mx + 2, my + 2, 46, 50, WHT);
  drawArrow(nax, nay, nth, 11);
  textMid(nax + (int)lroundf(21 * sinf(nth)), nay - (int)lroundf(21 * cosf(nth)), "N", F9);
  if (vm.wind.valid) {
    d->fillRect(mx + mw - 42, my + 2, 40, 38, WHT);
    drawArrow(mx + mw - 22, my + 21, vm.wind.toDeg * PI_F / 180.0f - hdgMap * PI_F / 180.0f, 14);
  }

  // altitude box (bottom left) and scale bar (bottom right)
  const int by = my + mh;
  d->fillRect(mx + 2, by - 46, 112, 44, WHT);
  d->drawFastHLine(mx + 2, by - 47, 112, BLK);
  d->drawFastVLine(mx + 113, by - 47, 46, BLK);
  snprintf(lb, sizeof lb, "AGL %s", fnum(b, sizeof b, vm.agl, "%.0f"));
  text(mx + 6, by - 24, lb, F12B);
  snprintf(lb, sizeof lb, "alt %s", fnum(b, sizeof b, vm.s.alt, "%.0f"));
  text(mx + 6, by - 7, lb, F9);
  mapScaleBar(mx + mw - 8, by - 8, m->mPerPx());

  if (m->noData()) mapNotice(mx, my, mw, mh, "OUTSIDE MAP", "no terrain data here");
  if (vm.mapAbout) {                                               // first seconds on the page: attribution, caveat
    const int bx = mx + 6, bw = mw - 12, ty = my + 62;
    d->fillRect(bx, ty, bw, 84, WHT);
    d->drawRect(bx, ty, bw, 84, BLK);
    text(bx + 6, ty + 17, "(c) OpenStreetMap", F9);
    text(bx + 6, ty + 34, "Copernicus DEM", F9);
    text(bx + 6, ty + 51, "AGL: terrain model,", F9);
    text(bx + 6, ty + 68, "+-10 m, 80 at peaks", F9);
  }
}

// ---- CENTRING PAGE -------------------------------------------------------------
// The thermal assistant for the circle you are flying (core/thermal3d.h, CoreCue). Heading up, the centre of your
// newest turn in the middle of the map:
//   top      which way and how far to shift the circle (arrow, distance, word) or CENTRED
//   map      the last turns in the air-mass frame, thick where it climbed (the thick part is the lift), a ring
//            of 25 m, the core marker, the shift as a line from your centre to the core, you
//   bottom   climb now, average and best on this turn, and a small lean readout
//   right    strength by height, a strip, current altitude at the top
// It needs one full turn of circling; before that, or after leaving the lift, it says so instead of guessing.

// A pixel, a line w pixels wide (parallel lines: no gaps on diagonals), a dotted line.
static void cPix(int x, int y, uint16_t col) {
  if (fbm) fbm->pixel(x, y, col ? DRAW_SET : DRAW_CLEAR);
  else d->drawPixel(x, y, col);
}
static void cLine(int x0, int y0, int x1, int y1, int w, uint16_t col) {
  if (w <= 1) { gLine(x0, y0, x1, y1, col); return; }
  const bool flat = abs(x1 - x0) >= abs(y1 - y0);
  for (int k = 0; k < w; k++) {
    const int o = k - (w - 1) / 2;
    if (flat) gLine(x0, y0 + o, x1, y1 + o, col);
    else gLine(x0 + o, y0, x1 + o, y1, col);
  }
}
// on/off pixels along the segment (period on + off), the phase carried across segments in dashPhase
static void cDots(int x0, int y0, int x1, int y1, int on, int off, uint16_t col) {
  const int dx = x1 - x0, dy = y1 - y0;
  int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  if (steps < 1) steps = 1;
  for (int i = 0; i <= steps; i++) {
    if ((dashPhase++ % (on + off)) >= on) continue;
    cPix(x0 + (int)lroundf((float)dx * i / steps), y0 + (int)lroundf((float)dy * i / steps), col);
  }
}
static void cDisc(int cx, int cy, int r, uint16_t col) {
  if (fbm) fillDisc(*fbm, cx, cy, r, col ? DRAW_SET : DRAW_CLEAR);
  else d->fillCircle(cx, cy, r, col);
}
// stroke width by climb: sink is a dotted line, strong lift a thick one
static int cWidth(float v) { return v < 0.3f ? 1 : (v < 1.0f ? 2 : (v < 2.0f ? 3 : (v < 3.0f ? 4 : 5))); }
static void cTrackSeg(int x0, int y0, int x1, int y1, float v, bool older) {
  if (v < 0.0f) { cDots(x0, y0, x1, y1, 1, 2, BLK); return; }
  int w = cWidth(v);
  if (older) {                                  // the turn before: a grey stroke, thinner
    if (fbm) greyLine(*fbm, x0, y0, x1, y1, w > 1 ? w - 1 : 1, MID + 1);
    else cDots(x0, y0, x1, y1, 1, 1, BLK);
    return;
  }
  cLine(x0, y0, x1, y1, w, BLK);
}

// Strength by altitude as horizontal bars growing right from x0, one per band, to scale (6.5 px per m/s), altitude
// along y at `pxPerM` with zRef at yRef.
static void climbBars(const ThermalColumn& c, int x0, int ylo, int yhi, float zRef, int yRef, float pxPerM) {
  for (int i = 0; i < c.nBands; i++) {
    const ClimbBand& bd = c.bands[i];
    const int yb = yRef + (int)lroundf((zRef - (bd.z0 + c.bandM)) * pxPerM), yt = yRef + (int)lroundf((zRef - bd.z0) * pxPerM);
    int h = yt - yb - 1;
    if (h < 2) h = 2;
    if (yb < ylo || yb + h > yhi) continue;
    int len = (int)lroundf(fminf(fmaxf(bd.climb, 0.0f), 4.5f) * 6.5f);
    if (bd.climb < 0.15f) { d->drawFastHLine(x0 + 1, yb + h / 2, 3, BLK); continue; }
    if (len < 2) len = 2;
    d->fillRect(x0 + 1, yb, len, h, BLK);
  }
}

static const char* shiftWord(float relDeg) {          // relDeg: clockwise from ahead
  static const char* w[] = {"AHEAD", "AHEAD-R", "RIGHT", "BACK-R", "BEHIND", "BACK-L", "LEFT", "AHEAD-L"};
  return w[((int)((relDeg + 22.5f) / 45.0f)) & 7];
}

// the zoom steps (pixels per metre): the picture does not breathe, it jumps when a step no longer fits
static float centerPickScale(float extentM, float halfPx) {
  static const float ladder[] = {3.0f, 2.2f, 1.6f, 1.2f, 0.9f, 0.65f, 0.45f};
  for (float k : ladder) if (extentM * k <= halfPx) return k;
  return 0.45f;
}

static const int CEN_CUE_H = 62, CEN_BOTTOM_H = 78, CEN_STRIP_W = 34;
static const float CEN_OK_M = 12.0f;                   // a shift shorter than this is "centred"

static void drawCenterPage(const ViewModel& vm, const Bitmap1* bm) {
  int mx = THERMAL_X0, my = HDR, mw, mh;
  mapPageSize(W, H, mw, mh);
  thinVario(bm, 3, 9, vm.s.vario, vm.avgVario);
  const ThermalColumn* c = vm.col;
  const bool have = c && c->valid && c->circling && c->cue.valid;
  const CoreCue* q = have ? &c->cue : nullptr;
  char b[32], l[40];
  d->drawRect(mx, my, mw, mh, BLK);

  // ---- the point of view: up = the direction you fly in the air-mass frame (the last 3 s of the track)
  float hdg = 0;                                        // radians clockwise from north
  int n = 0;
  float headX = 0, headY = 0;
  if (have) {
    n = c->count();
    headX = c->headValid() ? c->headX() : c->px(n - 1);
    headY = c->headValid() ? c->headY() : c->py(n - 1);
    const int k = n > 4 ? n - 4 : 0;
    hdg = atan2f(headX - c->px(k), headY - c->py(k));
  }
  const float sh = sinf(hdg), ch = cosf(hdg);
  auto right = [&](float e, float nn) { return e * ch - nn * sh; };      // metres to the right of your heading
  auto ahead = [&](float e, float nn) { return e * sh + nn * ch; };

  // ---- top: the shift
  d->drawFastHLine(mx, my + CEN_CUE_H, mw, BLK);
  float relDeg = NAN;
  if (q) relDeg = atan2f(right(q->shiftX, q->shiftY), ahead(q->shiftX, q->shiftY)) * 57.29578f;
  const int ax = mx + 33, ay = my + 31;
  if (q && q->shiftM >= CEN_OK_M) {
    float rel = relDeg;
    if (rel < 0) rel += 360.0f;
    drawArrow(ax, ay, relDeg * PI_F / 180.0f, 22, 13);
    text(mx + 68, my + 40, fnum(b, sizeof b, q->shiftM, "%.0f"), F24B);
    text(mx + 68 + textW(b, F24B) + 5, my + 40, "m", F12B);
    text(mx + 68, my + 58, shiftWord(rel), F12B);
  } else if (q) {
    gCircle(ax, ay, 18, BLK); gCircle(ax, ay, 17, BLK);
    d->fillCircle(ax, ay, 6, BLK);
    text(mx + 68, my + 40, "CENTRED", F18B);
    text(mx + 68, my + 58, "hold the circle", F9);
  } else {
    text(ax, ay + 12, "--", F24B, AC);
    const char* why = !c || !c->valid ? "no circles yet" : (!c->circling ? "not circling" : "first full turn");
    text(mx + 68, my + 40, why, F12B);
    if (c && c->valid && !c->circling) { snprintf(l, sizeof l, "left %u s ago", (unsigned)c->ageS); text(mx + 68, my + 58, l, F9); }
    else if (c && c->valid) text(mx + 68, my + 58, "needs one turn", F9);
    else text(mx + 68, my + 58, "turn to start", F9);
  }

  // ---- bottom: climb now / average / best, lean
  const int by0 = my + mh - CEN_BOTTOM_H;
  d->drawFastHLine(mx, by0, mw, BLK);
  text(mx + 6, by0 + 14, "NOW m/s", F9);
  text(mx + 6, by0 + 46, fnum(b, sizeof b, vm.s.vario, "%+.1f"), F24B);
  text(mx + 104, by0 + 14, "turn avg", F9);
  text(mx + mw - 6, by0 + 14, fnum(b, sizeof b, q ? q->meanClimb : NAN, "%+.1f"), F12B, AR);
  text(mx + 104, by0 + 33, "best", F9);
  text(mx + mw - 6, by0 + 33, fnum(b, sizeof b, q ? q->bestClimb : NAN, "%+.1f"), F12B, AR);
  d->drawFastHLine(mx, by0 + 53, mw, BLK);
  // lean: the way the lift moves as it goes up, relative to your heading, and by how much
  if (c && c->valid && c->leanValid) {
    const float lr = atan2f(right(sinf(c->leanDeg * PI_F / 180.0f), cosf(c->leanDeg * PI_F / 180.0f)),
                            ahead(sinf(c->leanDeg * PI_F / 180.0f), cosf(c->leanDeg * PI_F / 180.0f)));
    if (have) drawArrow(mx + 16, by0 + 66, lr, 6, 5);
    snprintf(l, sizeof l, "lean %.0f m/100 m %s", c->leanPer100, cardinal16(c->leanDeg));
  } else {
    snprintf(l, sizeof l, "lean --");
  }
  text(mx + 30, by0 + 71, l, F9);
  {
    const char* tr = "";
    if (c && c->valid) tr = c->trend == TREND_BUILDING ? "BUILDING" : c->trend == TREND_STEADY ? "STEADY" : c->trend == TREND_WEAKENING ? "FADING" : c->trend == TREND_TOP ? "TOP" : "";
    text(mx + mw - 6, by0 + 71, tr, F9B, AR);
  }

  // ---- map frame
  const int mapX0 = mx + 1, mapX1 = mx + mw - CEN_STRIP_W - 2, mapY0 = my + CEN_CUE_H + 2, mapY1 = by0 - 2;
  const int ox = (mapX0 + mapX1) / 2, oy = (mapY0 + mapY1) / 2;
  d->drawFastVLine(mapX1 + 1, mapY0, mapY1 - mapY0 + 1, BLK);
  if (!q) {
    text(ox, oy + 6, "centre of the circle", F9, AC);
    text(ox, oy + 24, "and the lift appear here", F9, AC);
    return;
  }

  // scale: the newest three turns and the core must fit
  float ext = fminf(q->shiftM, 80.0f) + 10.0f;
  for (int i = q->a2; i < n; i++) ext = fmaxf(ext, hypotf(c->px(i) - q->cx, c->py(i) - q->cy) + 6.0f);
  const float k = centerPickScale(ext, 0.5f * fminf((float)(mapX1 - mapX0), (float)(mapY1 - mapY0)) - 6.0f);
  auto SX = [&](float e, float nn) { return (int)lroundf(ox + right(e - q->cx, nn - q->cy) * k); };
  auto SY = [&](float e, float nn) { return (int)lroundf(oy - ahead(e - q->cx, nn - q->cy) * k); };
  const int clx0 = mapX0 + 2, cly0 = mapY0 + 2, clx1 = mapX1 - 2, cly1 = mapY1 - 2;
  auto inside = [&](int x, int y) { return x >= clx0 && x <= clx1 && y >= cly0 && y <= cly1; };

  // 25 m reference ring (dotted) around your centre
  PROF_BEGIN(P_CLIMB);
  {
    dashPhase = 0;
    int px = 0, py = 0;
    for (int j = 0; j <= 36; j++) {
      const float th = j * (2 * PI_F / 36);
      const int x = ox + (int)lroundf(25.0f * k * sinf(th)), y = oy - (int)lroundf(25.0f * k * cosf(th));
      if (j && inside(x, y) && inside(px, py)) cDots(px, py, x, y, 1, 3, BLK);
      px = x; py = y;
    }
    text(ox + 3, oy - (int)lroundf(25.0f * k) + 12, "25 m", F9);
  }

  // turns: the one before the newest in grey, then the newest thick where it climbed (back to front)
  for (int turn = q->turns >= 2 ? 1 : 0; turn >= 0; turn--) {
    const int from = turn == 1 ? q->a1 : q->a0, to = turn == 1 ? q->a0 : n - 1;
    dashPhase = 0;
    for (int i = from + 1; i <= to; i++) {
      const int x0 = SX(c->px(i - 1), c->py(i - 1)), y0 = SY(c->px(i - 1), c->py(i - 1));
      const int x1 = SX(c->px(i), c->py(i)), y1 = SY(c->px(i), c->py(i));
      if (!inside(x0, y0) || !inside(x1, y1)) continue;
      cTrackSeg(x0, y0, x1, y1, 0.5f * (c->pv(i - 1) + c->pv(i)), turn == 1);
    }
  }
  if (c->headValid()) {                                  // the newest 3 Hz sample: the last bit of the line
    const int x0 = SX(c->px(n - 1), c->py(n - 1)), y0 = SY(c->px(n - 1), c->py(n - 1));
    const int x1 = SX(headX, headY), y1 = SY(headX, headY);
    if (inside(x0, y0) && inside(x1, y1)) cTrackSeg(x0, y0, x1, y1, c->headV(), false);
  }
  PROF_END(P_CLIMB);

  // best lift on this turn: a bead on the line
  {
    const int bx = SX(q->bestX, q->bestY), byy = SY(q->bestX, q->bestY);
    if (inside(bx, byy)) { cDisc(bx, byy, 7, WHT); cDisc(bx, byy, 5, BLK); cDisc(bx, byy, 2, WHT); }
  }
  // your centre, the core, and the shift between them
  d->drawFastHLine(ox - 5, oy, 11, BLK);
  d->drawFastVLine(ox, oy - 5, 11, BLK);
  {
    const int cx = SX(q->x, q->y), cy = SY(q->x, q->y);
    if (q->shiftM >= CEN_OK_M && hypotf((float)(cx - ox), (float)(cy - oy)) > 22) {
      const float len = hypotf((float)(cx - ox), (float)(cy - oy));
      const float ux = (cx - ox) / len, uy = (cy - oy) / len;
      cLine(ox + (int)(ux * 7), oy + (int)(uy * 7), cx - (int)(ux * 14), cy - (int)(uy * 14), 3, BLK);
      const int hx = cx - (int)(ux * 13), hy = cy - (int)(uy * 13);
      d->fillTriangle(cx - (int)(ux * 9), cy - (int)(uy * 9), hx - (int)(uy * 7), hy + (int)(ux * 7), hx + (int)(uy * 7), hy - (int)(ux * 7), BLK);
    }
    if (inside(cx, cy)) {                                // the core: a target
      cDisc(cx, cy, 13, WHT);
      gCircle(cx, cy, 12, BLK); gCircle(cx, cy, 11, BLK); gCircle(cx, cy, 10, BLK);
      d->drawFastHLine(cx - 17, cy, 35, BLK); d->drawFastVLine(cx, cy - 17, 35, BLK);
      d->drawFastHLine(cx - 17, cy + 1, 35, BLK); d->drawFastVLine(cx + 1, cy - 17, 35, BLK);
      cDisc(cx, cy, 5, BLK);
    }
  }
  // you, on the circle
  {
    const int hx = SX(headX, headY), hy = SY(headX, headY);
    const float lx = hx - ox, ly = hy - oy;
    if (inside(hx, hy)) {
      d->fillTriangle(hx, hy - 9, hx - 7, hy + 7, hx + 7, hy + 7, WHT);
      d->fillTriangle(hx, hy - 7, hx - 5, hy + 6, hx + 5, hy + 6, BLK);
      d->fillTriangle(hx, hy - 2, hx - 2, hy + 3, hx + 2, hy + 3, WHT);
    }
    (void)lx; (void)ly;
  }
  // wind in the map's frame (top right of the map), and "ahead" is up
  if (vm.wind.valid) {
    d->fillRect(mapX1 - 34, mapY0 + 1, 33, 31, WHT);
    drawArrow(mapX1 - 18, mapY0 + 17, vm.wind.toDeg * PI_F / 180.0f - hdg, 10, 7);
  }

  // ---- strength by height: bars, the current altitude at the top
  {
    const int sx0 = mx + mw - CEN_STRIP_W;
    const int ytop = mapY0, ybot = by0 - 2;
    const float zNow = c->headValid() ? c->headZ() : c->pz(n - 1);
    const float pxPerM = 1.0f;
    const int yRef = ytop + 22;
    d->drawFastVLine(sx0, ytop, ybot - ytop + 1, BLK);
    climbBars(*c, sx0, ytop, ybot, zNow, yRef, pxPerM);
    d->fillTriangle(sx0 + 1, yRef, sx0 + 9, yRef - 5, sx0 + 9, yRef + 5, BLK);          // you
    snprintf(b, sizeof b, "%.0f", zNow);
    text(mx + mw - 3, ytop + 12, b, F9, AR);
    d->drawFastHLine(sx0, yRef, 3, BLK);
    for (int z = (int)floorf((zNow - (ybot - yRef) / pxPerM) / 50.0f) * 50; z <= zNow; z += 50) {
      const int y = yRef + (int)lroundf((zNow - z) * pxPerM);
      if (y > ytop + 30 && y < ybot - 2) d->drawFastHLine(sx0 + 1, y, 3, BLK);
    }
    text(sx0 + 3, ybot - 3, "m/s", F9);
  }
}

// Portrait layout, 240 x 400. Priorities: GPS speed + glide, altitude + vertical speed, compass + wind.
// Cells have a short inverted title band like the top toolbar.
static void cell(int x, int y, int w, const char* title, const char* value) {
  d->fillRect(x, y, w, 17, BLK);
  text(x + 5, y + 14, title, F9, AL, WHT);
  text(x + 4, y + 58, value, F24B);
}

static void drawLiveBody(const ViewModel& vm, const Bitmap1* bm) {
  const Sample& v = vm.s;
  char b[24];
  const int barX = 3, barW = 9;              // thin vario on the left edge
  const int cx0 = THERMAL_X0, cellW = 107;   // two cells to the right of it
  const int row1 = HDR + 2, row2 = HDR + 66, gridEnd = HDR + 130;

  // no GPS fix: speed and track read 0 (the title says NO FIX) instead of dashes
  float spd = isfinite(v.speedKmh) ? v.speedKmh : 0;
  float crs = isfinite(vm.heading) ? vm.heading : 0;
  PROF_BEGIN(P_VARIO);
  thinVario(bm, barX, barW, v.vario, vm.avgVario);
  PROF_END(P_VARIO);
  PROF_BEGIN(P_CELLS);
  cell(cx0, row1, cellW, v.fix ? "GPS km/h" : "NO FIX", fnum(b, sizeof b, spd, "%.0f"));
  cell(cx0 + cellW + 4, row1, cellW, "GLIDE", fnum(b, sizeof b, vm.glide, "%.1f"));
  cell(cx0, row2, cellW, "ALT m", fnum(b, sizeof b, v.alt, "%.0f"));
  cell(cx0 + cellW + 4, row2, cellW, "VS m/s", fnum(b, sizeof b, v.vario, "%+.1f"));
  PROF_END(P_CELLS);

  // compass with the wind arrow in a corner
  const Wind& w = vm.wind;
  const int R = 92, cx = cx0 + (W - cx0) / 2, cy = gridEnd + 8 + R + 6;
  int mapW, pageH;
  thermalMapSize(W, H, mapW, pageH);
  if (vm.page == ViewModel::PAGE_THERMAL) {
    PROF_BEGIN(P_THERMAL);
    drawThermalMap(vm, bm, cx0, gridEnd + 2, mapW, pageH);
    PROF_END(P_THERMAL);
  } else if (vm.page == ViewModel::PAGE_PROFILE) {
    PROF_BEGIN(P_WPROFILE);
    drawWindProfile(vm, bm, cx0, gridEnd + 2, mapW, pageH);
    PROF_END(P_WPROFILE);
  } else if (vm.page == ViewModel::PAGE_HOME) {
    PROF_BEGIN(P_HOME);
    drawHome(vm, cx0, gridEnd + 2, mapW, pageH);
    PROF_END(P_HOME);
  } else {
    PROF_BEGIN(P_DIAL);
    PROF_BEGIN(P_RING);
    if (bm) greyRing(*bm, cx, cy, R - 1, R - 12, LIGHT);   // before any GFX call: it overwrites what is under it
    PROF_END(P_RING);
    compassDial(cx, cy, R, vm.heading, vm.headingSrc);
    PROF_END(P_DIAL);
    if (vm.headingSrc == HDG_GPS) {
      PROF_BEGIN(P_WCORNER);
      windCorner(cx, cy, R, crs, w);
      PROF_END(P_WCORNER);
    }
  }

  // wind as one line: "15 km/h NNW"
  PROF_BEGIN(P_WLINE);
  const int ly = H - 6;
  if (w.valid) {
    float from = fmodf(w.toDeg + 180.0f, 360.0f);
    fnum(b, sizeof b, w.speedKmh, "%.0f");
    int x = cx0;
    text(x, ly, b, F18B);
    x += textW(b, F18B) + 5;
    // With terrain the right end of the row is the AGL tile; a stale wind then says "old" in place of the unit.
    const bool old = w.ageS > 300;
    const char* unit = vm.terrainOn && old ? "old" : "km/h";
    text(x, ly, unit, F12B);
    x += textW(unit, F12B) + 10;
    text(x, ly, cardinal16(from), F18B);
    if (old && !vm.terrainOn) text(W - 6, ly, "old", F9, AR);
  } else {
    text(cx0, ly, "0", F18B);
    text(cx0 + textW("0", F18B) + 5, ly, "km/h", F12B);
  }
  PROF_END(P_WLINE);
  if (vm.terrainOn) {          // height above the terrain model (not the baro altitude): bottom right, label above the number
    char a[16];
    text(W - 6, ly - 20, "AGL", F9, AR);
    text(W - 6, ly, fnum(a, sizeof a, vm.agl, "%.0f"), F12B, AR);
  }
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

void drawLive(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs, const Bitmap1* bm) {
  begin(g, bm);
  PROF_BEGIN(P_LIVE);
  PROF_BEGIN(P_HEADER);
  header(vm);
  PROF_END(P_HEADER);
  if (vm.fresh && vm.page == ViewModel::PAGE_MAP && vm.terrainOn) drawMapPage(vm, bm);
  else if (vm.fresh && vm.page == ViewModel::PAGE_CENTER) drawCenterPage(vm, bm);
  else if (vm.fresh) drawLiveBody(vm, bm);
  else drawStandalone(vm, bm);
  powerBanner(heldMs);
  PROF_END(P_LIVE);
}

void drawList(Adafruit_GFX& g, const ListModel& lm, const ViewModel& vm, uint32_t heldMs, const Bitmap1* bm) {
  begin(g, bm);
  header(vm, lm.tools ? "COMPASS" : "SELECT VARIO");
  char b[40];
  snprintf(b, sizeof b, "filter: %.16s", lm.filter);
  if (!lm.tools) text(6, HDR + 20, b, F9);
  const int n = lm.count, sel = lm.selected;
  const int rowH = 42, y0 = HDR + 30;
  const int extra = lm.homeRow ? 1 : 0;
  int rows = n + 1 + extra;
  int vis = (H - y0 - 30) / rowH;
  int first = sel >= vis ? sel - vis + 1 : 0;   // scroll so the selection stays visible
  for (int k = 0; k < vis && first + k < rows; k++) {
    int i = first + k, y = y0 + k * rowH;
    bool on = (i == sel);
    if (on) d->fillRect(0, y, W, rowH - 2, BLK);
    uint16_t col = on ? WHT : BLK;
    if (i == 0) {
      text(10, y + 27, "< back", F12B, AL, col);
    } else if (lm.homeRow && i == 1) {
      text(10, y + 27, "Home", F12B, AL, col);
    } else {
      const ListItem& it = lm.items[i - 1 - extra];
      char nm[20];
      snprintf(nm, sizeof nm, "%.17s", it.name);
      text(10, y + 18, nm, F12B, AL, col);
      snprintf(b, sizeof b, "%d dBm   %s", it.rssi, it.id);
      text(10, y + 36, b, F9, AL, col);
    }
  }
  if (n == 0 && !lm.tools) text(W / 2, y0 + (2 + extra) * rowH, "scanning...", F12B, AC);
  text(W / 2, H - 12, "press: next   hold: select", F9, AC);
  powerBanner(heldMs);
}

void drawMessage(Adafruit_GFX& g, const char* l1, const char* l2, const Bitmap1* bm) {
  begin(g, bm);
  text(W / 2, H / 2 - (l2 ? 8 : 0), l1, F18B, AC);
  if (l2) text(W / 2, H / 2 + 28, l2, F12B, AC);
}
