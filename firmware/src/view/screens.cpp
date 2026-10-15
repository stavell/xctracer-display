#include "view/screens.h"
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
static const uint16_t BLK = 0, WHT = 1;
static const float PI_F = 3.14159265f;

#define F9   (&FreeSans9pt7b)
#define F9B  (&FreeSansBold9pt7b)
#define F12B (&FreeSansBold12pt7b)
#define F18B (&FreeSansBold18pt7b)
#define F24B (&FreeSansBold24pt7b)

static Adafruit_GFX* d;

static void begin(Adafruit_GFX& g) {
  d = &g;
  W = g.width();
  H = g.height();
}

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

// Centre a string on (cx, cy) using its real glyph bounds, so letters sit in the middle of boxes.
static void textMid(int cx, int cy, const char* s, const GFXfont* f, uint16_t col = BLK) {
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
      textMid(lx, ly, "N", F12B, WHT);
    } else if (i % 2 == 0) {
      textMid(lx, ly, L[i], F12B);
    } else {
      textMid(lx, ly, L[i], F9);
    }
  }
  char b[8];
  text(cx, cy + 8, fnum(b, sizeof b, course, "%.0f"), F18B, AC);
}

// Arrow centred on (ax, ay) pointing along screen angle th (radians clockwise from up), half length L.
static void drawArrow(int ax, int ay, float th, int L) {
  float ux = sinf(th), uy = -cosf(th), px = -uy, py = ux;
  int tx = ax - L * ux, ty = ay - L * uy;                  // tail
  int hx = ax + 3 * ux, hy = ay + 3 * uy;                  // base of the head
  int ex = ax + L * ux, ey = ay + L * uy;                  // tip
  for (int k = -1; k <= 1; k++) d->drawLine(tx + k * px, ty + k * py, hx + k * px, hy + k * py, BLK);
  d->fillTriangle(ex, ey, hx + 9 * px, hy + 9 * py, hx - 9 * px, hy - 9 * py, BLK);
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

static void drawStatus(const ViewModel& vm) {
  const char* title = vm.link == ViewModel::LINK_CONNECTED ? "CONNECTED"
                      : vm.link == ViewModel::LINK_CONNECTING ? "CONNECTING"
                      : vm.link == ViewModel::LINK_SEARCHING ? "SEARCHING" : "NO VARIO";
  int cx = W / 2;
  text(cx, 130, title, F18B, AC);
  char b[48];
  if (vm.hasSaved) snprintf(b, sizeof b, "%.18s", vm.device);
  else snprintf(b, sizeof b, "filter: %.12s", vm.filter);
  text(cx, 162, b, F12B, AC);
  if (vm.link == ViewModel::LINK_CONNECTED) {
    text(cx, 200, "waiting for data...", F9, AC);
    snprintf(b, sizeof b, "%lu ok, %lu bad", (unsigned long)vm.linesOk, (unsigned long)vm.linesBad);
    text(cx, 220, b, F9, AC);
  }
  text(cx, H - 12, "hold: choose device", F9, AC);
}

static int dashPhase = 0;

// Solid w x w squares stamped along the segment (no gaps); optionally dashed for the older memory.
static void thickLine(int x0, int y0, int x1, int y1, int w, bool dashed) {
  if (w <= 1 && !dashed) { d->drawLine(x0, y0, x1, y1, BLK); return; }
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

static float gRR[ViewModel::TRAIL_MAX], gUU[ViewModel::TRAIL_MAX];   // static: far too big for the 4 KB loop stack

static float gMr[ViewModel::MARKS_MAX], gMu[ViewModel::MARKS_MAX];

// Thermal assistant. Heading-up map: the last 3 minutes of track in the air-mass frame (shifted by the
// wind drift; the last 90 s solid and thicker where you were climbing, older track dashed) plus markers
// for the thermals you remember, each with its average climb. The newest marker, the thermal you last
// worked, also gets an arrow at the frame edge with the distance when it is off the map, and the zoom
// stays wide enough to show it. Fixed zoom levels, so the picture does not breathe.
static void drawThermalMap(const ViewModel& vm, int mx, int my, int mw, int mh) {
  d->drawRect(mx, my, mw, mh, BLK);
  const int n = vm.trailCount;
  if (n < 2) { text(mx + mw / 2, my + mh / 2 + 6, "no GPS track", F12B, AC); return; }

  // direction of travel in the air frame: the last ~3 s of the trace (GPS course if barely moving)
  float hdg = 0;
  if (vm.mapHeadingUp) {
    const AirPoint& a = vm.trail[n >= 4 ? n - 4 : 0];
    float dx = vm.trail[n - 1].x - a.x, dy = vm.trail[n - 1].y - a.y;
    if (hypotf(dx, dy) > 3.0f) hdg = atan2f(dx, dy);
    else if (isfinite(vm.s.course)) hdg = vm.s.course * PI_F / 180.0f;
  }
  const float ct = cosf(hdg), st = sinf(hdg);
  const int ox = mx + mw / 2, oy = my + mh / 2;                 // you, in the centre

  for (int i = 0; i < n; i++) {                                 // screen offsets in metres: r right, u up
    gRR[i] = vm.trail[i].x * ct - vm.trail[i].y * st;
    gUU[i] = vm.trail[i].y * ct + vm.trail[i].x * st;
  }
  const int nm = vm.markCount;
  for (int i = 0; i < nm; i++) {
    gMr[i] = vm.marks[i].x * ct - vm.marks[i].y * st;
    gMu[i] = vm.marks[i].y * ct + vm.marks[i].x * st;
  }

  // zoom: fit the last 30 s, plus the newest thermal when it is within 800 m
  float smax = 1e9f;
  const int margin = 14;
  auto fit = [&](float r, float u) {
    if (fabsf(r) > 1) smax = fminf(smax, (mw / 2 - margin) / fabsf(r));
    if (fabsf(u) > 1) smax = fminf(smax, (mh / 2 - margin) / fabsf(u));
  };
  for (int i = n > 30 ? n - 30 : 0; i < n; i++) fit(gRR[i], gUU[i]);
  if (nm > 0 && hypotf(gMr[0], gMu[0]) <= 800) fit(gMr[0], gMu[0]);
  static const float zoom[] = {2.2f, 1.6f, 1.1f, 0.7f, 0.45f, 0.28f, 0.18f, 0.11f};
  float s = zoom[7];
  for (float z : zoom) if (z <= smax) { s = z; break; }

  // trail, oldest first, clipped to the frame; anything older than 90 s is dashed
  dashPhase = 0;
  const float cx0 = mx + 4, cy0 = my + 4, cx1 = mx + mw - 4, cy1 = my + mh - 4;
  for (int i = 1; i < n; i++) {
    float x0 = ox + gRR[i - 1] * s, y0 = oy - gUU[i - 1] * s, x1 = ox + gRR[i] * s, y1 = oy - gUU[i] * s;
    if (!clipSeg(x0, y0, x1, y1, cx0, cy0, cx1, cy1)) continue;
    thickLine((int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1), (int)lroundf(y1),
              climbWidth(vm.trail[i].vario), (n - 1 - i) > 90);
  }

  // remembered thermals: a fixed-size ring (no fill, the trail stays visible), the newest one heavier
  char lb[16];
  static int gSx[ViewModel::MARKS_MAX], gSy[ViewModel::MARKS_MAX];
  static bool gIn[ViewModel::MARKS_MAX];
  for (int i = nm - 1; i >= 0; i--) {
    float tx = ox + gMr[i] * s, ty = oy - gMu[i] * s;
    gSx[i] = (int)lroundf(tx);
    gSy[i] = (int)lroundf(ty);
    gIn[i] = tx > mx + 14 && tx < mx + mw - 14 && ty > my + 14 && ty < my + mh - 14;
    if (gIn[i]) {
      int rad = i == 0 ? 10 : 7;
      d->drawCircle(gSx[i], gSy[i], rad, BLK);
      if (i == 0) { d->drawCircle(gSx[i], gSy[i], rad - 1, BLK); d->fillCircle(gSx[i], gSy[i], 3, BLK); }
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
        float dist = hypotf(vm.marks[0].x, vm.marks[0].y);
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

  // you: a small arrow pointing up
  d->fillTriangle(ox, oy - 9, ox - 6, oy + 6, ox + 6, oy + 6, BLK);
  d->fillTriangle(ox, oy - 3, ox - 2, oy + 3, ox + 2, oy + 3, WHT);

  // north marker (top-left) and wind arrow (top-right), both in the map's frame
  float nth = -hdg;                                              // screen direction of north
  int nax = mx + 28, nay = my + 30;
  drawArrow(nax, nay, nth, 11);
  textMid(nax + (int)lroundf(21 * sinf(nth)), nay - (int)lroundf(21 * cosf(nth)), "N", F9);
  if (vm.wind.valid) drawArrow(mx + mw - 22, my + 24, vm.wind.toDeg * PI_F / 180.0f - hdg, 14);

  static const int bars[] = {10, 20, 50, 100, 200, 500};
  int bm = bars[0];
  for (int bsel : bars) if (bsel * s >= 30 && bsel * s <= 90) { bm = bsel; break; }
  int bl = (int)lroundf(bm * s), by = my + mh - 8;
  d->fillRect(mx + 5, by - 19, bl + 6, 25, WHT);                 // keep the scale bar readable over the trail
  d->drawFastHLine(mx + 8, by, bl, BLK);
  d->drawFastVLine(mx + 8, by - 3, 7, BLK);
  d->drawFastVLine(mx + 8 + bl, by - 3, 7, BLK);
  snprintf(lb, sizeof lb, "%d m", bm);
  text(mx + 8, by - 7, lb, F9);
}

// Portrait layout, 240 x 400. Priorities: GPS speed + glide, altitude + vertical speed, compass + wind.
// Cells have a short inverted title band like the top toolbar.
static void cell(int x, int y, int w, const char* title, const char* value) {
  d->fillRect(x, y, w, 17, BLK);
  text(x + 5, y + 14, title, F9, AL, WHT);
  text(x + 4, y + 58, value, F24B);
}

static void drawLiveBody(const ViewModel& vm) {
  const Sample& v = vm.s;
  char b[24];
  const int barX = 3, barW = 9;              // thin vario on the left edge
  const int cx0 = 22, cellW = 107;           // two cells to the right of it
  const int row1 = HDR + 2, row2 = HDR + 66, gridEnd = HDR + 130;

  // no GPS fix: speed and track read 0 (the title says NO FIX) instead of dashes
  float spd = isfinite(v.speedKmh) ? v.speedKmh : 0;
  float crs = isfinite(v.course) ? v.course : 0;
  thinVario(barX, barW, v.vario, vm.avgVario);
  cell(cx0, row1, cellW, v.fix ? "GPS km/h" : "NO FIX", fnum(b, sizeof b, spd, "%.0f"));
  cell(cx0 + cellW + 4, row1, cellW, "GLIDE", fnum(b, sizeof b, vm.glide, "%.1f"));
  cell(cx0, row2, cellW, "ALT m", fnum(b, sizeof b, v.alt, "%.0f"));
  cell(cx0 + cellW + 4, row2, cellW, "VS m/s", fnum(b, sizeof b, v.vario, "%+.1f"));

  // compass with the wind arrow in a corner
  const Wind& w = vm.wind;
  const int R = 92, cx = cx0 + (W - cx0) / 2, cy = gridEnd + 8 + R + 6;
  if (vm.page == ViewModel::PAGE_THERMAL) {
    drawThermalMap(vm, cx0, gridEnd + 2, W - cx0 - 2, H - 6 - 25 - 5 - (gridEnd + 2));
  } else {
    compassDial(cx, cy, R, crs);
    windCorner(cx, cy, R, crs, w);
  }

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

static void powerBanner(uint32_t heldMs) {
  if (heldMs < 1500) return;
  int bw = W - 24, bx = 12, by = H / 2 - 36;
  d->fillRect(bx, by, bw, 72, WHT);
  d->drawRect(bx, by, bw, 72, BLK);
  d->drawRect(bx + 1, by + 1, bw - 2, 70, BLK);
  text(W / 2, by + 26, "Keep holding to power off", F9, AC);
  hbar(bx + 14, by + 38, bw - 28, 18, (heldMs - 1500) / 1500.0f);
}

void drawLive(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs) {
  begin(g);
  header(vm);
  if (vm.fresh) drawLiveBody(vm);
  else drawStatus(vm);
  powerBanner(heldMs);
}

void drawList(Adafruit_GFX& g, const ListModel& lm, const ViewModel& vm, uint32_t heldMs) {
  begin(g);
  header(vm, "SELECT VARIO");
  char b[40];
  snprintf(b, sizeof b, "filter: %.16s", lm.filter);
  text(6, HDR + 20, b, F9);
  const int n = lm.count, sel = lm.selected;
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
      const ListItem& it = lm.items[i - 1];
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
}

void drawMessage(Adafruit_GFX& g, const char* l1, const char* l2) {
  begin(g);
  text(W / 2, H / 2 - (l2 ? 8 : 0), l1, F18B, AC);
  if (l2) text(W / 2, H / 2 + 28, l2, F12B, AC);
}
