#pragma once
#include <Adafruit_GFX.h>
#include "view/viewmodel.h"
#include "gfx/bitmap1.h"

// Pure drawing: takes a canvas and a view model, touches no hardware. The caller clears the canvas
// first (IPanel::clear(): a filled rectangle over the whole screen is per-pixel and takes 140 ms). The canvas size (and so
// portrait or landscape) comes from g.width() / g.height().
// bm, when given, is the same canvas as a Bitmap1; the standalone compass uses it for dithered grey
// (drawn first, never under text).
void drawLive(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs, const Bitmap1* bm = nullptr);
void drawList(Adafruit_GFX& g, const ListModel& lm, const ViewModel& vm, uint32_t heldMs);
void drawCalibrate(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs);
void drawMessage(Adafruit_GFX& g, const char* line1, const char* line2 = nullptr);
