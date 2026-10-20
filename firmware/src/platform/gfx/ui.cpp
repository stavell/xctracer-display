#include "platform/gfx/ui.h"
#include <stdio.h>
#include <string.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

void UiFonts::ensure(int rot) {
  if (!small.isBuilt(&FreeSans9pt7b, rot)) small.build(&FreeSans9pt7b, rot);
  if (!bold.isBuilt(&FreeSansBold9pt7b, rot)) bold.build(&FreeSansBold9pt7b, rot);
  if (!mid.isBuilt(&FreeSansBold12pt7b, rot)) mid.build(&FreeSansBold12pt7b, rot);
}

namespace ui {

void roundFill(const Bitmap1& bm, int x, int y, int w, int h, int r, DrawMode m) {
  if (w <= 0 || h <= 0) return;
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  for (int i = 0; i < h; i++) {
    int d = i < r ? r - i : (i >= h - r ? i - (h - r - 1) : 0);   // rows near the top / bottom lose d pixels per side
    int cut = 0;
    if (d > 0) cut = d >= r ? r : (d * d + r - 1) / r;            // a gentle arc rather than a diagonal
    bm.hspan(x + cut, y + i, w - 2 * cut, m);
  }
}

void roundFrame(const Bitmap1& bm, int x, int y, int w, int h, int r, int thick, DrawMode m) {
  roundFill(bm, x, y, w, h, r, m);
  roundFill(bm, x + thick, y + thick, w - 2 * thick, h - 2 * thick, r > thick ? r - thick : 0, other(m));
}

void battery(const Bitmap1& bm, int x, int y, int pct, DrawMode ink) {
  const DrawMode paper = other(ink);
  bm.fillRect(x, y, 26, 13, ink);
  bm.fillRect(x + 26, y + 4, 3, 5, ink);                         // terminal
  bm.fillRect(x + 2, y + 2, 22, 9, paper);
  if (pct >= 0) {
    int fill = pct >= 100 ? 18 : pct * 18 / 100;
    if (pct > 0 && fill < 2) fill = 2;                           // never an empty-looking glyph while there is charge
    bm.fillRect(x + 4, y + 4, fill, 5, ink);
  }
}

void titleBar(const Bitmap1& bm, UiFonts& f, const char* title, int battPct, const char* badge, bool framed) {
  f.ensure(bm.rotation);
  bm.fillRect(0, 0, W, BAR_H, DRAW_CLEAR);
  drawText(bm, 10, 23, title, f.mid, DRAW_SET);
  if (badge) {
    const int cx = 10 + textWidth(title, f.mid) + 14;
    if (framed) {
      const int cw = 44;
      roundFrame(bm, cx, 7, cw, 18, 3, 2, DRAW_SET);
      drawText(bm, cx + (cw - textWidth(badge, f.bold)) / 2, 21, badge, f.bold, DRAW_SET);
    } else {
      drawText(bm, cx, 21, badge, f.small, DRAW_SET);
    }
  }
  if (battPct >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d%%", battPct);
    const int tw = textWidth(b, f.bold);
    drawText(bm, W - 10 - tw, 22, b, f.bold, DRAW_SET);
    battery(bm, W - 10 - tw - 36, 9, battPct, DRAW_SET);
  }
}

void hintLine(const Bitmap1& bm, UiFonts& f, const char* text) {
  f.ensure(bm.rotation);
  bm.hspan(0, HINT_TOP, W, DRAW_CLEAR);
  bm.hspan(0, HINT_TOP + 1, W, DRAW_CLEAR);
  drawText(bm, (W - textWidth(text, f.small)) / 2, H - 12, text, f.small, DRAW_CLEAR);
}

const Font1& fitFont(const char* s, int maxW, const Font1& a, const Font1& b) {
  return textWidth(s, a) <= maxW ? a : b;
}

void centered(const Bitmap1& bm, const Font1& f, int cx, int baseline, const char* s, DrawMode m) {
  drawText(bm, cx - textWidth(s, f) / 2, baseline, s, f, m);
}

int wrapped(const Bitmap1& bm, const Font1& f, int x, int baseline, int w, int lineH, const char* s, DrawMode m) {
  char line[80] = "";
  int n = 0;
  const char* p = s;
  while (*p) {
    // one word
    const char* e = p;
    while (*e && *e != ' ') e++;
    char word[48];
    int wl = (int)(e - p);
    if (wl > (int)sizeof word - 1) wl = (int)sizeof word - 1;
    memcpy(word, p, wl);
    word[wl] = 0;
    char trial[128];
    snprintf(trial, sizeof trial, "%s%s%s", line, n ? " " : "", word);
    if (n && textWidth(trial, f) > w) {
      drawText(bm, x, baseline, line, f, m);
      baseline += lineH;
      snprintf(line, sizeof line, "%s", word);
      n = (int)strlen(line);
    } else {
      snprintf(line, sizeof line, "%s", trial);
      n = (int)strlen(line);
    }
    p = e;
    while (*p == ' ') p++;
  }
  if (n) {
    drawText(bm, x, baseline, line, f, m);
    baseline += lineH;
  }
  return baseline;
}

void bar(const Bitmap1& bm, int x, int y, int w, int h, float frac) {
  bm.fillRect(x, y, w, h, DRAW_CLEAR);
  bm.fillRect(x + 2, y + 2, w - 4, h - 4, DRAW_SET);
  if (!(frac > 0)) return;
  if (frac > 1) frac = 1;
  bm.fillRect(x + 4, y + 4, (int)((w - 8) * frac), h - 8, DRAW_CLEAR);
}

}  // namespace ui
