#pragma once
#include <Adafruit_GFX.h>
#include "apps/vario/view/viewmodel.h"
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"

// Pure drawing: takes a canvas and a view model, touches no hardware. The caller clears the canvas
// first (IPanel::clear(): a filled rectangle over the whole screen is per-pixel and takes 140 ms). The canvas size (and so
// portrait or landscape) comes from g.width() / g.height().
// bm, when given, is the same canvas as a Bitmap1; the standalone compass uses it for dithered grey
// (drawn first, never under text) and all text is drawn from pre-rotated glyph sheets.
void drawLive(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs, const Bitmap1* bm = nullptr);
void drawList(Adafruit_GFX& g, const ListModel& lm, const ViewModel& vm, uint32_t heldMs, const Bitmap1* bm = nullptr);
void drawCalibrate(Adafruit_GFX& g, const ViewModel& vm, uint32_t heldMs, const Bitmap1* bm = nullptr);
void drawMessage(Adafruit_GFX& g, const char* line1, const char* line2 = nullptr, const Bitmap1* bm = nullptr);

// Text goes through the pre-rotated glyph sheets (platform/gfx/font.h) whenever a draw call got a Bitmap1; off
// forces Adafruit_GFX text (tests compare the two). viewFontRam: bytes the sheets hold right now.
void viewFastText(bool on);
size_t viewFontRam();
// The sheet the view uses for one of its fonts, built now for rotation rot if needed (nullptr: not a
// view font or out of memory). viewGfxFont(i) is the i-th such font: 0 Sans9, 1 SansBold9, 2 Bold12,
// 3 Bold18, 4 Bold24 (the view's own copies: font tables are per translation unit). Diagnostics time it.
const GFXfont* viewGfxFont(int i);
const Font1* viewFont(const GFXfont* f, int rot);

// The thermal map's frame in pixels for a canvas of this size, and the zoom step (pixels per metre) the
// map would pick for this view model: the app glides the shown scale toward it (ViewModel::mapScale).
void thermalMapSize(int canvasW, int canvasH, int& mw, int& mh);
float thermalMapScale(const ViewModel& vm, int mw, int mh);

// The map page: the frame (everything right of the thin vario and below the header) for a canvas of this size.
// Its cached picture (view/terrain_map.h) is rendered for exactly this size.
void mapPageSize(int canvasW, int canvasH, int& mw, int& mh);
