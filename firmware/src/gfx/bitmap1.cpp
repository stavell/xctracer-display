#include "gfx/bitmap1.h"
#include <string.h>

static inline void applyMask(uint8_t& b, uint8_t mask, DrawMode m) {
  switch (m) {
    case DRAW_CLEAR: b &= (uint8_t)~mask; break;
    case DRAW_INVERT: b ^= mask; break;
    default: b |= mask; break;     // DRAW_SET, DRAW_COPY
  }
}

// Same mapping as Adafruit_SharpMem::drawPixel, with the bitmap's own native size.
void Bitmap1::toNative(int x, int y, int& nx, int& ny) const {
  switch (rotation & 3) {
    case 1: nx = w - 1 - y; ny = x; break;
    case 2: nx = w - 1 - x; ny = h - 1 - y; break;
    case 3: nx = y; ny = h - 1 - x; break;
    default: nx = x; ny = y; break;
  }
}

// Native rectangle, already inside the bitmap: partial masks at the ends of each row, whole bytes between.
void Bitmap1::nativeRect(int nx0, int ny0, int nx1, int ny1, DrawMode m) const {
  int b0 = nx0 >> 3, b1 = nx1 >> 3;
  uint8_t first = (uint8_t)(0xFFu << (nx0 & 7));
  uint8_t last = (uint8_t)(0xFFu >> (7 - (nx1 & 7)));
  for (int y = ny0; y <= ny1; y++) {
    uint8_t* row = buf + (size_t)y * stride;
    if (b0 == b1) { applyMask(row[b0], first & last, m); continue; }
    applyMask(row[b0], first, m);
    if (b1 - b0 > 1) {
      if (m == DRAW_CLEAR) memset(row + b0 + 1, 0x00, (size_t)(b1 - b0 - 1));
      else if (m == DRAW_INVERT) for (int i = b0 + 1; i < b1; i++) row[i] ^= 0xFF;
      else memset(row + b0 + 1, 0xFF, (size_t)(b1 - b0 - 1));
    }
    applyMask(row[b1], last, m);
  }
}

void Bitmap1::rectIncl(int x0, int y0, int x1, int y1, DrawMode m) const {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= width()) x1 = width() - 1;
  if (y1 >= height()) y1 = height() - 1;
  if (x0 > x1 || y0 > y1) return;
  int ax, ay, bx, by;
  toNative(x0, y0, ax, ay);
  toNative(x1, y1, bx, by);
  nativeRect(ax < bx ? ax : bx, ay < by ? ay : by, ax < bx ? bx : ax, ay < by ? by : ay, m);
}

void Bitmap1::hline(int x0, int x1, int y, DrawMode m) const { rectIncl(x0, y, x1, y, m); }
void Bitmap1::vline(int x, int y0, int y1, DrawMode m) const { rectIncl(x, y0, x, y1, m); }

uint8_t Bitmap1::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width() || y >= height()) return 0;
  int nx, ny;
  toNative(x, y, nx, ny);
  return (buf[(size_t)ny * stride + (nx >> 3)] >> (nx & 7)) & 1;
}

// n <= 8 bits of a row starting at bit position `bit`, LSB first.
static inline unsigned fetchBits(const uint8_t* row, int bit, int n) {
  int i = bit >> 3, off = bit & 7;
  unsigned v = row[i] >> off;
  if (off + n > 8) v |= (unsigned)row[i + 1] << (8 - off);
  return v & ((1u << n) - 1);
}

static inline void clipAxis(int& d, int& s, int& n, int dmax, int smax) {
  if (d < 0) { s -= d; n += d; d = 0; }
  if (s < 0) { d -= s; n += s; s = 0; }
  if (d + n > dmax) n = dmax - d;
  if (s + n > smax) n = smax - s;
}

void Bitmap1::blit(const Bitmap1& dst, int x, int y, const Bitmap1& src, int sx, int sy, int w, int h,
                   DrawMode m, BlitKey key, const Bitmap1* mask) {
  if (w <= 0 || h <= 0) return;
  clipAxis(x, sx, w, dst.width(), src.width());
  clipAxis(y, sy, h, dst.height(), src.height());
  if (w <= 0 || h <= 0) return;
  if (mask) {
    if (sx + w > mask->width()) w = mask->width() - sx;
    if (sy + h > mask->height()) h = mask->height() - sy;
    if (w <= 0 || h <= 0) return;
  }

  bool fast = dst.rotation == 0 && src.rotation == 0 && (!mask || mask->rotation == 0);
  if (!fast) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        uint8_t s = src.getPixel(sx + i, sy + j);
        if (key == SKIP_BLACK && !s) continue;
        if (key == SKIP_WHITE && s) continue;
        if (mask && !mask->getPixel(sx + i, sy + j)) continue;
        DrawMode pm = m == DRAW_COPY ? (s ? DRAW_SET : DRAW_CLEAR) : m;
        dst.pixel(x + i, y + j, pm);
      }
    return;
  }
  for (int j = 0; j < h; j++) {
    uint8_t* drow = dst.buf + (size_t)(y + j) * dst.stride;
    const uint8_t* srow = src.buf + (size_t)(sy + j) * src.stride;
    const uint8_t* mrow = mask ? mask->buf + (size_t)(sy + j) * mask->stride : nullptr;
    int dx1 = x + w - 1;
    for (int bi = x >> 3; bi <= dx1 >> 3; bi++) {
      int lo = bi * 8 > x ? bi * 8 : x;
      int hi = bi * 8 + 7 < dx1 ? bi * 8 + 7 : dx1;
      int n = hi - lo + 1, shift = lo & 7;
      unsigned s = fetchBits(srow, sx + (lo - x), n);
      unsigned part = (1u << n) - 1;                       // pixels taking part
      if (key == SKIP_BLACK) part &= s;
      else if (key == SKIP_WHITE) part &= ~s;
      if (mrow) part &= fetchBits(mrow, sx + (lo - x), n);
      uint8_t pm = (uint8_t)(part << shift), sv = (uint8_t)(s << shift);
      uint8_t& d = drow[bi];
      switch (m) {
        case DRAW_COPY: d = (uint8_t)((d & ~pm) | (sv & pm)); break;
        case DRAW_CLEAR: d &= (uint8_t)~pm; break;
        case DRAW_INVERT: d ^= pm; break;
        default: d |= pm; break;
      }
    }
  }
}
