#include "platform/ui/system_sheet.h"

using namespace ui;

namespace {
const char* label(SystemSheet::Row r) {
  switch (r) {
    case SystemSheet::R_BACK: return "Back";
    case SystemSheet::R_HOME: return "Home";
    case SystemSheet::R_SLEEP: return "Sleep";
    default: return "Power off";
  }
}
}  // namespace

void SystemSheet::draw(const Bitmap1& bm, bool sixButtons) {
  f_.ensure(bm.rotation);
  const int rowH = 44, headH = 38, footH = 40;
  const int w = 200, h = headH + n_ * rowH + footH;
  const int x = (W - w) / 2, y = (H - h) / 2;
  roundFrame(bm, x, y, w, h, 10, 3, DRAW_CLEAR);                 // black outline, white inside
  centered(bm, f_.bold, W / 2, y + 26, "SYSTEM", DRAW_CLEAR);
  bm.hspan(x + 12, y + headH - 4, w - 24, DRAW_CLEAR);
  for (int i = 0; i < n_; i++) {
    const int ry = y + headH + i * rowH;
    const bool on = i == cur_;
    if (on) roundFill(bm, x + 8, ry, w - 16, rowH - 4, 7, DRAW_CLEAR);
    centered(bm, f_.mid, W / 2, ry + 28, label(rows_[i]), on ? DRAW_SET : DRAW_CLEAR);
  }
  const int fy = y + headH + n_ * rowH;
  if (keepHolding()) {
    centered(bm, f_.bold, W / 2, fy + 18, "keep holding: power off", DRAW_CLEAR);
    bar(bm, x + 14, fy + 24, w - 28, 8, barStep() / 12.0f);
  }
  else centered(bm, f_.small, W / 2, fy + 22, sixButtons ? "up/down  A: do  B: back" : "press: next   hold: do", DRAW_CLEAR);
}
