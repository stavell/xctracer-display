#include "apps/games/sim/sim_hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "apps/games/sim/sim_math.h"
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

namespace sim {

namespace {
// A number for the HUD: "--" for anything that is not finite (a wrong number that looks right is worse than none).
void fmt(char* dst, size_t n, const char* f, float v) {
  if (!isfinite(v)) snprintf(dst, n, "--");
  else snprintf(dst, n, f, (double)v);
}
void frame(const Bitmap1& bm, int x, int y, int w, int h) {
  bm.fillRect(x, y, w, h, DRAW_SET);
  bm.hspan(x, y, w, DRAW_CLEAR);
  bm.hspan(x, y + h - 1, w, DRAW_CLEAR);
  bm.vspan(x, y, h, DRAW_CLEAR);
  bm.vspan(x + w - 1, y, h, DRAW_CLEAR);
}
}  // namespace

void SimHud::ensureFonts(int rot) {
  if (!small_.isBuilt(&FreeSans9pt7b, rot)) small_.build(&FreeSans9pt7b, rot);
  if (!mid_.isBuilt(&FreeSansBold12pt7b, rot)) mid_.build(&FreeSansBold12pt7b, rot);
  if (!big_.isBuilt(&FreeSansBold24pt7b, rot)) big_.build(&FreeSansBold24pt7b, rot);
}

void SimHud::text(const Bitmap1& bm, const Font1& f, int x, int base, const char* s, DrawMode m) const {
  drawText(bm, x, base, s, f, m);
}
void SimHud::textC(const Bitmap1& bm, const Font1& f, int cx, int base, const char* s, DrawMode m) const {
  drawText(bm, cx - textWidth(s, f) / 2, base, s, f, m);
}

void SimHud::varioBar(const Bitmap1& bm, float vario) {
  const int x = 222, y = 34, w = 16, h = 300, mid = y + h / 2;
  frame(bm, x, y, w, h);
  const float scale = (h / 2 - 4) / 5.0f;                       // pixels per m/s, full scale +-5
  for (int k = -4; k <= 4; k++) {
    const int ty = mid - (int)lroundf(k * scale);
    bm.hspan(x + (k == 0 ? 1 : w - 5), ty, k == 0 ? w - 2 : 4, DRAW_CLEAR);
  }
  float v = isfinite(vario) ? clampf(vario, -5.0f, 5.0f) : 0.0f;
  const int len = (int)lroundf(fabsf(v) * scale);
  if (v >= 0) bm.fillRect(x + 3, mid - len, 7, len, DRAW_CLEAR);
  else bm.fillRect(x + 3, mid + 1, 7, len, DRAW_CLEAR);
}

void SimHud::windBox(const Bitmap1& bm, const HudData& d) {
  const int cx = 28, cy = 352, r = 24;
  fillDisc(bm, cx, cy, r, DRAW_SET);
  circle(bm, cx, cy, r, DRAW_CLEAR);
  circle(bm, cx, cy, r - 1, DRAW_CLEAR);
  // Arrow: the way the wind blows, relative to the nose (up).
  const float a = d.windRel;
  const float sx = sinf(a), sy = -cosf(a);
  const int tx = cx + (int)lroundf(sx * 17), ty = cy + (int)lroundf(sy * 17);
  const int bx = cx - (int)lroundf(sx * 17), by = cy - (int)lroundf(sy * 17);
  line(bm, bx, by, tx, ty, DRAW_CLEAR);
  line(bm, bx + 1, by, tx + 1, ty, DRAW_CLEAR);
  for (int s = -1; s <= 1; s += 2) {
    const float ha = a + kPi + s * 0.45f;
    line(bm, tx, ty, tx + (int)lroundf(sinf(ha) * 9), ty - (int)lroundf(cosf(ha) * 9), DRAW_CLEAR);
  }
  char b[16];
  fmt(b, sizeof b, "%.0f", d.windKmh);
  const int tw = textWidth(b, small_);
  bm.fillRect(cx - tw / 2 - 2, cy + r - 2, tw + 4, 18, DRAW_SET);
  textC(bm, small_, cx, cy + r + 13, b, DRAW_CLEAR);
}

void SimHud::draw(const Bitmap1& bm, const HudData& d, uint32_t fr) {
  ensureFonts(bm.rotation);
  // Top strip: altitude, climb, speed.
  bm.fillRect(0, 0, 240, kStripH, DRAW_CLEAR);
  char b[24];
  fmt(b, sizeof b, "%.0f", d.alt);
  text(bm, mid_, 4, 19, b, DRAW_SET);
  const int aw = textWidth(b, mid_);
  text(bm, small_, 4 + aw + 2, 19, "m", DRAW_SET);
  fmt(b, sizeof b, d.vario >= 0 ? "+%.1f" : "%.1f", d.vario);
  textC(bm, mid_, 118, 19, b, DRAW_SET);
  const int uw = textWidth("km/h", small_);
  text(bm, small_, 238 - uw, 19, "km/h", DRAW_SET);
  fmt(b, sizeof b, "%.0f", d.speedKmh);
  text(bm, mid_, 236 - uw - textWidth(b, mid_), 19, b, DRAW_SET);
  varioBar(bm, d.vario);
  windBox(bm, d);
  // Height above ground, bottom right.
  fmt(b, sizeof b, "%.0f", d.agl);
  char c[32];
  snprintf(c, sizeof c, "AGL %s", b);
  const int cw = textWidth(c, mid_);
  frame(bm, 238 - cw - 10, 372, cw + 10, 26);
  text(bm, mid_, 238 - cw - 5, 392, c, DRAW_CLEAR);
  if (d.timeScale > 1) {
    char x[8];
    snprintf(x, sizeof x, "x%d", d.timeScale);
    const int w = textWidth(x, mid_) + 10;
    frame(bm, 4, 30, w, 26);
    text(bm, mid_, 9, 50, x, DRAW_CLEAR);
  }
  if (d.assist) text(bm, small_, 60, 392, "assist", DRAW_CLEAR);
  if (d.stalled && ((fr / 12) & 1) == 0) {                      // blinks at about 2 Hz at 50 fps
    bm.fillRect(40, 60, 150, 36, DRAW_CLEAR);
    textC(bm, mid_, 115, 85, "STALL", DRAW_SET);
  }
}

void SimHud::banner(const Bitmap1& bm, const char* title, const char* l1, const char* l2, const char* l3) {
  ensureFonts(bm.rotation);
  const int x = 20, y = 130, w = 200, h = 140;
  frame(bm, x, y, w, h);
  bm.fillRect(x, y, w, 36, DRAW_CLEAR);
  textC(bm, mid_, 120, y + 26, title, DRAW_SET);
  const char* ls[3] = {l1, l2, l3};
  int by = y + 62;
  for (int i = 0; i < 3; i++)
    if (ls[i]) { textC(bm, small_, 120, by, ls[i], DRAW_CLEAR); by += 24; }
}

void SimHud::menu(const Bitmap1& bm, const char* title, const char* const* rows, int n, int sel) {
  ensureFonts(bm.rotation);
  const int x = 14, w = 212, rowH = n > 10 ? 27 : 30, h = 44 + n * rowH + 6, y = 200 - h / 2;
  frame(bm, x, y, w, h);
  bm.fillRect(x, y, w, 36, DRAW_CLEAR);
  textC(bm, mid_, 120, y + 26, title, DRAW_SET);
  for (int i = 0; i < n; i++) {
    const int ry = y + 42 + i * rowH;
    if (i == sel) bm.fillRect(x + 4, ry, w - 8, rowH - 2, DRAW_CLEAR);
    text(bm, small_, x + 12, ry + 20, rows[i], i == sel ? DRAW_SET : DRAW_CLEAR);
  }
}

void SimHud::note(const Bitmap1& bm, const char* line1, const char* line2) {
  ensureFonts(bm.rotation);
  textC(bm, mid_, 120, 180, "Flight sim", DRAW_CLEAR);
  textC(bm, small_, 120, 220, line1, DRAW_CLEAR);
  if (line2) textC(bm, small_, 120, 246, line2, DRAW_CLEAR);
}

}  // namespace sim
