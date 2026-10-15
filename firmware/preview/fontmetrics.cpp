#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
static void m(const char* n, const GFXfont* f) {
  const GFXglyph* g0 = &f->glyph['0' - f->first];
  const GFXglyph* gp = &f->glyph['.' - f->first];
  const GFXglyph* gm = &f->glyph['-' - f->first];
  const GFXglyph* gw = &f->glyph['W' - f->first];
  printf("%-18s yAdv %2d  digit: adv %2d  w %2d  h %2d  | '.' adv %2d  '-' adv %2d  'W' adv %2d\n",
         n, f->yAdvance, g0->xAdvance, g0->width, g0->height, gp->xAdvance, gm->xAdvance, gw->xAdvance);
}
int main() {
  m("FreeSans9pt", &FreeSans9pt7b); m("FreeSansBold9pt", &FreeSansBold9pt7b);
  m("FreeSansBold12pt", &FreeSansBold12pt7b); m("FreeSansBold18pt", &FreeSansBold18pt7b);
  m("FreeSansBold24pt", &FreeSansBold24pt7b);
}
