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

// Built fonts are shared: every app built its own copies of the same few fonts (2 to 10 kB of heap each), and with the
// flight sim in the box image the heap ran out, so the sim's build() failed and its dialogs drew without text. A block
// is keyed by the font's content (each translation unit has its own copy of an Adafruit font array, so pointers differ)
// and the rotation, and freed when its last user lets go.
struct FontBlock {
  uint32_t key = 0;
  uint8_t rot = 0;
  uint16_t refs = 0;
  uint8_t* mem = nullptr;
  size_t used = 0;
};
static FontBlock gBlocks[16];

static uint32_t fontKey(const GFXfont* f) {
  uint32_t h = 2166136261u;
  auto mix = [&h](const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  };
  const uint16_t hdr[3] = {f->first, f->last, f->yAdvance};
  mix(hdr, sizeof hdr);
  size_t bits = 0;
  for (int c = f->first; c <= f->last; c++) {
    const GFXglyph& g = f->glyph[c - f->first];
    mix(&g, sizeof g);
    const size_t end = g.bitmapOffset + ((size_t)g.width * g.height + 7) / 8;
    if (end > bits) bits = end;
  }
  mix(f->bitmap, bits);
  return h;
}

void Font1::release() {
  if (slot_ >= 0) {
    FontBlock& b = gBlocks[slot_];
    if (--b.refs == 0) { free(b.mem); b = FontBlock(); }
  }
  slot_ = -1;
  block_ = nullptr;
  glyphs_ = nullptr;
  src_ = nullptr;
  used_ = 0;
}

Font1::~Font1() { release(); }

const Font1::Glyph* Font1::glyph(unsigned char c) const {
  if (!glyphs_ || c < FIRST || c > LAST || c < first_ || c > last_) return nullptr;
  return &glyphs_[c - FIRST];
}

bool Font1::build(const GFXfont* f, int rot) {
  rot &= 3;
  if (isBuilt(f, rot)) return true;
  release();
  if (!f) return false;
  first_ = f->first; last_ = f->last;
  const uint32_t key = fontKey(f);
  int freeSlot = -1;
  for (int i = 0; i < (int)(sizeof gBlocks / sizeof gBlocks[0]); i++) {
    FontBlock& b = gBlocks[i];
    if (b.refs && b.key == key && b.rot == rot) {         // someone built it already
      b.refs++;
      slot_ = (int8_t)i;
      block_ = b.mem;
      glyphs_ = (Glyph*)(void*)block_;
      src_ = f; rot_ = (uint8_t)rot; yAdv_ = f->yAdvance;
      used_ = b.used;
      return true;
    }
    if (!b.refs && freeSlot < 0) freeSlot = i;
  }
  if (freeSlot < 0) return false;
  // Pass 1: size. Bitmaps are packed after the glyph table, byte aligned.
  size_t need = sizeof(Glyph) * COUNT;
  for (int c = FIRST; c <= LAST; c++) {
    if (c < first_ || c > last_) continue;
    const GFXglyph& g = f->glyph[c - first_];
    need += Atlas::bytesFor(g.width, g.height, rot);
  }
  if (need > 65535) return false;                        // Glyph::off is 16 bits
  block_ = (uint8_t*)malloc(need);
  if (!block_) return false;
  FontBlock& b = gBlocks[freeSlot];
  b.key = key; b.rot = (uint8_t)rot; b.refs = 1; b.mem = block_; b.used = need;
  slot_ = (int8_t)freeSlot;
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
