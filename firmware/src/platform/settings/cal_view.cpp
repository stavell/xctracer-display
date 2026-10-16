#include "platform/settings/cal_view.h"
#include <stdio.h>

void drawCompassCal(const Bitmap1& bm, UiFonts& f, const CalModel& m) {
  using namespace ui;
  ui::titleBar(bm, f, "CALIBRATE", m.battPct);
  centered(bm, f.small, W / 2, 70, "Turn the unit slowly", DRAW_CLEAR);
  centered(bm, f.small, W / 2, 92, "in every direction:", DRAW_CLEAR);
  centered(bm, f.small, W / 2, 114, "flat, on edge, inverted", DRAW_CLEAR);
  static const char* ax[3] = {"X", "Y", "Z"};
  for (int i = 0; i < 3; i++) {
    const int y = 142 + i * 34;
    drawText(bm, 14, y + 17, ax[i], f.mid, DRAW_CLEAR);
    bar(bm, 40, y, W - 60, 22, m.span[i] / 70.0f);          // about 70 uT of swing per axis is plenty
  }
  char b[32];
  snprintf(b, sizeof b, "field %.0f uT", m.fieldUt);
  centered(bm, f.small, W / 2, 268, b, DRAW_CLEAR);
  centered(bm, f.small, W / 2, 290, m.stored ? "replaces the saved calibration" : "no calibration saved yet", DRAW_CLEAR);
  bm.hspan(0, 312, W, DRAW_CLEAR);
  bm.hspan(0, 313, W, DRAW_CLEAR);
  if (m.heldMs >= 1500) {
    centered(bm, f.bold, W / 2, 350, m.holdWarning, DRAW_CLEAR);
  } else {
    centered(bm, f.mid, W / 2, 342, "hold / A: save", DRAW_CLEAR);
    centered(bm, f.mid, W / 2, 378, "press / B: cancel", DRAW_CLEAR);
  }
}
