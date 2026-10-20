#pragma once
// The heading-up compass dial in the Garmin style, shared by the vario's compass page and the compass app: the
// ring turns so the heading is under the fixed pointer at the top; big N/E/S/W and smaller NE/SE/SW/NW sit on it,
// ticks every 10 degrees (longer every 30), the heading in degrees is the big number in the centre with its source
// above it. With no heading the dial stays north-up and shows "--".
//
// The widget draws through a DialPen, so each user keeps its own canvas and text path: the vario's view draws on an
// Adafruit_GFX canvas with its own glyph sheets (its frames must not change by a pixel), a platform app uses
// BitmapDialPen below. The grey ring under the ticks is the caller's (fillRing with a Gray, before any other drawing).
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/font.h"
#include "platform/gfx/ui.h"

namespace widgets {

enum DialSrc { DIAL_SRC_NONE, DIAL_SRC_MAG, DIAL_SRC_TRK };     // the label above the number (none: no label)
enum DialFont { DIAL_F9, DIAL_F12B, DIAL_F18B };                // Sans 9, SansBold 12, SansBold 18
enum DialPart { DIAL_BEZEL, DIAL_TICKS, DIAL_LABELS, DIAL_NUMBER };

struct DialPen {
  virtual ~DialPen() {}
  virtual void line(int x0, int y0, int x1, int y1) = 0;                    // black
  virtual void circle(int cx, int cy, int r) = 0;                           // black outline
  virtual void triangle(int x0, int y0, int x1, int y1, int x2, int y2) = 0;   // black, filled
  virtual void fillRect(int x, int y, int w, int h) = 0;                    // black
  virtual void textMid(int cx, int cy, const char* s, DialFont f, bool white) = 0;   // centred on the point by its ink
  virtual void textCentered(int cx, int baseline, const char* s, DialFont f) = 0;    // black, baseline y
  virtual void part(DialPart, bool begin) { (void)begin; }                  // profiling hooks
};

// R = ring radius (the ticks sit just inside it), heading degrees (NaN: no heading).
void drawCompassDial(DialPen& pen, int cx, int cy, int R, float heading, DialSrc src);

// A pen over a Bitmap1 (any rotation) with the platform's glyph sheets. big is the SansBold 18 sheet (the box
// shares one block among equal sheets, so it costs nothing when another screen built it).
class BitmapDialPen : public DialPen {
 public:
  BitmapDialPen(const Bitmap1& bm, UiFonts& fonts, Font1& big) : bm_(bm), f_(fonts), big_(big) {}
  void line(int x0, int y0, int x1, int y1) override;
  void circle(int cx, int cy, int r) override;
  void triangle(int x0, int y0, int x1, int y1, int x2, int y2) override;
  void fillRect(int x, int y, int w, int h) override { bm_.fillRect(x, y, w, h, DRAW_CLEAR); }
  void textMid(int cx, int cy, const char* s, DialFont f, bool white) override;
  void textCentered(int cx, int baseline, const char* s, DialFont f) override;

 private:
  const Font1& font(DialFont f) const { return f == DIAL_F9 ? f_.small : f == DIAL_F12B ? f_.mid : big_; }
  const Bitmap1& bm_;
  UiFonts& f_;
  Font1& big_;
};

}  // namespace widgets
