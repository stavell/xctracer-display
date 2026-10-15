#include "gfx/atlas.h"
#include <string.h>

size_t Atlas::bytesFor(int lw, int lh, int rot) {
  if (lw <= 0 || lh <= 0) return 0;
  int nw = (rot & 1) ? lh : lw, nh = (rot & 1) ? lw : lh;
  return (size_t)((nw + 7) / 8) * (size_t)nh;
}

// The image seen as a Bitmap1 in its own native orientation: gives us the rotation mapping.
static Bitmap1 viewOf(const Atlas& a, uint8_t* p) {
  return Bitmap1{p, a.nw, a.nh, a.stride, a.rot};
}

bool Atlas::build(uint8_t* mem, int w, int h, int r, AtlasPixelFn px, void* ctx) {
  *this = Atlas();
  if (w <= 0 || h <= 0 || w > 32767 || h > 32767) return false;
  rot = (uint8_t)(r & 3);
  lw = (int16_t)w; lh = (int16_t)h;
  nw = (rot & 1) ? lh : lw; nh = (rot & 1) ? lw : lh;
  stride = (uint16_t)((nw + 7) / 8);
  memset(mem, 0, bytesFor(w, h, rot));
  Bitmap1 v = viewOf(*this, mem);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (px(ctx, x, y)) v.pixel(x, y, DRAW_SET);
  data = mem;
  return true;
}

Atlas Atlas::over(const uint8_t* mem, int w, int h, int r) {
  Atlas a;
  a.rot = (uint8_t)(r & 3);
  a.lw = (int16_t)w; a.lh = (int16_t)h;
  a.nw = (a.rot & 1) ? a.lh : a.lw; a.nh = (a.rot & 1) ? a.lw : a.lh;
  a.stride = (uint16_t)((a.nw + 7) / 8);
  a.data = mem;
  return a;
}

uint8_t Atlas::get(int x, int y) const {
  if (!data) return 0;
  return viewOf(*this, const_cast<uint8_t*>(data)).getPixel(x, y);
}

// n <= 8 bits of a row starting at bit position `bit`, LSB first.
static inline unsigned fetchBits(const uint8_t* row, int bit, int n) {
  int i = bit >> 3, off = bit & 7;
  unsigned v = row[i] >> off;
  if (off + n > 8) v |= (unsigned)row[i + 1] << (8 - off);
  return v & ((1u << n) - 1);
}

void Atlas::draw(const Bitmap1& dst, int x, int y, DrawMode m) const {
  if (!data || lw <= 0 || lh <= 0) return;
  if ((dst.rotation & 3) != rot) {                         // other orientation: per pixel
    DrawMode pm = m == DRAW_COPY ? DRAW_SET : m;
    for (int j = 0; j < lh; j++)
      for (int i = 0; i < lw; i++)
        if (get(i, j)) dst.pixel(x + i, y + j, pm);
    return;
  }
  // Native rectangle of the image inside dst (the two corners map to opposite native corners).
  int ax, ay, bx, by;
  dst.toNative(x, y, ax, ay);
  dst.toNative(x + lw - 1, y + lh - 1, bx, by);
  int nx0 = ax < bx ? ax : bx, ny0 = ay < by ? ay : by;
  int sx0 = nx0 < 0 ? -nx0 : 0, sy0 = ny0 < 0 ? -ny0 : 0;          // first source column / row kept
  int cols = nw - sx0, rows = nh - sy0;
  if (nx0 + sx0 + cols > dst.w) cols = dst.w - (nx0 + sx0);
  if (ny0 + sy0 + rows > dst.h) rows = dst.h - (ny0 + sy0);
  if (cols <= 0 || rows <= 0) return;
  const int dx0 = nx0 + sx0, dx1 = dx0 + cols - 1;
  for (int j = 0; j < rows; j++) {
    const uint8_t* srow = data + (size_t)(sy0 + j) * stride;
    uint8_t* drow = dst.buf + (size_t)(ny0 + sy0 + j) * dst.stride;
    for (int bi = dx0 >> 3; bi <= dx1 >> 3; bi++) {
      int lo = bi * 8 > dx0 ? bi * 8 : dx0;
      int hi = bi * 8 + 7 < dx1 ? bi * 8 + 7 : dx1;
      int n = hi - lo + 1;
      unsigned s = fetchBits(srow, sx0 + (lo - dx0), n);
      if (!s) continue;
      uint8_t pm = (uint8_t)(s << (lo & 7));
      if (m == DRAW_CLEAR) drow[bi] &= (uint8_t)~pm;
      else if (m == DRAW_INVERT) drow[bi] ^= pm;
      else drow[bi] |= pm;
    }
  }
}
