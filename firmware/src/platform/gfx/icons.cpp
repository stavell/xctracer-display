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
    case ICON_GENERIC: {                                  // a plain rounded square with a dot
      bm.fillRect(x + 4, y + 4, 40, 40, ink);
      bm.fillRect(x + 7, y + 7, 34, 34, paper);
      fillDisc(bm, cx, cy, 6, ink);
      break;
    }
  }
}
