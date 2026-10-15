#include "gfx/gray.h"

// The 8 pixels of the native byte at native row ny, as dithered at their screen positions. The screen
// position of native bit k of any byte depends only on k (bytes start at multiples of 8), so one value
// serves the whole row.
static uint8_t rowPattern(const Bitmap1& bm, int ny, int level) {
  int thr = 4 * level;
  uint8_t p = 0;
  for (int k = 0; k < 8; k++) {
    int lx, ly;                          // inverse of Bitmap1::toNative, for native (k, ny) modulo 8
    switch (bm.rotation & 3) {
      case 1: lx = ny; ly = bm.w - 1 - k; break;
      case 2: lx = bm.w - 1 - k; ly = bm.h - 1 - ny; break;
      case 3: lx = bm.h - 1 - ny; ly = k; break;
      default: lx = k; ly = ny; break;
    }
    if (bayer8(lx & 7, ly & 7) < thr) p |= (uint8_t)(1u << k);
  }
  return p;
}

void rectIncl(const Bitmap1& bm, int x0, int y0, int x1, int y1, Gray g) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= bm.width()) x1 = bm.width() - 1;
  if (y1 >= bm.height()) y1 = bm.height() - 1;
  if (x0 > x1 || y0 > y1) return;
  int ax, ay, bx, by;
  bm.toNative(x0, y0, ax, ay);
  bm.toNative(x1, y1, bx, by);
  int nx0 = ax < bx ? ax : bx, nx1 = ax < bx ? bx : ax;
  int ny0 = ay < by ? ay : by, ny1 = ay < by ? by : ay;
  int b0 = nx0 >> 3, b1 = nx1 >> 3;
  uint8_t first = (uint8_t)(0xFFu << (nx0 & 7));
  uint8_t last = (uint8_t)(0xFFu >> (7 - (nx1 & 7)));
  for (int y = ny0; y <= ny1; y++) {
    uint8_t* row = bm.buf + (size_t)y * bm.stride;
    uint8_t pat = rowPattern(bm, y, g.level);
    for (int b = b0; b <= b1; b++) {
      uint8_t m = 0xFF;
      if (b == b0) m &= first;
      if (b == b1) m &= last;
      row[b] = (uint8_t)((row[b] & ~m) | (pat & m));
    }
  }
}

static inline int nibble(const Gray4& s, int x, int y) {
  uint8_t v = s.buf[(size_t)y * s.stride + (x >> 1)];
  return (x & 1) ? (v & 15) : (v >> 4);
}

static inline bool ditherWhite(int nib, int x, int y) {
  int level = (nib * 16 + 7) / 15;
  return bayer8(x & 7, y & 7) < 4 * level;
}

void blitGray(const Bitmap1& dst, int x, int y, const Gray4& src, int sx, int sy, int w, int h,
              int transparent) {
  if (w <= 0 || h <= 0) return;
  // clip against the destination and the source
  if (x < 0) { sx -= x; w += x; x = 0; }
  if (sx < 0) { x -= sx; w += sx; sx = 0; }
  if (y < 0) { sy -= y; h += y; y = 0; }
  if (sy < 0) { y -= sy; h += sy; sy = 0; }
  if (x + w > dst.width()) w = dst.width() - x;
  if (sx + w > src.w) w = src.w - sx;
  if (y + h > dst.height()) h = dst.height() - y;
  if (sy + h > src.h) h = src.h - sy;
  if (w <= 0 || h <= 0) return;

  if (dst.rotation != 0) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int n = nibble(src, sx + i, sy + j);
        if (n == transparent) continue;
        dst.pixel(x + i, y + j, ditherWhite(n, x + i, y + j) ? DRAW_SET : DRAW_CLEAR);
      }
    return;
  }
  // rotation 0: build each destination byte from up to 8 pixels, one read-modify-write per byte
  for (int j = 0; j < h; j++) {
    uint8_t* row = dst.buf + (size_t)(y + j) * dst.stride;
    for (int bi = x >> 3; bi <= (x + w - 1) >> 3; bi++) {
      int lo = bi * 8 > x ? bi * 8 : x;
      int hi = bi * 8 + 7 < x + w - 1 ? bi * 8 + 7 : x + w - 1;
      uint8_t m = 0, v = 0;
      for (int px = lo; px <= hi; px++) {
        int n = nibble(src, sx + (px - x), sy + j);
        if (n == transparent) continue;
        m |= (uint8_t)(1u << (px & 7));
        if (ditherWhite(n, px, y + j)) v |= (uint8_t)(1u << (px & 7));
      }
      row[bi] = (uint8_t)((row[bi] & ~m) | v);
    }
  }
}
