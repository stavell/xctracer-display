#pragma once
#include <Adafruit_SharpMem.h>
#include "sensors.h"

void uiBegin(Adafruit_SharpMem* d);
void uiInvalidate();                       // next draw always refreshes the panel
uint32_t uiIntervalMs(Page p);             // redraw cadence for a page
void uiDraw(Page p, uint32_t heldMs);      // render + refresh only if the picture changed
void uiMessage(const char* line1, const char* line2 = nullptr);   // centered splash
void uiBlank();                            // white screen

void uiDrawList(int sel, uint32_t heldMs);   // device discovery list; sel 0 = back
