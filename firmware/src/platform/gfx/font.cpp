#include "platform/gfx/font.h"
#include <stdlib.h>
#include <new>

namespace {
struct GlyphBits { const uint8_t* bits; int w; };
// GFX glyph bitmaps are packed MSB first, rows back to back with no padding.
bool glyphPx(void* ctx, int x, int y) {
  const GlyphBits* g = (const GlyphBits*)ctx;
  int i = y * g->w + x;
  return (g->bits[i >> 3] << (i & 7)) & 0x80;
}
}  // namespace

Font1::~Font1() { free(block_); }

const Font1::Glyph* Font1::glyph(unsigned char c) const {
  if (!glyphs_ || c < FIRST || c > LAST || c < first_ || c > last_) return nullptr;
  return &glyphs_[c - FIRST];
}

bool Font1::build(const GFXfont* f, int rot) {
  rot &= 3;
  if (isBuilt(f, rot)) return true;
  glyphs_ = nullptr;
  src_ = nullptr;
  used_ = 0;
  if (!f) return false;
  first_ = f->first; last_ = f->last;
  // Pass 1: size. Bitmaps are packed after the glyph table, byte aligned.
  size_t need = sizeof(Glyph) * COUNT;
  for (int c = FIRST; c <= LAST; c++) {
    if (c < first_ || c > last_) continue;
    const GFXglyph& g = f->glyph[c - first_];
    need += Atlas::bytesFor(g.width, g.height, rot);
  }
  if (need > 65535) return false;                        // Glyph::off is 16 bits
  if (need > cap_) {
    free(block_);
    block_ = (uint8_t*)malloc(need);
    cap_ = block_ ? need : 0;
    if (!block_) return false;
  }
  glyphs_ = (Glyph*)(void*)block_;                       // malloc'd: aligned for Glyph
  size_t pos = sizeof(Glyph) * COUNT;
  for (int c = FIRST; c <= LAST; c++) {
    Glyph* out = new (&glyphs_[c - FIRST]) Glyph();
    if (c < first_ || c > last_) continue;
    const GFXglyph& g = f->glyph[c - first_];
    out->xo = g.xOffset; out->yo = g.yOffset; out->xa = g.xAdvance;
    if (g.width == 0 || g.height == 0) continue;
    GlyphBits gb{f->bitmap + g.bitmapOffset, g.width};
    Atlas a;
    a.build(block_ + pos, g.width, g.height, rot, glyphPx, &gb);
    out->off = (uint16_t)pos; out->w = g.width; out->h = g.height;
    pos += Atlas::bytesFor(g.width, g.height, rot);
  }
  src_ = f; rot_ = (uint8_t)rot; yAdv_ = f->yAdvance;
  used_ = need;
  return true;
}

// Cursor handling as GFX's write() (drawing) and charBounds() (measuring). They differ only in the
// wrap test: charBounds applies it to every glyph, write() only to glyphs that have a bitmap.
int drawText(const Bitmap1& bm, int x, int y, const char* s, const Font1& f, DrawMode m, int wrapW) {
  int cx = x, cy = y;
  for (unsigned char c; (c = (unsigned char)*s++);) {
    if (c == '\n') { cx = 0; cy += f.yAdvance(); continue; }
    if (c == '\r') continue;
    const Font1::Glyph* g = f.glyph(c);
    if (!g) continue;
    if (g->w > 0 && g->h > 0) {
      if (wrapW > 0 && cx + g->xo + g->w > wrapW) { cx = 0; cy += f.yAdvance(); }
      f.atlas(*g).draw(bm, cx + g->xo, cy + g->yo, m);
    }
    cx += g->xa;
  }
  return cx;
}

void textBounds(const char* s, const Font1& f, int& x1, int& y1, int& w, int& h, int wrapW) {
  int cx = 0, cy = 0, minx = 0x7FFF, miny = 0x7FFF, maxx = -1, maxy = -1;
  x1 = 0; y1 = 0; w = 0; h = 0;
  for (unsigned char c; (c = (unsigned char)*s++);) {
    if (c == '\n') { cx = 0; cy += f.yAdvance(); continue; }
    if (c == '\r') continue;
    const Font1::Glyph* g = f.glyph(c);
    if (!g) continue;
    int gw = g->w, gh = g->h;
    if (wrapW > 0 && cx + g->xo + gw > wrapW) { cx = 0; cy += f.yAdvance(); }
    int ax = cx + g->xo, ay = cy + g->yo, bx = ax + gw - 1, by = ay + gh - 1;
    if (ax < minx) minx = ax;
    if (ay < miny) miny = ay;
    if (bx > maxx) maxx = bx;
    if (by > maxy) maxy = by;
    cx += g->xa;
  }
  if (maxx >= minx) { x1 = minx; w = maxx - minx + 1; }
  if (maxy >= miny) { y1 = miny; h = maxy - miny + 1; }
}

int textWidth(const char* s, const Font1& f, int wrapW) {
  int x1, y1, w, h;
  textBounds(s, f, x1, y1, w, h, wrapW);
  return w;
}
