#include "platform/map/map_view.h"
#include <math.h>
#include <string.h>
#include "platform/gfx/gray.h"

using namespace terrain;

namespace mapview {

uint32_t (*MapView::clock_)() = nullptr;

namespace {

constexpr float kVisPx = 45.3f;                        // half the diagonal of a tile: a tile whose centre is nearer the screen than this shows
constexpr int kHalf = MapView::kTile / 2;

// Black where the Bayer threshold is at or above 4 x the level; nibble 15 is level 16 (white: the light style's level ground).
const uint8_t kThr[16] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 64};

}  // namespace

void MapView::reset() {
  for (int i = 0; i < kPool; i++) slot_[i].state = EMPTY;
  job_ = -1;
  gen_++;
}

void MapView::setScale(float mPerPx) {
  if (!(mPerPx > 0) || mPerPx == m_) return;
  double lat, lon;
  toLatLon(cx_, cy_, &lat, &lon);
  m_ = mPerPx;
  toGlobal(lat, lon, &cx_, &cy_);
  job_ = -1;
  gen_++;                                              // every tile is of the old scale
}

void MapView::setHeading(float deg) {
  if (!isfinite(deg)) deg = 0;
  deg = fmodf(deg, 360.0f);
  if (deg < 0) deg += 360.0f;
  hdg_ = deg;
  if (deg == 0) { ct_ = 1; sn_ = 0; return; }          // exactly: north up takes the whole-pixel path
  const float r = deg * 0.017453292f;
  ct_ = cosf(r);
  sn_ = sinf(r);
}

void MapView::centreOn(double lat, double lon) {
  float x, y;
  if (toGlobal(lat, lon, &x, &y)) { cx_ = x; cy_ = y; }
}

void MapView::pan(float dx, float dy) {
  cx_ += dx * ct_ - dy * sn_;
  cy_ += dx * sn_ + dy * ct_;
}

void MapView::snap() {
  cx_ = floorf(cx_ + 0.5f);
  cy_ = floorf(cy_ + 0.5f);
}

bool MapView::toGlobal(double lat, double lon, float* gx, float* gy) const {
  if (!terr_ || !terr_->isOpen() || !(fabs(lat) <= 90 && fabs(lon) <= 180)) return false;
  const PackInfo& I = terr_->info();
  const double k = (double)I.mPerDeg / m_;
  *gx = (float)((lon - I.originLon_e7 * 1e-7) * I.cos0 * k);
  *gy = (float)((I.originLat_e7 * 1e-7 - lat) * k);
  return true;
}

void MapView::toLatLon(float gx, float gy, double* lat, double* lon) const {
  if (!terr_ || !terr_->isOpen()) { *lat = *lon = 0; return; }
  const PackInfo& I = terr_->info();
  const double k = m_ / (double)I.mPerDeg;
  *lat = I.originLat_e7 * 1e-7 - gy * k;
  *lon = I.originLon_e7 * 1e-7 + gx * k / I.cos0;
}

void MapView::toScreen(float gx, float gy, float* sx, float* sy) const {
  const float dx = gx - cx_, dy = gy - cy_;
  *sx = dx * ct_ + dy * sn_ + aw_ * 0.5f;
  *sy = -dx * sn_ + dy * ct_ + ah_ * 0.5f;
}

void MapView::toGlobalFromScreen(float sx, float sy, float* gx, float* gy) const {
  const float dx = sx - aw_ * 0.5f, dy = sy - ah_ * 0.5f;
  *gx = cx_ + dx * ct_ - dy * sn_;
  *gy = cy_ + dx * sn_ + dy * ct_;
}

Window MapView::tileWindow(int tx, int ty, int px, float mPerPx) const {
  Window w;
  toLatLon((float)(tx * kTile + kHalf), (float)(ty * kTile + kHalf), &w.lat, &w.lon);
  w.mPerPx = mPerPx;
  w.w = w.h = (int16_t)px;
  w.headingDeg = 0;
  w.cosLat = terr_->info().cos0;
  return w;
}

Window MapView::viewWindow() const {
  Window w;
  toLatLon(cx_, cy_, &w.lat, &w.lon);
  w.mPerPx = m_;
  w.w = (int16_t)aw_;
  w.h = (int16_t)ah_;
  w.headingDeg = hdg_;
  w.cosLat = terr_->info().cos0;
  return w;
}

float MapView::tileEdge(int tx, int ty) const {
  const float gx = (float)(tx * kTile + kHalf) - cx_, gy = (float)(ty * kTile + kHalf) - cy_;
  const float dx = fabsf(gx * ct_ + gy * sn_) - aw_ * 0.5f, dy = fabsf(-gx * sn_ + gy * ct_) - ah_ * 0.5f;
  const float e = dx > dy ? dx : dy;
  return e > 0 ? e : 0;
}

MapView::Box MapView::boxFor(float margin) const {
  const float hx = aw_ * 0.5f + margin, hy = ah_ * 0.5f + margin;
  const float ex = hx * fabsf(ct_) + hy * fabsf(sn_), ey = hx * fabsf(sn_) + hy * fabsf(ct_);
  Box b;
  b.tx0 = (int)floorf((cx_ - ex) / kTile);
  b.ty0 = (int)floorf((cy_ - ey) / kTile);
  b.tw = (int)floorf((cx_ + ex) / kTile) - b.tx0 + 1;
  b.th = (int)floorf((cy_ + ey) / kTile) - b.ty0 + 1;
  if (b.tw > kTab) { b.tx0 += (b.tw - kTab) / 2; b.tw = kTab; }   // cannot happen for a 240 x 400 screen; keeps the table bounded
  if (b.th > kTab) { b.ty0 += (b.th - kTab) / 2; b.th = kTab; }
  return b;
}

void MapView::table(const Box& b, const Slot** tab) const {
  for (int i = 0; i < b.tw * b.th; i++) tab[i] = nullptr;
  for (int i = 0; i < kPool; i++) {
    const Slot& s = slot_[i];
    if (s.state != READY || s.gen != gen_) continue;
    const int x = s.tx - b.tx0, y = s.ty - b.ty0;
    if (x >= 0 && x < b.tw && y >= 0 && y < b.th) tab[y * b.tw + x] = &s;
  }
}

// The order tiles are rendered in: the ones on the screen from the centre outwards, then the margin, nearest first.
static float priority(float edge, float dx, float dy) { return edge < kVisPx ? dx * dx + dy * dy : 1e9f + edge; }

bool MapView::pickTile(int* tx, int* ty, float* key) const {
  const Box b = boxFor(kVisPx + kPrefetchPx);
  const Slot** tab = tab_;                             // a member: the loop task has 4 KB of stack
  table(b, tab);
  float best = 3e9f;
  for (int y = 0; y < b.th; y++)
    for (int x = 0; x < b.tw; x++) {
      if (tab[y * b.tw + x]) continue;
      const int ttx = b.tx0 + x, tty = b.ty0 + y;
      const float e = tileEdge(ttx, tty);
      if (e >= kVisPx + kPrefetchPx) continue;
      const float p = priority(e, ttx * kTile + kHalf - cx_, tty * kTile + kHalf - cy_);
      if (p < best) { best = p; *tx = ttx; *ty = tty; }
    }
  *key = best;
  return best < 3e9f;
}

// A free slot, or the one holding the least wanted tile when that is less wanted than the new one (-1: keep them all).
int MapView::pickSlot(float key) const {
  int worst = -1;
  float worstKey = key;
  for (int i = 0; i < kPool; i++) {
    const Slot& s = slot_[i];
    if (s.state == EMPTY || s.gen != gen_) return i;
    if (s.state != READY) continue;
    const float e = tileEdge(s.tx, s.ty);
    const float p = priority(e, s.tx * kTile + kHalf - cx_, s.ty * kTile + kHalf - cy_);
    if (p > worstKey) { worstKey = p; worst = i; }
  }
  return worst;
}

void MapView::startTile(int i, int tx, int ty) {
  Slot& s = slot_[i];
  s.tx = tx;
  s.ty = ty;
  s.gen = gen_;
  s.state = BUILDING;
  memset(s.ink, 0, sizeof s.ink);
  memset(s.grey, 0xFF, sizeof s.grey);
  job_ = i;
  phase_ = 0;
  jy_ = 0;
}

bool MapView::unit() {
  Slot& s = slot_[job_];
  // Stronger than the vario's light style (darkest 10): the map has nothing else to show, so the relief may speak. Level ground stays
  // white and the darkest face at level 6 keeps contours, roads and names readable on it (darkest 3 drowned them; NOTES, "Map app").
  ReliefStyle st = ReliefStyle::light();
  st.exaggeration = 2.0f;
  st.knee = 0.10f;
  st.darkest = 6.0f;
  if (phase_ == 0) {                                   // hillshade, half resolution
    const int n = kTile / 2, y1 = jy_ + kRows < n ? jy_ + kRows : n;
    terr_->reliefStrip(tileWindow(s.tx, s.ty, n, 2 * m_), st, 0, 0, n, jy_, y1, greyRows_, nullptr);
    for (int r = jy_; r < y1; r++)
      for (int c = 0; c < n; c++) {
        const uint8_t g = greyRows_[(r - jy_) * n + c];
        uint8_t v = g >= 15 ? 15 : g;
        if (g == kNoShade) {                           // no data: a 50 % checkerboard in the ink, like the vario's map page
          v = 15;
          for (int k = 0; k < 4; k++) {
            const int x = 2 * c + (k & 1), y = 2 * r + (k >> 1);
            if ((x ^ y) & 1) s.ink[y * (kTile / 8) + (x >> 3)] |= (uint8_t)(1u << (x & 7));
          }
        }
        uint8_t& b = s.grey[r * (kTile / 4) + (c >> 1)];
        b = (c & 1) ? (uint8_t)((b & 0x0F) | (v << 4)) : (uint8_t)((b & 0xF0) | v);
      }
    jy_ = y1;
    if (jy_ >= n) { phase_ = 1; jy_ = 0; }
    return false;
  }
  const int n = kTile, y1 = jy_ + kRows < n ? jy_ + kRows : n;
  terr_->reliefStrip(tileWindow(s.tx, s.ty, n, m_), st, 0, 0, n, jy_, y1, nullptr, contRows_);
  // Contours dotted (every other pixel, on the ground's grid), never solid, so they cannot pass for a road; an index contour
  // gets a second row of dots (dropped at the tile's right and bottom edge: one dot in 64).
  for (int r = jy_; r < y1; r++)
    for (int c = 0; c < n; c++) {
      const uint8_t k = contRows_[(r - jy_) * n + c];
      if (!k) continue;
      if (((c + r) & 1) == 0) s.ink[r * 8 + (c >> 3)] |= (uint8_t)(1u << (c & 7));
      if (k == CONTOUR_INDEX) {
        if (((c + r) & 1) && c + 1 < n) s.ink[r * 8 + ((c + 1) >> 3)] |= (uint8_t)(1u << ((c + 1) & 7));
        if (((c + r) & 1) && r + 1 < n) s.ink[(r + 1) * 8 + (c >> 3)] |= (uint8_t)(1u << (c & 7));
      }
    }
  jy_ = y1;
  if (jy_ < n) return false;
  s.state = READY;
  job_ = -1;
  tiles_++;
  return tileEdge(s.tx, s.ty) < kVisPx;               // a tile of the margin changes nothing on the screen
}

bool MapView::step(uint32_t budgetUs) {
  if (!hasData()) return false;
  bool done = false;
  const uint32_t t0 = clock_ ? clock_() : 0;
  for (int n = 1;; n++) {
    if (job_ < 0) {
      int tx = 0, ty = 0;
      float key;
      if (!pickTile(&tx, &ty, &key)) break;
      const int s = pickSlot(key);
      if (s < 0) break;
      startTile(s, tx, ty);
    }
    if (unit()) done = true;
    if (clock_ ? clock_() - t0 >= budgetUs : n >= kUnitsNoClock) break;
  }
  return done;
}

int MapView::missing() const {
  if (!hasData()) return 0;
  const Box b = boxFor(2);
  const Slot** tab = tab_;                             // a member: the loop task has 4 KB of stack
  table(b, tab);
  int n = 0;
  for (int y = 0; y < b.th; y++)
    for (int x = 0; x < b.tw; x++)
      if (!tab[y * b.tw + x] && tileEdge(b.tx0 + x, b.ty0 + y) < kVisPx) n++;
  return n;
}

// Every pixel of the area: the tile's ink, else its grey dithered. A tile not rendered yet shows a sparse dot grid.
void MapView::drawTiles(const Bitmap1& bm, int ax, int ay) {
  const Box b = boxFor(2);
  const Slot** tab = tab_;                             // a member: the loop task has 4 KB of stack
  table(b, tab);
  const bool north = ct_ == 1 && sn_ == 0;
  const float ox = (float)(b.tx0 * kTile), oy = (float)(b.ty0 * kTile);
  const int32_t du = (int32_t)lroundf(-sn_ * 65536.0f), dv = (int32_t)lroundf(ct_ * 65536.0f);
  const unsigned tw = (unsigned)b.tw, th = (unsigned)b.th;
  const bool fast = (bm.rotation & 3) == 1;
  for (int x = 0; x < aw_; x++) {
    const int X = ax + x;
    if ((unsigned)X >= (unsigned)bm.width()) continue;
    const float dx = x + 0.5f - aw_ * 0.5f, dy = 0.5f - ah_ * 0.5f;
    int32_t u = (int32_t)floorf((cx_ + dx * ct_ - dy * sn_ - ox) * 65536.0f);
    int32_t v = (int32_t)floorf((cy_ + dx * sn_ + dy * ct_ - oy) * 65536.0f);
    uint8_t* row = bm.buf + (unsigned)X * bm.stride;
    for (int y = 0; y < ah_; y++, u += du, v += dv) {
      const int Y = ay + y;
      const int ix = u >> 16, iy = v >> 16;
      const unsigned tx = (unsigned)(ix >> 6), ty = (unsigned)(iy >> 6);
      const Slot* s = tx < tw && ty < th ? tab[ty * tw + tx] : nullptr;
      const int dxp = north ? ix : X, dyp = north ? iy : Y;
      bool black;
      if (s) {
        const int lx = ix & 63, ly = iy & 63;
        black = (s->ink[ly * 8 + (lx >> 3)] >> (lx & 7)) & 1;
        if (!black) {
          const int g = (s->grey[(ly >> 1) * 16 + (lx >> 2)] >> ((lx & 2) << 1)) & 15;
          black = bayer8(dxp, dyp) >= kThr[g];
        }
      } else {
        black = (dxp & 7) == 0 && (dyp & 7) == 0;
      }
      if (!black || (unsigned)Y >= (unsigned)bm.height()) continue;
      if (fast) {
        const int nx = bm.w - 1 - Y;
        row[nx >> 3] &= (uint8_t)~(1u << (nx & 7));
      } else {
        bm.pixel(X, Y, DRAW_CLEAR);
      }
    }
  }
}

void MapView::draw(const Bitmap1& bm, int ax, int ay, const Font1* font, const mapdraw::Rect* reserved, int nReserved) {
  if (!hasData()) return;
  drawTiles(bm, ax, ay);
  if (!vec_) return;
  const Window w = viewWindow();
  const VecXform xf = vec_->makeXform(w);
  const UnitBox box = vec_->boxForWindow(w);
  if (vec_->hasRoads())
    for (int pass = 0; pass < 2; pass++) {
      roads_.begin(*vec_, box);
      while (const RoadPoly* r = roads_.next()) mapdraw::roadPoly(bm, xf, *r, pass, aw_, ah_, ax, ay);
    }
  if (!vec_->hasTowns()) return;
  labels_.clear();
  for (int i = 0; i < nReserved; i++) labels_.reserve(reserved[i]);
  towns_.begin(*vec_, box);
  Town t;
  while (towns_.next(&t)) {
    const float px = xf.px((float)t.x, (float)t.y), py = xf.py((float)t.x, (float)t.y);
    if (!(px >= 4 && py >= 4 && px < aw_ - 4 && py < ah_ - 4)) continue;
    const int x = (int)px, y = (int)py;
    mapdraw::townDot(bm, ax + x, ay + y, t.city);
    mapdraw::Rect r;
    if (font && t.latin[0] && labels_.place(x, y, textWidth(t.latin, *font), aw_, &r))
      mapdraw::haloText(bm, ax + r.x0, ay + r.y1 - 3, t.latin, *font);
  }
}

}  // namespace mapview
