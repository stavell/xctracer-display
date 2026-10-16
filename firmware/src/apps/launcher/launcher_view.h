#pragma once
// The launcher screen: a header with the battery, one row per app (icon, name, one line of text), a
// "Start-up" settings row, a footer with the firmware version and the key hint. 1-bit, portrait 240x400,
// drawn straight into a Bitmap1 like the games. Stateless apart from the font cache.
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"

enum LauncherIcon : uint8_t { ICON_VARIO, ICON_TETRIS, ICON_COMPASS, ICON_GEAR };

struct LauncherRow {                // plain aggregate (the device compiles as C++11)
  const char* title;
  const char* sub;
  LauncherIcon icon;
};

struct LauncherModel {
  static const int MAX_ROWS = 6;
  LauncherRow rows[MAX_ROWS] = {};
  int count = 0;
  int selected = 0;
  int battPct = -1;                 // < 0: unknown
  const char* version = "";
  bool dpad = false;                // hint text for the six buttons instead of the single switch
  uint32_t heldMs = 0;              // the switch is being held: from 1.5 s the footer warns about power-off
};

class LauncherView {
 public:
  static const int ROW_Y = 40, ROW_H = 68;
  void draw(const Bitmap1& bm, const LauncherModel& m);

 private:
  void ensureFonts(int rot);
  void icon(const Bitmap1& bm, LauncherIcon ic, int x, int y, DrawMode ink) const;
  Font1 small_, mid_, bold_;
};
