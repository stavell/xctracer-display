#pragma once
// Tests for gfx/shapes: line and circle against Adafruit_GFX's own drawLine / drawCircle (every pixel
// call counted, so the XOR mode also checks that no pixel is hit a different number of times), rings
// against the per-row span construction they replaced. Included by gfx_tests.h (namespace gt).

// Adafruit_GFX with a hit counter per logical pixel; draws nothing else.
struct HitCanvas : public Adafruit_GFX {
  std::vector<uint8_t> hits;
  HitCanvas(int w, int h) : Adafruit_GFX((int16_t)w, (int16_t)h), hits((size_t)w * h, 0) {}
  void drawPixel(int16_t x, int16_t y, uint16_t) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    hits[(size_t)y * width() + x]++;
  }
};

// Apply the hit counts to a reference bitmap as `m` would.
static void applyHits(const Bitmap1& b, const HitCanvas& hc, DrawMode m) {
  for (int y = 0; y < hc.height(); y++)
    for (int x = 0; x < hc.width(); x++) {
      int n = hc.hits[(size_t)y * hc.width() + x];
      if (!n) continue;
      if (m == DRAW_INVERT) { if (n & 1) refSet(b, x, y, DRAW_INVERT); }
      else refSet(b, x, y, m);
    }
}

// The row-by-row construction the grey ring used before gfx/shapes: one span per row (two when the row
// crosses the hole), x extents from floor(sqrt()).
static void refRing(const Bitmap1& b, int cx, int cy, int R, int Ri, const ClipBox& cl, DrawMode m, int level) {
  auto span = [&](int x0, int x1, int y) {
    if (y < cl.y0 || y > cl.y1) return;
    if (x0 < cl.x0) x0 = cl.x0;
    if (x1 > cl.x1) x1 = cl.x1;
    for (int x = x0; x <= x1; x++) {
      if (level >= 0) refSet(b, x, y, refWhite(level, x, y) ? DRAW_SET : DRAW_CLEAR);
      else refSet(b, x, y, m);
    }
  };
  if (R < 0 || Ri < 0 || Ri > R) return;
  for (int dy = -R; dy <= R; dy++) {
    int ho = (int)sqrtf((float)(R * R - dy * dy));
    if (abs(dy) >= Ri) { span(cx - ho, cx + ho, cy + dy); continue; }
    int hi = (int)sqrtf((float)(Ri * Ri - dy * dy));
    span(cx - ho, cx - hi - 1, cy + dy);
    span(cx + hi + 1, cx + ho, cy + dy);
  }
}

static void testShapes() {
  puts("shapes: line, circle, ring vs references");
  for (int i = 0; i < 64; i++) {      // the dither table is the closed form
    int x = i & 7, y = i >> 3, v = 0;
    for (int k = 0; k < 3; k++) { v |= (((x ^ y) >> k) & 1) << (5 - 2 * k); v |= ((y >> k) & 1) << (4 - 2 * k); }
    CHECK(kBayer8[i] == v);
  }
  Rng r{777};
  int badLine = 0, badCircle = 0, badRing = 0, badGray = 0, n = 0;
  for (int it = 0; it < 5000; it++) {
    Img a(r, r.range(1, 90), r.range(1, 70), r.range(0, 2), r.range(0, 3));
    Img b(a);
    int lw = a.b.width(), lh = a.b.height();
    DrawMode m = (DrawMode)r.range(0, 2);
    uint16_t col = m == DRAW_SET ? 1 : 0;
    HitCanvas hc(lw, lh);
    int x0 = r.range(-30, lw + 30), y0 = r.range(-30, lh + 30);
    switch (r.range(0, 3)) {
      case 0: {                                           // line, any direction, also axis-parallel and a point
        int x1 = r.range(0, 3) == 0 ? x0 : r.range(-30, lw + 30), y1 = r.range(0, 3) == 0 ? y0 : r.range(-30, lh + 30);
        line(a.b, x0, y0, x1, y1, m);
        hc.drawLine((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, col);
        applyHits(b.b, hc, m);
        if (a.v != b.v) badLine++;
        break;
      }
      case 1: {
        int rad = r.range(0, 60);
        circle(a.b, x0, y0, rad, m);
        hc.drawCircle((int16_t)x0, (int16_t)y0, (int16_t)rad, col);
        applyHits(b.b, hc, m);
        if (a.v != b.v) badCircle++;
        break;
      }
      case 2: {                                           // ring / disc, solid modes, clipped
        int R = r.range(0, 60), Ri = r.range(0, 3) == 0 ? 0 : r.range(0, R);
        ClipBox cl;
        if (r.range(0, 1)) cl = ClipBox(r.range(-10, lw), r.range(-10, lh), r.range(0, lw + 10), r.range(0, lh + 10));
        fillRing(a.b, x0, y0, R, Ri, m, cl);
        refRing(b.b, x0, y0, R, Ri, cl, m, -1);
        if (a.v != b.v) badRing++;
        break;
      }
      default: {                                          // ring, dithered
        int R = r.range(0, 60), Ri = r.range(0, 3) == 0 ? 0 : r.range(0, R), level = r.range(0, 16);
        ClipBox cl;
        if (r.range(0, 1)) cl = ClipBox(r.range(-10, lw), r.range(-10, lh), r.range(0, lw + 10), r.range(0, lh + 10));
        fillRing(a.b, x0, y0, R, Ri, Gray(level), cl);
        refRing(b.b, x0, y0, R, Ri, cl, DRAW_SET, level);
        if (a.v != b.v) badGray++;
        break;
      }
    }
    n++;
  }
  CHECK(badLine == 0);
  CHECK(badCircle == 0);
  CHECK(badRing == 0);
  CHECK(badGray == 0);
  CHECK(n == 5000);
  // degenerate: negative radii and Ri > R draw nothing
  Img e(r, 20, 20, 0, 1), e2(e);
  fillRing(e.b, 10, 10, -1, 0, DRAW_SET); fillRing(e.b, 10, 10, 5, 6, DRAW_INVERT); circle(e.b, 10, 10, -3, DRAW_INVERT);
  CHECK(e.v == e2.v);
}

// lineGrown == line() drawn at every offset of the square, for any direction, clipped, in both draw modes.
static void testThickLine() {
  puts("shapes: lineGrown equals the line at every offset");
  Rng r{4242};
  int bad = 0;
  for (int it = 0; it < 4000; it++) {
    Img a(r, r.range(8, 90), r.range(8, 70), r.range(0, 2), r.range(0, 3));
    Img b(a);
    const int lw = a.b.width(), lh = a.b.height(), g = r.range(0, 3);
    const DrawMode m = r.range(0, 1) ? DRAW_SET : DRAW_CLEAR;
    const int x0 = r.range(-20, lw + 20), y0 = r.range(-20, lh + 20);
    const int x1 = r.range(0, 4) == 0 ? x0 : r.range(-20, lw + 20), y1 = r.range(0, 4) == 0 ? y0 : r.range(-20, lh + 20);
    lineGrown(a.b, x0, y0, x1, y1, g, m);
    for (int oy = -g; oy <= g; oy++)
      for (int ox = -g; ox <= g; ox++) line(b.b, x0 + ox, y0 + oy, x1 + ox, y1 + oy, m);
    if (a.v != b.v) bad++;
  }
  CHECK(bad == 0);
}

// Work budget of the compass page (see `build.sh count`): the grey ring used to cost one native row and
// eight dither evaluations per pixel in portrait, which measured 48 ms on the device. Counts, not time.
static void testCompassWork() {
  puts("compass page: draw work budget");
  static ViewModel vm;
  static CountingSharp disp;
  disp.setRotation(1);
  Bitmap1 bm = disp.bm();
  for (int variant = 0; variant < 3; variant++) {
    vm = ViewModel();
    vm.fresh = variant != 2; vm.link = ViewModel::LINK_SEARCHING;
    vm.heading = 123; vm.headingSrc = HDG_MAG; vm.ambientC = 21; vm.humidityPct = 50;
    vm.localMinutes = 600; vm.localBattPct = 80;
    if (variant == 1) {
      vm.s.fix = true; vm.s.alt = 1400; vm.s.vario = 1.5f; vm.s.speedKmh = 30; vm.heading = vm.s.course = 90;
      vm.headingSrc = HDG_GPS; vm.wind.valid = true; vm.wind.speedKmh = 12; vm.wind.toDeg = 90;
    }
    disp.clearDisplay();
    drawLive(disp, vm, 0, &bm);                // warm-up: glyph sheets
    disp.clearDisplay();
    GfxCounters before = gGfxCnt;
    uint32_t px0 = gCanvasPx;
    drawLive(disp, vm, 0, &bm);
    CHECK(gCanvasPx - px0 == 0);                // no per-pixel virtual calls
    CHECK(gGfxCnt.rows - before.rows < 2500);   // was 7000 and more
    CHECK(gGfxCnt.pixel - before.pixel < 2000);
  }
}
