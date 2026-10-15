#pragma once
#include <Adafruit_GFX.h>
#include "view/viewmodel.h"

// Pure drawing: takes a canvas and a view model, touches no hardware. The caller clears the canvas
// first (IPanel::clear(): a filled rectangle over the whole screen is per-pixel and takes 140 ms). The canvas size (and so
// portrait or landscape) comes from g.width() / g.height().
void drawLive(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs);
void drawList(Adafruit_GFX& g, const ListModel& lm, const ViewModel& vm, uint32_t heldMs);
void drawMessage(Adafruit_GFX& g, const char* line1, const char* line2 = nullptr);
