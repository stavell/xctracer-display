#include "apps/vario/view/terrain_map.h"
#include <math.h>
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

using namespace terrain;

namespace {

const double kD2R = 0.017453292519943295;

float angDiff(float a, float b) {                       // absolute shortest angle, degrees
  float d = fabsf(fmodf(a - b, 360.0f));
  return d > 180.0f ? 360.0f - d : d;
}

void thickLine(const Bitmap1& bm, int x0, int y0, int x1, int y1, int w, DrawMode m) {
  if (w <= 1) { line(bm, x0, y0, x1, y1, m); return; }
  for (int a = -(w - 1) / 2; a <= w / 2; a++)
    for (int b = -(w - 1) / 2; b <= w / 2; b++) line(bm, x0 + a, y0 + b, x1 + a, y1 + b, m);
}

// Skip a segment whose bounding box misses the bitmap by a margin: the polylines come from 10 km blocks and
// mostly lie off the window, and line() clips pixel by pixel.
bool segVisible(int x0, int y0, int x1, int y1, int w, int h, int m) {
  if ((x0 < -m && x1 < -m) || (x0 > w + m && x1 > w + m)) return false;
  if ((y0 < -m && y1 < -m) || (y0 > h + m && y1 > h + m)) return false;
  return true;
}

struct Rect { int x0, y0, x1, y1; };
constexpr int kReserved = 5, kMaxLabels = 40;
Rect gLabels[kReserved + kMaxLabels];                                // static: the loop task has 4 KB of stack

// Float to int for pixel coordinates: a polyline vertex far outside the window must not overflow the int.
int toPx(float v) {
  if (!(v > -30000.0f)) return -30000;
  if (v > 30000.0f) return 30000;
  return (int)floorf(v);
}

}  // namespace

void TerrainMap::windowCentre(const Params& p, double mPerDeg, double* lat, double* lon) {
  const float hd = isfinite(p.headingDeg) ? p.headingDeg : 0.0f;
  const double ahead = isfinite(p.headingDeg) ? kAhead * p.h * p.mPerPx : 0.0;   // north up: the pilot is in the centre
  const double north = ahead * cos(hd * kD2R), east = ahead * sin(hd * kD2R);
  *lat = p.lat + north / mPerDeg;
  *lon = p.lon + east / (mPerDeg * cos(*lat * kD2R));
}

TerrainMap::Result TerrainMap::update(Terrain& terr, MapVectors& vec, const Params& p, const Font1* font, uint32_t nowMs) {
  if (!terr.isOpen() || !isfinite(p.lat) || !isfinite(p.lon) || !(p.mPerPx > 0)) { valid_ = false; return KEPT; }
  int w = p.w < 1 ? 1 : (p.w > kMaxW ? kMaxW : p.w), h = p.h < 1 ? 1 : (p.h > kMaxH ? kMaxH : p.h);
  Params q = p;
  q.w = w; q.h = h;
  const float hd = isfinite(p.headingDeg) ? fmodf(fmodf(p.headingDeg, 360.0f) + 360.0f, 360.0f) : 0.0f;
  q.headingDeg = isfinite(p.headingDeg) ? hd : NAN;
  double lat, lon;
  const double mpd = terr.info().mPerDeg;
  windowCentre(q, mpd, &lat, &lon);

  bool redo = !valid_ || w != w_ || h != h_ || p.mPerPx != mPerPx_ || vec_ != &vec;
  if (!redo) {
    const double dN = (lat - cLat_) * mpd, dE = (lon - cLon_) * mpd * cos(lat * kD2R);
    const float movedPx = (float)(sqrt(dN * dN + dE * dE) / p.mPerPx);
    const float turned = angDiff(hd, hdg_);
    const bool old = (uint32_t)(nowMs - renderedMs_) >= kRefreshMs;
    redo = movedPx > kMovePx || turned > kTurnDeg || (old && (movedPx >= kSmallMovePx || turned >= kSmallTurnDeg));
  }
  if (!redo) return KEPT;

  Window win;
  win.lat = lat; win.lon = lon; win.mPerPx = p.mPerPx; win.w = (int16_t)w; win.h = (int16_t)h; win.headingDeg = hd;
  vec_ = &vec;
  pilotY_ = h * 0.5f + (isfinite(p.headingDeg) ? kAhead * h : 0.0f);
  w_ = w; h_ = h; mPerPx_ = p.mPerPx; hdg_ = hd; cLat_ = lat; cLon_ = lon;
  render(terr, vec, win, font);
  xf_ = vec.makeXform(win);
  renderedMs_ = nowMs;
  renders_++;
  valid_ = true;
  return RENDERED;
}

bool TerrainMap::project(double lat, double lon, float* x, float* y) const {
  if (!valid_ || !vec_) return false;
  double ux, uy;
  vec_->latLonToUnit(lat, lon, &ux, &uy);
  *x = xf_.px((float)ux, (float)uy);
  *y = xf_.py((float)ux, (float)uy);
  return isfinite(*x) && isfinite(*y);
}

void TerrainMap::render(Terrain& terr, MapVectors& vec, const Window& win, const Font1* font) {
  const int w = win.w, h = win.h;
  Bitmap1 bm = cache();
  bm.fill(DRAW_SET);
  const ReliefStyle style = ReliefStyle::light();
  int data = 0;
  for (int y0 = 0; y0 < h; y0 += kBand) {
    const int y1 = y0 + kBand < h ? y0 + kBand : h;
    data += terr.relief(win, style, 0, y0, y1, grey_, cont_);
    for (int y = y0; y < y1; y++) {
      const uint8_t* g = &grey_[(y - y0) * w];
      const uint8_t* c = &cont_[(y - y0) * w];
      for (int x = 0; x < w; x++) {
        const int gv = g[x];
        if (gv == kNoShade) {
          if (((x ^ y) & 1) != 0) bm.pixel(x, y, DRAW_CLEAR);       // no data: a 50 % checkerboard
          continue;
        }
        if (bayer8(x, y) >= 4 * gv) bm.pixel(x, y, DRAW_CLEAR);
        if (c[x]) {                                                    // contour: index lines 2 px wide
          bm.pixel(x, y, DRAW_CLEAR);
          if (c[x] == CONTOUR_INDEX) { bm.pixel(x + 1, y, DRAW_CLEAR); bm.pixel(x, y + 1, DRAW_CLEAR); }
        }
      }
    }
  }
  dataPx_ = data;
  noData_ = data == 0;
  if (noData_) return;
  drawRoads(vec, win, bm);
  drawTowns(vec, win, bm, font);
}

void TerrainMap::drawRoads(MapVectors& vec, const Window& win, const Bitmap1& bm) {
  if (!vec.hasRoads()) return;
  const VecXform xf = vec.makeXform(win);
  const UnitBox box = vec.boxForWindow(win);
  static const int core[4] = {3, 2, 2, 1};                              // motorway, trunk, primary, secondary
  for (int pass = 0; pass < 2; pass++) {                                // casings first, so crossings stay clean
    roads_.begin(vec, box);
    while (const RoadPoly* r = roads_.next()) {
      const int cw = core[r->cls & 3];
      for (int i = 0; i + 1 < r->n; i++) {
        const int x0 = toPx(xf.px(r->x[i], r->y[i])), y0 = toPx(xf.py(r->x[i], r->y[i]));
        const int x1 = toPx(xf.px(r->x[i + 1], r->y[i + 1])), y1 = toPx(xf.py(r->x[i + 1], r->y[i + 1]));
        if (!segVisible(x0, y0, x1, y1, win.w, win.h, 4)) continue;
        if (pass == 0) thickLine(bm, x0, y0, x1, y1, cw + 2, DRAW_SET);
        else thickLine(bm, x0, y0, x1, y1, cw, DRAW_CLEAR);
      }
    }
  }
}

// Towns, most populous first: a ring marker always, the Latin name when it fits clear of the names already
// placed (and inside the window). A marker is drawn even when the name is dropped.
void TerrainMap::drawTowns(MapVectors& vec, const Window& win, const Bitmap1& bm, const Font1* font) {
  if (!vec.hasTowns()) return;
  const VecXform xf = vec.makeXform(win);
  const UnitBox box = vec.boxForWindow(win);
  const int W = win.w, H = win.h;
  // The screen draws the north marker, the wind arrow, the altitude box and the scale bar over the map (see
  // drawMapPage in screens.cpp): names stay clear of those corners.
  gLabels[0] = Rect{0, 0, 50, 54};
  gLabels[1] = Rect{W - 46, 0, W, 42};
  gLabels[2] = Rect{0, H - 50, 116, H};
  gLabels[3] = Rect{W - 112, H - 32, W, H};
  const int pcx = W / 2, pcy = (int)pilotY_;                       // where the pilot arrow will sit, with its badge
  gLabels[4] = Rect{pcx - 16, pcy - 16, pcx + 16, pcy + 16};
  int nl = kReserved;
  Town t;
  towns_.begin(vec, box);
  while (towns_.next(&t)) {
    const float px = xf.px((float)t.x, (float)t.y), py = xf.py((float)t.x, (float)t.y);
    if (!(px >= 4 && py >= 4 && px < W - 4 && py < H - 4)) continue;
    const int x = (int)px, y = (int)py;
    fillDisc(bm, x, y, t.city ? 5 : 4, DRAW_SET);
    fillDisc(bm, x, y, t.city ? 3 : 2, DRAW_CLEAR);
    if (!font || !t.latin[0] || nl >= kReserved + kMaxLabels) continue;
    const int tw = textWidth(t.latin, *font);
    Rect r{x + 6, y - 14, x + 6 + tw, y + 2};
    if (r.x1 >= W - 1) { r.x0 = x - 6 - tw; r.x1 = x - 6; }                // no room on the right: left of the dot
    bool hit = r.x0 < 1;
    for (int i = 0; i < nl && !hit; i++) {
      const Rect& o = gLabels[i];
      hit = !(r.x0 > o.x1 + 2 || r.x1 < o.x0 - 2 || r.y0 > o.y1 || r.y1 < o.y0);
    }
    if (hit) continue;
    gLabels[nl++] = r;
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        if (dx || dy) drawText(bm, r.x0 + dx, r.y1 - 3 + dy, t.latin, *font, DRAW_SET);
    drawText(bm, r.x0, r.y1 - 3, t.latin, *font, DRAW_CLEAR);
  }
}

void TerrainMap::feedTrack(double lat, double lon, uint32_t clockMs) {
  if (!isfinite(lat) || !isfinite(lon)) return;
  if (haveTrack_ && (uint32_t)(clockMs - trackMs_) < kTrackEveryMs && clockMs >= trackMs_) return;
  haveTrack_ = true;
  trackMs_ = clockMs;
  trackLat_[trackHead_] = (int32_t)llround(lat * 1e7);
  trackLon_[trackHead_] = (int32_t)llround(lon * 1e7);
  trackHead_ = (trackHead_ + 1) % kTrackMax;
  if (trackN_ < kTrackMax) trackN_++;
}

bool TerrainMap::trackAt(int i, double* lat, double* lon) const {
  if (i < 0 || i >= trackN_) return false;
  const int k = (trackHead_ - trackN_ + i + 2 * kTrackMax) % kTrackMax;
  *lat = trackLat_[k] * 1e-7;
  *lon = trackLon_[k] * 1e-7;
  return true;
}
