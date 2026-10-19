#include "apps/vario/view/terrain_map.h"
#include <math.h>
#include <string.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

using namespace terrain;

uint32_t (*TerrainMap::clock_)() = nullptr;

namespace {

const float kD2Rf = 0.017453292f;

float angDiff(float a, float b) {                       // absolute shortest angle, degrees
  float d = fabsf(fmodf(a - b, 360.0f));
  return d > 180.0f ? 360.0f - d : d;
}

// How far two sun positions are apart: the larger of the azimuth and elevation differences, degrees.
float sunDelta(float az0, float el0, float az1, float el1) {
  const float da = angDiff(az0, az1), de = fabsf(el0 - el1);
  return da > de ? da : de;
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
  // The shift ahead is a few km: float is exact enough; only the sum with the absolute position stays double.
  const float hd = isfinite(p.headingDeg) ? p.headingDeg : 0.0f;
  const float ahead = isfinite(p.headingDeg) ? kAhead * p.h * p.mPerPx : 0.0f;   // north up: the pilot is in the centre
  const float north = ahead * cosf(hd * kD2Rf), east = ahead * sinf(hd * kD2Rf);
  const float dLat = north / (float)mPerDeg;
  *lat = p.lat + (double)dLat;
  *lon = p.lon + (double)(east / ((float)mPerDeg * cosf((float)*lat * kD2Rf)));
}

TerrainMap::Result TerrainMap::update(Terrain& terr, MapVectors& vec, const Params& p, const Font1*, uint32_t nowMs) {
  if (!terr.isOpen() || !isfinite(p.lat) || !isfinite(p.lon) || !(p.mPerPx > 0)) { valid_ = false; job_ = IDLE; return KEPT; }
  int w = p.w < 1 ? 1 : (p.w > kMaxW ? kMaxW : p.w), h = p.h < 1 ? 1 : (p.h > kMaxH ? kMaxH : p.h);
  Params q = p;
  q.w = w; q.h = h;
  const float hd = isfinite(p.headingDeg) ? fmodf(fmodf(p.headingDeg, 360.0f) + 360.0f, 360.0f) : 0.0f;
  q.headingDeg = isfinite(p.headingDeg) ? hd : NAN;
  const bool sunOn = isfinite(p.sunAz) && isfinite(p.sunEl) && p.sunEl >= kSunMinEl;
  double lat, lon;
  const double mpd = terr.info().mPerDeg;
  const float mpdf = (float)mpd;
  windowCentre(q, mpd, &lat, &lon);

  if (job_ != IDLE) {
    // A job for another size, scale or pack is useless; otherwise let it finish (the next update decides again).
    if (w != jw_ || h != jh_ || p.mPerPx != jmPerPx_ || jvec_ != &vec) job_ = IDLE;
    else return STARTED;
  }
  bool redo = !valid_ || w != w_ || h != h_ || p.mPerPx != mPerPx_ || vec_ != &vec;
  if (!redo) {
    const float dN = (float)(lat - cLat_) * mpdf, dE = (float)(lon - cLon_) * mpdf * cosf((float)lat * kD2Rf);
    const float movedPx = sqrtf(dN * dN + dE * dE) / p.mPerPx;
    if (movedPx > kLostPx) valid_ = false;                       // the pilot is far out of this picture: do not show it
    const float turned = angDiff(hd, hdg_);
    const bool old = (uint32_t)(nowMs - renderedMs_) >= kRefreshMs;
    const bool sunMoved = sunOn != sunOn_ || (sunOn && sunDelta(p.sunAz, p.sunEl, sunAz_, sunEl_) > kSunDeg);
    redo = movedPx > kMovePx || turned > kTurnDeg || (old && (movedPx >= kSmallMovePx || turned >= kSmallTurnDeg)) || sunMoved;
  }
  if (!redo) return KEPT;

  Window win;
  win.lat = lat; win.lon = lon; win.mPerPx = p.mPerPx; win.w = (int16_t)w; win.h = (int16_t)h; win.headingDeg = hd;
  startJob(win, q, nowMs);
  jvec_ = &vec;
  jsunOn_ = sunOn;
  if (sunOn) {
    jsunAz_ = p.sunAz; jsunEl_ = p.sunEl;
    jstyle_.setSun(p.sunAz, p.sunEl, hd);
    jstyle_.darkest = 6.0f;                                       // the shaded side must read: that is the point of the sun
    jstyle_.exaggeration = 2.0f;
  }
  jxf_ = vec.makeXform(win);
  jbox_ = vec.boxForWindow(win);
  return STARTED;
}

void TerrainMap::startJob(const Window& win, const Params& q, uint32_t nowMs) {
  jwin_ = win;
  jstyle_ = ReliefStyle::light();
  jw_ = win.w; jh_ = win.h;
  jhdg_ = win.headingDeg;
  jmPerPx_ = q.mPerPx;
  jpilotY_ = jh_ * 0.5f + (isfinite(q.headingDeg) ? kAhead * jh_ : 0.0f);
  jStartMs_ = nowMs;
  jxs_ = 0; jy_ = 0; jdata_ = 0; jpass_ = 0; jlabels_ = kReserved; jActive_ = false;
  jsw_ = 0;
  jsunOn_ = false;
  buildBitmap().fill(DRAW_SET);
  job_ = RELIEF;
}

bool TerrainMap::step(Terrain& terr, MapVectors& vec, const Font1* font, uint32_t budgetUs) {
  if (job_ == IDLE) return false;
  if (!terr.isOpen()) { job_ = IDLE; return false; }
  const uint32_t t0 = clock_ ? clock_() : 0;
  int units = 0;
  bool done = false;
  while (job_ != IDLE) {
    if (unit(terr, vec, font)) { done = true; break; }
    units++;
    if (clock_ ? (uint32_t)(clock_() - t0) >= budgetUs : units >= kUnitsNoClock) break;
  }
  steps_++;
  if (clock_) {
    const uint32_t us = (uint32_t)(clock_() - t0);
    if (us > maxStepUs_) maxStepUs_ = us;
  }
  return done;
}

TerrainMap::Result TerrainMap::updateSync(Terrain& terr, MapVectors& vec, const Params& p, const Font1* font, uint32_t nowMs) {
  const bool wasBuilding = job_ != IDLE;
  if (update(terr, vec, p, font, nowMs) == KEPT) return KEPT;
  (void)wasBuilding;
  while (job_ != IDLE)
    if (unit(terr, vec, font)) break;
  return valid_ ? RENDERED : KEPT;
}

bool TerrainMap::unit(Terrain& terr, MapVectors& vec, const Font1* font) {
  switch (job_) {
    case RELIEF:
      reliefUnit(terr);
      if (jxs_ >= jw_) {                                    // all strips done
        if (jdata_ == 0 || (!vec.hasRoads() && !vec.hasTowns())) { finishJob(); return true; }
        job_ = vec.hasRoads() ? ROADS : TOWNS;
        jActive_ = false;
        jpass_ = 0;
      }
      return false;
    case ROADS:
      if (roadUnit(vec)) {
        jActive_ = false;
        if (!vec.hasTowns()) { finishJob(); return true; }
        job_ = TOWNS;
      }
      return false;
    case TOWNS:
      if (townUnit(vec, font)) { finishJob(); return true; }
      return false;
    default:
      return false;
  }
}

void TerrainMap::finishJob() {
  front_ ^= 1;
  vec_ = jvec_;
  w_ = jw_; h_ = jh_; mPerPx_ = jmPerPx_; hdg_ = jhdg_; pilotY_ = jpilotY_;
  cLat_ = jwin_.lat; cLon_ = jwin_.lon;
  xf_ = jxf_;
  dataPx_ = jdata_;
  noData_ = jdata_ == 0;
  sunOn_ = jsunOn_; sunAz_ = jsunAz_; sunEl_ = jsunEl_;
  renderedMs_ = jStartMs_;
  renders_++;
  valid_ = true;
  job_ = IDLE;
}

bool TerrainMap::project(double lat, double lon, float* x, float* y) const {
  if (!valid_ || !vec_) return false;
  if (!(fabs(lat) <= 90 && fabs(lon) <= 180)) return false;       // also rejects NaN
  return projectE7((int32_t)llround(lat * 1e7), (int32_t)llround(lon * 1e7), x, y);
}

bool TerrainMap::projectE7(int32_t lat_e7, int32_t lon_e7, float* x, float* y) const {
  if (!valid_ || !vec_) return false;
  float ux, uy;
  vec_->latLonToUnitE7(lat_e7, lon_e7, &ux, &uy);
  *x = xf_.px(ux, uy);
  *y = xf_.py(ux, uy);
  return isfinite(*x) && isfinite(*y);
}

bool TerrainMap::projectTrack(int i, float* x, float* y) const {
  if (i < 0 || i >= trackN_) return false;
  const int k = (trackHead_ - trackN_ + i + 2 * kTrackMax) % kTrackMax;
  return projectE7(trackLat_[k], trackLon_[k], x, y);
}

// kRows rows of the next relief strip into the layer being built.
void TerrainMap::reliefUnit(Terrain& terr) {
  if (jsw_ == 0) { jsw_ = terr.stripWidth(jwin_); if (jsw_ <= 0 || jsw_ > kMaxStrip) jsw_ = kMaxStrip; }
  const int xe = jxs_ + jsw_ < jw_ ? jxs_ + jsw_ : jw_;
  const int y1 = jy_ + kRows < jh_ ? jy_ + kRows : jh_;
  const int sw = xe - jxs_;
  jdata_ += terr.reliefStrip(jwin_, jstyle_, 0, jxs_, xe, jy_, y1, grey_, cont_);
  const Bitmap1 bm = buildBitmap();
  for (int y = jy_; y < y1; y++) {
    const uint8_t* g = &grey_[(y - jy_) * sw];
    const uint8_t* c = &cont_[(y - jy_) * sw];
    for (int i = 0; i < sw; i++) {
      const int x = jxs_ + i;
      const int gv = g[i];
      if (gv == kNoShade) {
        if (((x ^ y) & 1) != 0) bm.pixel(x, y, DRAW_CLEAR);       // no data: a 50 % checkerboard
        continue;
      }
      if (bayer8(x, y) >= 4 * gv) bm.pixel(x, y, DRAW_CLEAR);
      if (c[i]) {                                                    // contour: index lines 2 px wide
        bm.pixel(x, y, DRAW_CLEAR);
        if (c[i] == CONTOUR_INDEX) { bm.pixel(x + 1, y, DRAW_CLEAR); bm.pixel(x, y + 1, DRAW_CLEAR); }
      }
    }
  }
  jy_ = y1;
  if (jy_ >= jh_) { jy_ = 0; jxs_ = xe; }
}

// One road polyline of the current pass; true when both passes are through.
bool TerrainMap::roadUnit(MapVectors& vec) {
  if (!jActive_) { roads_.begin(vec, jbox_); jActive_ = true; }
  const RoadPoly* r = roads_.next();
  if (!r) {
    jActive_ = false;
    return ++jpass_ >= 2;
  }
  if (r->cls > 3) return false;                                       // only motorway .. secondary roads, never anything else (rail)
  static const int core[4] = {3, 2, 2, 1};                              // motorway, trunk, primary, secondary
  const Bitmap1 bm = buildBitmap();
  const int cw = core[r->cls];
  for (int i = 0; i + 1 < r->n; i++) {
    const int x0 = toPx(jxf_.px(r->x[i], r->y[i])), y0 = toPx(jxf_.py(r->x[i], r->y[i]));
    const int x1 = toPx(jxf_.px(r->x[i + 1], r->y[i + 1])), y1 = toPx(jxf_.py(r->x[i + 1], r->y[i + 1]));
    if (!segVisible(x0, y0, x1, y1, jw_, jh_, 4)) continue;
    if (jpass_ == 0) thickLine(bm, x0, y0, x1, y1, cw + 2, DRAW_SET);   // casings first, so crossings stay clean
    else thickLine(bm, x0, y0, x1, y1, cw, DRAW_CLEAR);
  }
  return false;
}

// Towns, most populous first: a ring marker always, the Latin name when it fits clear of the names already
// placed (and inside the window). A marker is drawn even when the name is dropped. One town per call; true at the end.
bool TerrainMap::townUnit(MapVectors& vec, const Font1* font) {
  const int W = jw_, H = jh_;
  if (!jActive_) {
    // The screen draws the north marker, the wind arrow, the altitude box and the scale bar over the map (see
    // drawMapPage in screens.cpp): names stay clear of those corners.
    gLabels[0] = Rect{0, 0, 50, 54};
    gLabels[1] = Rect{W - 46, 0, W, 42};
    gLabels[2] = Rect{0, H - 50, 116, H};
    gLabels[3] = Rect{W - 112, H - 32, W, H};
    const int pcx = W / 2, pcy = (int)jpilotY_;                      // where the pilot symbol will sit
    gLabels[4] = Rect{pcx - 26, pcy - 26, pcx + 26, pcy + 26};
    jlabels_ = kReserved;
    towns_.begin(vec, jbox_);
    jActive_ = true;
  }
  Town t;
  if (!towns_.next(&t)) return true;
  const Bitmap1 bm = buildBitmap();
  const float px = jxf_.px((float)t.x, (float)t.y), py = jxf_.py((float)t.x, (float)t.y);
  if (!(px >= 4 && py >= 4 && px < W - 4 && py < H - 4)) return false;
  const int x = (int)px, y = (int)py;
  fillDisc(bm, x, y, t.city ? 5 : 4, DRAW_SET);
  fillDisc(bm, x, y, t.city ? 3 : 2, DRAW_CLEAR);
  int& nl = jlabels_;
  if (!font || !t.latin[0] || nl >= kReserved + kMaxLabels) return false;
  const int tw = textWidth(t.latin, *font);
  Rect r{x + 6, y - 14, x + 6 + tw, y + 2};
  if (r.x1 >= W - 1) { r.x0 = x - 6 - tw; r.x1 = x - 6; }                // no room on the right: left of the dot
  bool hit = r.x0 < 1;
  for (int i = 0; i < nl && !hit; i++) {
    const Rect& o = gLabels[i];
    hit = !(r.x0 > o.x1 + 2 || r.x1 < o.x0 - 2 || r.y0 > o.y1 || r.y1 < o.y0);
  }
  if (hit) return false;
  gLabels[nl++] = r;
  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
      if (dx || dy) drawText(bm, r.x0 + dx, r.y1 - 3 + dy, t.latin, *font, DRAW_SET);
  drawText(bm, r.x0, r.y1 - 3, t.latin, *font, DRAW_CLEAR);
  return false;
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
