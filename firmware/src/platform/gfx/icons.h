#pragma once
// The box's app icons: 48x48, 1-bit, drawn with the shape primitives (no bitmaps in flash). An app registers with
// one of these ids; a new app adds an id and a case in icons.cpp.
#include <stdint.h>
#include "platform/gfx/bitmap1.h"

enum IconId : uint8_t { ICON_VARIO, ICON_TETRIS, ICON_COMPASS, ICON_SETTINGS, ICON_SIM, ICON_GENERIC, ICON_MAP };

const int ICON_SIZE = 48;

// Draw the icon with its top left at (x, y) in `ink` over a `paper` background (the opposite colour).
void drawIcon(const Bitmap1& bm, IconId id, int x, int y, DrawMode ink);
