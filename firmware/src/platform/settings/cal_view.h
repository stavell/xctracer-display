#pragma once
// The compass calibration screen, the same wherever the flow runs (the Settings app, the vario when the laptop
// starts it). Draws straight into a Bitmap1 on a white canvas.
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/ui.h"

struct CalModel {
  float span[3] = {0, 0, 0};       // microtesla covered per axis
  float fieldUt = 0;
  bool stored = false;             // a calibration exists (the new one replaces it when kept)
  int battPct = -1;
  uint32_t heldMs = 0;             // the switch is down: from 1.5 s the footer warns about what 3 s will do
  const char* holdWarning = "keep holding: power off";
};

void drawCompassCal(const Bitmap1& bm, UiFonts& fonts, const CalModel& m);
