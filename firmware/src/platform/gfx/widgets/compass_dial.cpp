#include "platform/gfx/widgets/compass_dial.h"
#include <math.h>
#include "platform/gfx/shapes.h"
#include "platform/gfx/strokes.h"
#include "platform/util/angle.h"

namespace widgets {

static const float PI_F = 3.14159265f;

void drawCompassDial(DialPen& pen, int cx, int cy, int R, float heading, DialSrc src) {
  const bool have = isfinite(heading);
  const float course = have ? heading : 0.0f;
  pen.part(DIAL_BEZEL, true);
  pen.circle(cx, cy, R);
  pen.circle(cx, cy, R - 1);
  pen.triangle(cx - 8, cy - R - 4, cx + 8, cy - R - 4, cx, cy - R + 11);   // direction of travel
  pen.part(DIAL_BEZEL, false);
  pen.part(DIAL_TICKS, true);
  const int rl = R - 26;   // label radius, clear of the grey ring (R-1 .. R-12)
  for (int a = 0; a < 360; a += 10) {
    if (a % 45 == 0) continue;   // letters sit there
    float th = (a - course) * PI_F / 180.0f;
    int len = (a % 30 == 0) ? 9 : 5;
    pen.line(cx + (R - 2) * sinf(th), cy - (R - 2) * cosf(th),
             cx + (R - 2 - len) * sinf(th), cy - (R - 2 - len) * cosf(th));
  }
  pen.part(DIAL_TICKS, false);
  pen.part(DIAL_LABELS, true);
  static const char* L[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  for (int i = 0; i < 8; i++) {
    float th = (i * 45 - course) * PI_F / 180.0f;
    int lx = cx + rl * sinf(th), ly = cy - rl * cosf(th);
    if (i == 0) {
      pen.fillRect(lx - 12, ly - 12, 25, 25);
      pen.textMid(lx, ly, "N", DIAL_F12B, true);
    } else if (i % 2 == 0) {
      pen.textMid(lx, ly, L[i], DIAL_F12B, false);
    } else {
      pen.textMid(lx, ly, L[i], DIAL_F9, false);
    }
  }
  pen.part(DIAL_LABELS, false);
  pen.part(DIAL_NUMBER, true);
  char b[8];
  if (have) {
    if (src != DIAL_SRC_NONE) pen.textCentered(cx, cy - 14, src == DIAL_SRC_MAG ? "MAG" : "TRK", DIAL_F9);
    pen.textCentered(cx, cy + 12, bearingText(b, sizeof b, heading), DIAL_F18B);
  } else {
    pen.textCentered(cx, cy + 8, "--", DIAL_F18B);
  }
  pen.part(DIAL_NUMBER, false);
}

void BitmapDialPen::line(int x0, int y0, int x1, int y1) { ::line(bm_, x0, y0, x1, y1, DRAW_CLEAR); }
void BitmapDialPen::circle(int cx, int cy, int r) { ::circle(bm_, cx, cy, r, DRAW_CLEAR); }

void BitmapDialPen::triangle(int x0, int y0, int x1, int y1, int x2, int y2) {
  const int p[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
  strokes::polyFill(bm_, p, 3, DRAW_CLEAR);
}

void BitmapDialPen::textMid(int cx, int cy, const char* s, DialFont f, bool white) {
  const Font1& ft = font(f);
  int x1, y1, w, h;
  textBounds(s, ft, x1, y1, w, h, bm_.width());
  drawText(bm_, cx - w / 2 - x1, cy - y1 - h / 2, s, ft, white ? DRAW_SET : DRAW_CLEAR, bm_.width());
}

void BitmapDialPen::textCentered(int cx, int baseline, const char* s, DialFont f) {
  const Font1& ft = font(f);
  int x1, y1, w, h;
  textBounds(s, ft, x1, y1, w, h, bm_.width());
  drawText(bm_, cx - w / 2 - x1, baseline, s, ft, DRAW_CLEAR, bm_.width());
}

}  // namespace widgets
