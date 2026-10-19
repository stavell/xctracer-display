#pragma once
// HUD of the flight simulator: a black strip with altitude / climb / speed on top, a vario bar on the right edge, a
// wind arrow and the height above ground at the bottom, banners and the pause menu. Fast gfx only (spans, atlas
// text, shapes); portrait 240 x 400, rotation 1.
#include <math.h>
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"

namespace sim {

struct HudData {
  float alt = 0;               // m
  float vario = 0;             // m/s
  float agl = 0;               // m, NaN when unknown
  float speedKmh = 0;          // airspeed
  float windKmh = 0;
  float windRel = 0;           // rad: direction the wind blows to, relative to the glider's heading (0 = ahead)
  float bankDeg = 0;
  bool stalled = false;
  bool assist = false;
  float flightS = 0;           // s since takeoff (sim clock)
  float fromStartM = NAN;      // m, straight line from the takeoff
  int timeScale = 1;           // shown as a box "x3" under the top strip when > 1
};

class SimHud {
 public:
  static constexpr int kStripH = 24;
  void draw(const Bitmap1& bm, const HudData& d, uint32_t frame);
  // Centered banner with up to three lines (inverse title).
  void banner(const Bitmap1& bm, const char* title, const char* l1, const char* l2, const char* l3);
  // Pause menu: rows with the selected one inverted.
  void menu(const Bitmap1& bm, const char* title, const char* const* rows, int n, int sel);
  void note(const Bitmap1& bm, const char* line1, const char* line2);   // plain message page

 private:
  void ensureFonts(int rot);
  void varioBar(const Bitmap1& bm, float vario);
  void windBox(const Bitmap1& bm, const HudData& d);
  void text(const Bitmap1& bm, const Font1& f, int x, int base, const char* s, DrawMode m) const;
  void textC(const Bitmap1& bm, const Font1& f, int cx, int base, const char* s, DrawMode m) const;
  Font1 small_, mid_, big_;
};

}  // namespace sim
