#pragma once
// Screens of the Settings app: the list of settings (title + current value per row) and the About page.
// 1-bit, portrait 240x400, drawn straight into a Bitmap1. The calibration screen is the platform's
// (platform/settings/cal_view.h).
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/ui.h"

struct SettingsModel {
  static const int MAX_ROWS = 8;
  const char* title[MAX_ROWS] = {};
  char sub[MAX_ROWS][32] = {};
  int count = 0;
  int selected = 0;
  int battPct = -1;
  bool dpad = false;
  uint32_t heldMs = 0;              // the switch is held: from 1.5 s the footer says the box goes home
};

struct AboutModel {
  const char* version = "";
  bool packOk = false;
  char area[64] = "";
  uint32_t packBytes = 0;
  char credit1[200] = "";            // Copernicus
  char credit2[200] = "";            // OpenStreetMap contributors
  int battPct = -1;
};

class SettingsView {
 public:
  static const int ROW_Y = 40, ROW_H = 44;
  void drawList(const Bitmap1& bm, const SettingsModel& m);
  void drawAbout(const Bitmap1& bm, const AboutModel& m);
  UiFonts& fonts() { return f_; }

 private:
  UiFonts f_;
};
