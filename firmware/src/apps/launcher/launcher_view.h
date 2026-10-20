#pragma once
// The home screen: a status bar (time, map pack, battery), a grid of large app tiles (icon, name; the chosen tile
// is inverted), and a footer with the chosen app's one-line description and the key hint. 1-bit, portrait
// 240x400, drawn straight into a Bitmap1 like the games. Stateless apart from the font cache.
#include <stdint.h>
#include "platform/app_registry.h"
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/ui.h"

struct HomeModel {
  static const int MAX_TILES = 6;     // two columns, up to three rows
  AppEntry tiles[MAX_TILES];
  int count = 0;
  int selected = 0;
  int battPct = -1;                   // < 0: unknown
  int localMinutes = -1;              // minutes of the local day, < 0: unknown
  bool mapOk = false;                 // a map pack is installed and passed its checks
  bool dpad = false;                  // hint text for the six buttons instead of the single switch
};

class LauncherView {
 public:
  static const int COLS = 2;
  void draw(const Bitmap1& bm, const HomeModel& m);

 private:
  UiFonts f_;
};
