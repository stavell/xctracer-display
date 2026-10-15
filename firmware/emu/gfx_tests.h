#pragma once
// Tests for gfx/bitmap1 and the FastSharp canvas: every fast path is compared byte for byte with a
// per-pixel reference. Included by tests.cpp (uses its CHECK macro); runs under the sanitizers, and
// all buffers are exactly sized heap vectors, so an out-of-bounds write aborts the run.
#include <vector>
#include "gfx/bitmap1.h"
#include "hw/fast_sharp.h"

namespace gt {

struct Rng {
  uint32_t s;
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  int range(int lo, int hi) { return lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
};

// ---- the library's pixel path, copied line by line from Adafruit_SharpMem::drawPixel ----
struct RefCanvas : public Adafruit_GFX {
  uint8_t buf[400 * 240 / 8];
  RefCanvas() : Adafruit_GFX(400, 240) { memset(buf, 0xFF, sizeof buf); }
  void refPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height))
      return;
    switch (rotation) {
      case 1: { int16_t t = x; x = y; y = t; x = WIDTH - 1 - x; break; }
      case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
      case 3: { int16_t t = x; x = y; y = t; y = HEIGHT - 1 - y; break; }
    }
    if (color) buf[(y * WIDTH + x) / 8] |= (uint8_t)(1 << (x & 7));
    else buf[(y * WIDTH + x) / 8] &= (uint8_t)~(1 << (x & 7));
  }
  // Spans and rects are Adafruit_GFX's own default implementations on top of drawPixel.
  void drawPixel(int16_t x, int16_t y, uint16_t color) override { refPixel(x, y, color); }
  void clear() { memset(buf, 0xFF, sizeof buf); }
};

static uint16_t color(Rng& r) { int k = r.range(0, 3); return k == 0 ? 0 : k == 1 ? 1 : k == 2 ? 0xFFFF : 0x1234; }

// One random GFX call. Two canvases get the same call by passing copies of the same Rng.
static void gfxOp(Adafruit_GFX& g, Rng& r) {
  int k = r.range(0, 18);
  bool far = r.range(0, 29) == 0;
  auto C = [&]() { return (int16_t)(far ? r.range(-30000, 30000) : r.range(-60, 460)); };
  auto S = [&]() { return (int16_t)(far ? r.range(-40, 40) : (r.range(0, 3) == 0 ? r.range(-8, 8) : r.range(-30, 300))); };
  auto R = [&]() { return (int16_t)r.range(0, 120); };
  uint16_t c = color(r);
  switch (k) {
    case 0: g.drawPixel(C(), C(), c); break;
    case 1: g.drawFastHLine(C(), C(), S(), c); break;
    case 2: g.drawFastVLine(C(), C(), S(), c); break;
    case 3: g.writeFastHLine(C(), C(), S(), c); break;
    case 4: g.writeFastVLine(C(), C(), S(), c); break;
    case 5: g.fillRect(C(), C(), S(), S(), c); break;
    case 6: g.writeFillRect(C(), C(), S(), S(), c); break;
    case 7: g.fillScreen(c); break;
    case 8: g.drawLine(C(), C(), C(), C(), c); break;
    case 9: g.drawRect(C(), C(), S(), S(), c); break;
    case 10: g.drawCircle(C(), C(), R(), c); break;
    case 11: g.fillCircle(C(), C(), R(), c); break;
    case 12: g.fillRoundRect(C(), C(), S(), S(), (int16_t)r.range(0, 30), c); break;
    case 13: far = false; g.drawTriangle(C(), C(), C(), C(), C(), C(), c); break;
    case 14: far = false; g.fillTriangle(C(), C(), C(), C(), C(), C(), c); break;
    case 15: g.drawRoundRect(C(), C(), S(), S(), (int16_t)r.range(0, 30), c); break;
    case 16: {
      g.setCursor(C(), C()); g.setTextSize((uint8_t)r.range(1, 3)); g.setTextColor(c, color(r));
      g.print("Ab1 %q");
      g.setTextSize(1);
      break;
    }
    case 17: g.drawChar(C(), C(), (unsigned char)r.range(32, 126), c, color(r), (uint8_t)r.range(1, 2)); break;
    default: g.fillCircle(C(), C(), R(), c); break;
  }
}

static void randomize(uint8_t* p, size_t n, Rng& r) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)r.next(); }

static bool sameBuf(FastSharp& f, RefCanvas& ref) { return memcmp(f.getBuffer(), ref.buf, sizeof ref.buf) == 0; }

static void testFastSharpRandom(int& checksOut) {
  puts("fast_sharp vs gfx reference: random");
  static RefCanvas ref;
  static FastSharp fast(nullptr, 0, 400, 240, 0);
  Rng r{0x1234567u};
  int ops = 0, bad = 0;
  for (int rot = 0; rot < 4; rot++) {
    for (int round = 0; round < 40; round++) {
      fast.setRotation((uint8_t)rot); ref.setRotation((uint8_t)rot);
      if (round % 2 == 0) { randomize(ref.buf, sizeof ref.buf, r); memcpy(fast.getBuffer(), ref.buf, sizeof ref.buf); }
      for (int i = 0; i < 80; i++) {
        Rng r2 = r;
        gfxOp(fast, r);
        gfxOp(ref, r2);
        ops++;
        if (!sameBuf(fast, ref)) { bad++; if (bad < 4) printf("  mismatch rot %d round %d op %d\n", rot, round, i); break; }
      }
    }
  }
  CHECK(bad == 0);
  CHECK(ops > 10000);
  checksOut += ops;
}

// Systematic edge cases for the primitives GFX turns into spans: every combination of positions around
// the edges and byte boundaries, with widths/heights -3..20 and the full size.
static void testFastSharpEdges() {
  puts("fast_sharp vs gfx reference: edges");
  static RefCanvas ref;
  static FastSharp fast(nullptr, 0, 400, 240, 0);
  Rng r{99};
  int bad = 0, n = 0;
  for (int rot = 0; rot < 4; rot++) {
    fast.setRotation((uint8_t)rot); ref.setRotation((uint8_t)rot);
    int lw = ref.width(), lh = ref.height();
    std::vector<int> xs = {-40, -9, -8, -2, -1, 0, 1, 6, 7, 8, 9, 15, 16, 17, lw - 17, lw - 9, lw - 8, lw - 2, lw - 1, lw, lw + 1, lw + 9};
    std::vector<int> ys = {-5, -1, 0, 1, 7, 8, lh - 2, lh - 1, lh, lh + 1};
    std::vector<int> ls = {-3, -2, -1, 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 20, 64, lw, lh, lw + 30, 450};
    for (int x : xs) for (int y : ys) for (int l : ls) for (int m = 0; m < 6; m++) {
      uint16_t c = (m & 1) ? 1 : 0;
      randomize(ref.buf, sizeof ref.buf, r);
      memcpy(fast.getBuffer(), ref.buf, sizeof ref.buf);
      int16_t X = (int16_t)x, Y = (int16_t)y, L = (int16_t)l;
      switch (m >> 1) {
        case 0: fast.drawFastHLine(X, Y, L, c); ref.drawFastHLine(X, Y, L, c); break;
        case 1: fast.drawFastVLine(Y, X, L, c); ref.drawFastVLine(Y, X, L, c); break;
        default: fast.fillRect(X, Y, L, (int16_t)(l / 2 - 1), c); ref.fillRect(X, Y, L, (int16_t)(l / 2 - 1), c); break;
      }
      n++;
      if (!sameBuf(fast, ref)) { bad++; if (bad < 4) printf("  edge mismatch rot %d x %d y %d l %d m %d\n", rot, x, y, l, m); }
    }
  }
  CHECK(bad == 0);
  CHECK(n > 10000);
}

// ---- Bitmap1 against an independent per-pixel model ----
struct Img {
  std::vector<uint8_t> v;
  Bitmap1 b;
  Img(Rng& r, int w, int h, int extraStride, int rot) {
    int stride = (w + 7) / 8 + extraStride;
    v.assign((size_t)stride * h, 0);
    randomize(v.data(), v.size(), r);
    b = Bitmap1{v.data(), (int16_t)w, (int16_t)h, (uint16_t)stride, (uint8_t)rot};
  }
  Img(const Img& o) : v(o.v), b(o.b) { b.buf = v.data(); }
};

static void refMapNative(const Bitmap1& b, int x, int y, int& nx, int& ny) {
  switch (b.rotation) {
    case 1: nx = b.w - 1 - y; ny = x; break;
    case 2: nx = b.w - 1 - x; ny = b.h - 1 - y; break;
    case 3: nx = y; ny = b.h - 1 - x; break;
    default: nx = x; ny = y;
  }
}
static int refGet(const Bitmap1& b, int x, int y) {
  int lw = (b.rotation & 1) ? b.h : b.w, lh = (b.rotation & 1) ? b.w : b.h;
  if (x < 0 || y < 0 || x >= lw || y >= lh) return 0;
  int nx, ny; refMapNative(b, x, y, nx, ny);
  return (b.buf[ny * b.stride + nx / 8] >> (nx & 7)) & 1;
}
static void refSet(const Bitmap1& b, int x, int y, DrawMode m) {
  int lw = (b.rotation & 1) ? b.h : b.w, lh = (b.rotation & 1) ? b.w : b.h;
  if (x < 0 || y < 0 || x >= lw || y >= lh) return;
  int nx, ny; refMapNative(b, x, y, nx, ny);
  uint8_t& d = b.buf[ny * b.stride + nx / 8];
  uint8_t bit = (uint8_t)(1 << (nx & 7));
  if (m == DRAW_CLEAR) d &= (uint8_t)~bit; else if (m == DRAW_INVERT) d ^= bit; else d |= bit;
}
// Rectangle loops are limited to the logical area; the pixel setter clips anyway.
static void refRect(const Bitmap1& b, int x0, int y0, int x1, int y1, DrawMode m) {
  int lw = (b.rotation & 1) ? b.h : b.w, lh = (b.rotation & 1) ? b.w : b.h;
  for (int y = (y0 < 0 ? 0 : y0); y <= y1 && y < lh; y++)
    for (int x = (x0 < 0 ? 0 : x0); x <= x1 && x < lw; x++) refSet(b, x, y, m);
}

static int rc(Rng& r, int lim) { return r.range(0, 9) == 0 ? r.range(-100000, 100000) : r.range(-12, lim + 12); }

static void testBitmapOps() {
  puts("bitmap1 ops vs per-pixel reference");
  Rng r{4242};
  int bad = 0, bad2 = 0, n = 0;
  for (int it = 0; it < 4000; it++) {
    int w = r.range(1, 70), h = r.range(1, 50);
    Img a(r, w, h, r.range(0, 3), r.range(0, 3));
    Img b(a);
    int lw = a.b.width(), lh = a.b.height();
    for (int k = 0; k < 8; k++) {
      DrawMode m = (DrawMode)r.range(0, 3);
      int x0 = rc(r, lw), x1 = rc(r, lw), y0 = rc(r, lh), y1 = rc(r, lh);
      int len = r.range(-3, 40);
      switch (r.range(0, 6)) {
        case 0: a.b.pixel(x0, y0, m); refSet(b.b, x0, y0, m); break;
        case 1: a.b.hline(x0, x1, y0, m); refRect(b.b, x0, y0, x1, y0, m); break;
        case 2: a.b.vline(x0, y0, y1, m); refRect(b.b, x0, y0, x0, y1, m); break;
        case 3: a.b.rectIncl(x0, y0, x1, y1, m); refRect(b.b, x0, y0, x1, y1, m); break;
        case 4: a.b.hspan(x0, y0, len, m); refRect(b.b, x0, y0, x0 + len - 1, y0, m); break;
        case 5: a.b.vspan(x0, y0, len, m); refRect(b.b, x0, y0, x0, y0 + len - 1, m); break;
        default: a.b.fill(m); refRect(b.b, 0, 0, lw - 1, lh - 1, m); break;
      }
      n++;
      if (a.v != b.v) { bad++; if (bad < 4) printf("  ops mismatch it %d k %d\n", it, k); break; }
    }
    // getPixel everywhere, outside included
    for (int y = -2; y < lh + 2; y++)
      for (int x = -2; x < lw + 2; x++)
        if (a.b.getPixel(x, y) != refGet(a.b, x, y)) bad2++;
  }
  CHECK(bad == 0);
  CHECK(bad2 == 0);
  CHECK(n > 20000);
}

// fillRect with explicit width and height (the case above only checks that it does not crash).
static void testBitmapFillRect() {
  puts("bitmap1 fillRect");
  Rng r{777};
  int bad = 0;
  for (int it = 0; it < 6000; it++) {
    Img a(r, r.range(1, 70), r.range(1, 50), r.range(0, 2), r.range(0, 3));
    Img b(a);
    int lw = a.b.width(), lh = a.b.height();
    int x = rc(r, lw), y = rc(r, lh), rw = r.range(-3, lw + 10), rh = r.range(-3, lh + 10);
    DrawMode m = (DrawMode)r.range(0, 3);
    a.b.fillRect(x, y, rw, rh, m);
    if (rw > 0 && rh > 0) refRect(b.b, x, y, x + rw - 1, y + rh - 1, m);
    if (a.v != b.v) bad++;
  }
  CHECK(bad == 0);
}

static void testBlit() {
  puts("bitmap1 blit vs per-pixel reference");
  Rng r{31337};
  int bad = 0, n = 0, fastN = 0;
  for (int it = 0; it < 8000; it++) {
    int dw = r.range(1, 80), dh = r.range(1, 40);
    int rotD = r.range(0, 3) == 0 ? r.range(0, 3) : 0;
    Img d(r, dw, dh, r.range(0, 3), rotD);
    Img d2(d);
    int sw = r.range(1, 60), sh = r.range(1, 40);
    int rotS = r.range(0, 5) == 0 ? r.range(0, 3) : 0;
    Img s(r, sw, sh, r.range(0, 3), rotS);
    bool useMask = r.range(0, 2) == 0;
    Img mk(r, r.range(1, 70), r.range(1, 50), r.range(0, 2), r.range(0, 7) == 0 ? r.range(0, 3) : 0);
    int x = r.range(-30, d.b.width() + 30), y = r.range(-20, d.b.height() + 20);
    int sx = r.range(-10, s.b.width() + 10), sy = r.range(-10, s.b.height() + 10);
    int w = r.range(-3, 90), h = r.range(-3, 50);
    DrawMode m = (DrawMode)r.range(0, 3);
    BlitKey key = (BlitKey)r.range(0, 2);
    if (rotD == 0 && rotS == 0) fastN++;
    Bitmap1::blit(d.b, x, y, s.b, sx, sy, w, h, m, key, useMask ? &mk.b : nullptr);
    // reference
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int px = sx + i, py = sy + j;
        if (px < 0 || py < 0 || px >= s.b.width() || py >= s.b.height()) continue;
        int sv = refGet(s.b, px, py);
        if (key == SKIP_BLACK && !sv) continue;
        if (key == SKIP_WHITE && sv) continue;
        if (useMask && !refGet(mk.b, px, py)) continue;
        refSet(d2.b, x + i, y + j, m == DRAW_COPY ? (sv ? DRAW_SET : DRAW_CLEAR) : m);
      }
    n++;
    if (d.v != d2.v) { bad++; if (bad < 4) printf("  blit mismatch it %d (x %d y %d sx %d sy %d w %d h %d m %d key %d mask %d)\n", it, x, y, sx, sy, w, h, m, key, useMask); }
  }
  CHECK(bad == 0);
  CHECK(n == 8000);
  CHECK(fastN > 4000);     // most cases take the whole-byte path
}

// ---- whole screens: the same frame through the library's pixel path and through FastSharp ----
static void testScreensIdentical() {
  puts("fast_sharp vs gfx reference: compass and thermal screens");
  static ViewModel vm;
  vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED;
  vm.s.fix = true; vm.s.alt = 1400; vm.s.vario = 1.5f; vm.s.speedKmh = 30; vm.s.course = 90;
  vm.heading = 90; vm.headingSrc = HDG_GPS;
  vm.wind.valid = true; vm.wind.speedKmh = 12; vm.wind.toDeg = 90;
  vm.localMinutes = 600; vm.localBattPct = 80;
  vm.trailCount = 60;
  for (int i = 0; i < vm.trailCount; i++) {
    vm.trail[i].x = 40 * sinf(i * 0.3f); vm.trail[i].y = 40 * cosf(i * 0.3f); vm.trail[i].vario = 1.5f;
  }
  vm.markCount = 1; vm.marks[0].x = 30; vm.marks[0].y = 20; vm.marks[0].avgClimb = 2.1f;
  static RefCanvas ref;
  static FastSharp fast(nullptr, 0, 400, 240, 0);
  for (int rot = 0; rot < 4; rot++) {
    fast.setRotation((uint8_t)rot); ref.setRotation((uint8_t)rot);
    for (int page = 0; page < 2; page++) {
      fast.clearDisplay(); ref.clear();
      vm.page = page == 0 ? ViewModel::PAGE_COMPASS : ViewModel::PAGE_THERMAL;
      drawLive(fast, vm, 0);
      drawLive(ref, vm, 0);
      CHECK(sameBuf(fast, ref));
      int white = 0;
      for (size_t i = 0; i < sizeof ref.buf; i++) white += __builtin_popcount(ref.buf[i]);
      CHECK(white > 1000 && white < 400 * 240 - 1000);       // a real frame, not two blank buffers
    }
  }
}

static void run(int& totalOps) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  testBitmapOps();
  testBitmapFillRect();
  testBlit();
  testFastSharpEdges();
  testFastSharpRandom(totalOps);
  testScreensIdentical();
}

}  // namespace gt
