#include "platform/gfx/icons.h"
#include <math.h>
#include "platform/gfx/shapes.h"

namespace {
DrawMode other(DrawMode m) { return m == DRAW_SET ? DRAW_CLEAR : DRAW_SET; }
}  // namespace

void drawIcon(const Bitmap1& bm, IconId id, int x, int y, DrawMode ink) {
  const DrawMode paper = other(ink);
  const int cx = x + 24, cy = y + 24;
  switch (id) {
    case ICON_VARIO: {                                    // a dial with ticks and a needle
      circle(bm, cx, cy, 23, ink);
      circle(bm, cx, cy, 22, ink);
      for (int a = -60; a <= 60; a += 30) {
        float r = a * 0.017453292f;
        line(bm, cx + (int)lroundf(sinf(r) * 16), cy - (int)lroundf(cosf(r) * 16),
             cx + (int)lroundf(sinf(r) * 20), cy - (int)lroundf(cosf(r) * 20), ink);
      }
      line(bm, cx, cy, cx + 11, cy - 13, ink);
      line(bm, cx + 1, cy, cx + 12, cy - 13, ink);
      fillDisc(bm, cx, cy, 3, ink);
      break;
    }
    case ICON_TETRIS: {                                   // a few blocks
      static const uint8_t cells[][2] = {{0, 0}, {1, 0}, {2, 0}, {1, 1}, {0, 2}, {1, 2}};
      for (const auto& c : cells) {
        int px = x + 1 + c[0] * 16, py = y + 1 + c[1] * 16;
        bm.fillRect(px, py, 15, 15, ink);
        bm.fillRect(px + 4, py + 4, 7, 7, paper);
        bm.fillRect(px + 6, py + 6, 3, 3, ink);
      }
      break;
    }
    case ICON_COMPASS: {                                  // ring and a two-tone needle
      circle(bm, cx, cy, 23, ink);
      circle(bm, cx, cy, 22, ink);
      for (int r = 0; r <= 17; r++) {                     // north half solid
        int hw = 6 * r / 17;
        bm.hspan(cx - hw, cy - 17 + r, 2 * hw + 1, ink);
      }
      line(bm, cx - 6, cy, cx, cy + 17, ink);             // south half outlined
      line(bm, cx + 6, cy, cx, cy + 17, ink);
      line(bm, cx - 6, cy, cx + 6, cy, ink);
      bm.hspan(cx - 1, cy - 1, 3, paper);
      break;
    }
    case ICON_SETTINGS: {                                 // three sliders
      static const uint8_t knob[3] = {32, 12, 24};        // knob x inside the 48 box
      for (int i = 0; i < 3; i++) {
        const int ly = y + 8 + i * 16;
        bm.fillRect(x + 2, ly - 1, 44, 3, ink);
        bm.fillRect(x + knob[i] - 4, ly - 7, 9, 15, ink);
        bm.fillRect(x + knob[i] - 2, ly - 5, 5, 11, paper);
        bm.fillRect(x + knob[i] - 1, ly - 1, 3, 3, ink);
      }
      break;
    }
    case ICON_SIM: {                                      // a paraglider wing seen from the front, above a ridge line
      for (int i = -22; i <= 22; i++) {                   // canopy: an arch, two pixels thick
        const int hh = 14 - (i * i) / 36;
        bm.vspan(cx + i, y + 6 + (14 - hh), 3, ink);
      }
      line(bm, cx - 22, y + 20, cx - 3, y + 33, ink);     // lines to the pilot
      line(bm, cx + 22, y + 20, cx + 3, y + 33, ink);
      line(bm, cx, y + 8, cx, y + 33, ink);
      fillDisc(bm, cx, y + 35, 3, ink);
      for (int i = 0; i < 48; i++) {                      // the ridge
        const int rh = 6 + (int)(5 * sinf(i * 0.25f) + 3 * sinf(i * 0.7f));
        bm.vspan(x + i, y + 47 - rh, rh + 1, ink);
      }
      break;
    }
    case ICON_MAP: {                                      // a folded map (three panels) with a position pin
      static const int8_t px[4] = {2, 17, 31, 46}, top[4] = {8, 4, 8, 4};
      for (int t = 0; t < 2; t++)
        for (int i = 0; i < 3; i++) {
          line(bm, x + px[i], y + top[i] + t, x + px[i + 1], y + top[i + 1] + t, ink);            // top edge
          line(bm, x + px[i], y + top[i] + 36 - t, x + px[i + 1], y + top[i + 1] + 36 - t, ink);  // bottom edge
        }
      for (int i = 0; i < 4; i++) bm.vspan(x + px[i] - (i == 3), y + top[i], 37, ink);
      fillDisc(bm, cx, y + 19, 8, ink);
      for (int r = 0; r < 12; r++) bm.hspan(cx - 6 + r / 2, y + 23 + r, 13 - r, ink);
      fillDisc(bm, cx, y + 19, 3, paper);
      break;
    }
    case ICON_GENERIC: {                                  // a plain rounded square with a dot
      bm.fillRect(x + 4, y + 4, 40, 40, ink);
      bm.fillRect(x + 7, y + 7, 34, 34, paper);
      fillDisc(bm, cx, cy, 6, ink);
      break;
    }
  }
}
