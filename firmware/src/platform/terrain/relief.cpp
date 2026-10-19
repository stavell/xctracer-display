// Hillshade and contour lines from the mean DEM layer, for a rotated map window.
//
// One height per pixel is taken from the bilinear surface (the way the study rendered, so the 200 m cells do not
// show as facets when a pixel is smaller than a cell); the shade comes from central differences of those heights
// one pixel apart, in SCREEN axes (so the light stays at the upper left of the screen), and a contour
// pixel is where the contour level changes between a pixel and one of its four neighbours. Marking only the pixel
// nearest to the crossing keeps lines one pixel thin, and since every pixel looks at its own four neighbours the
// result does not depend on how the window is cut into strips or bands.
//
// Tile cache: a rotated row of pixels crosses a dozen 6.4 km tiles at 200 m/px, more than the 3 x 3 cache holds.
// The window is therefore rendered in vertical strips about one tile wide, top to bottom: a strip touches at most a
// few tiles at any row, each tile is decoded about once per strip crossing, not once per row.
#include <math.h>
#include <string.h>
#include "platform/terrain/terrain.h"

namespace terrain {

namespace {

constexpr float kNoData = -1e30f;
inline bool valid(float h) { return h > -1e29f; }

inline bool isIndex(int m) { return ((m % kIndexEvery) + kIndexEvery) % kIndexEvery == 0; }

// Contour code that pixel (height h) takes from its neighbour hn: the pixel nearer to the crossing keeps the
// line, ties go to the right/down pixel (win = true).
inline uint8_t crossing(float h, float hn, int interval, bool win) {
  const float I = (float)interval;
  const int la = (int)floorf(h / I), lb = (int)floorf(hn / I);
  if (la == lb) return CONTOUR_NONE;
  const int m = hn > h ? la + 1 : la;                // the boundary next to h on the way to hn
  const float B = m * I;
  const float dh = fabsf(h - B), dn = fabsf(hn - B);
  if (dh < dn || (dh == dn && win)) return isIndex(m) ? CONTOUR_INDEX : CONTOUR_LINE;
  return CONTOUR_NONE;
}

}  // namespace

void ReliefStyle::setSun(float azDeg, float elDeg, float headingDeg) {
  const float d2r = 0.017453292f;
  const float ce = cosf(elDeg * d2r), se = sinf(elDeg * d2r);
  const float le = sinf(azDeg * d2r) * ce, ln = cosf(azDeg * d2r) * ce;         // toward the sun: east, north
  const float ch = cosf(headingDeg * d2r), sh = sinf(headingDeg * d2r);
  lightX = le * ch - ln * sh;                        // along the screen's right (bearing heading + 90)
  lightY = le * sh + ln * ch;                        // along the screen's top (bearing heading)
  lightZ = se;
}

int Terrain::stripWidth(const Window& win) const {
  ReliefCtx c;
  return reliefSetup(win, 0, &c) ? c.sw : 0;
}

int contourIntervalFor(float mPerPx) {
  static const int iv[] = {20, 50, 100, 200, 500, 1000};
  for (int i = 0; i < 6; i++)
    if ((float)iv[i] >= 2 * mPerPx) return iv[i];
  return 1000;
}

bool Terrain::reliefSetup(const Window& win, int interval, ReliefCtx* c) const {
  if (!reader_ || win.w <= 0 || win.h <= 0 || !(win.mPerPx > 0) || !isfinite(win.headingDeg)) return false;
  if (!rows_) return false;                          // a Terrain built without relief scratch
  if (!(win.lat >= -90 && win.lat <= 90 && win.lon >= -180 && win.lon <= 180)) return false;
  c->W = win.w;
  c->H = win.h;
  c->interval = interval > 0 ? interval : contourIntervalFor(win.mPerPx);
  const Layer& L = info_.layer[0];
  const int32_t clat = (int32_t)llround(win.lat * 1e7), clon = (int32_t)llround(win.lon * 1e7);
  split(2 * (int64_t)(L.lat0_e7 - clat) - L.dy_e7, 2 * L.dy_e7, &c->r0, &c->fr0);
  split(2 * (int64_t)(clon - L.lon0_e7) - L.dx_e7, 2 * L.dx_e7, &c->c0, &c->fc0);
  const float scale = win.mPerPx;
  const float cellH = L.dy_e7 * 1e-7f * (float)info_.mPerDeg;
  const float cellW = L.dx_e7 * 1e-7f * (float)info_.mPerDeg * cosf((float)(win.lat * 0.017453292519943295));
  const float th = win.headingDeg * 0.017453292f;
  c->ct = cosf(th);
  c->sn = sinf(th);
  c->kr = scale / cellH;
  c->kc = scale / cellW;
  c->halfW = win.w * 0.5f;
  c->halfH = win.h * 0.5f;
  c->inv2s = 1.0f / (2.0f * scale);
  int sw = (int)(L.tileEdge() * cellH / scale + 0.5f);
  if (sw < 16) sw = 16;
  if (sw > kMaxStrip) sw = kMaxStrip;
  c->sw = sw;
  return true;
}

// Rows [y0, y1) of the columns [xs, xe) (at most kMaxStrip wide). The output of pixel (x, y) is at
// (y - y0) * stride + (x - xs). Height rows live in a ring of three slots by absolute row number, so a call that
// continues the previous one (same strip, y0 where it ended) finds rows y0 - 1 and y0 still there.
int Terrain::reliefBand(const ReliefCtx& c, const ReliefStyle& st, int xs, int xe, int y0, int y1, int stride, uint8_t* grey,
                        uint8_t* contour) {
  const int W = c.W;
  (void)W;
  const bool resume = resumeValid_ && resumeXs_ == xs && resumeXe_ == xe && resumeY_ == y0 && y0 > 0;
  resumeValid_ = false;
  const float darkSpan = (st.flat - st.darkest) / 0.6f, lightSpan = (st.lightest - st.flat) / 0.3f;
  const int n = xe - xs + 2;                         // one extra pixel left and right
  int count = 0;
  for (int yy = resume ? y0 + 1 : y0 - 1; yy <= y1; yy++) {
    float* row = rows_[(yy + 1) % 3];
    const float sy = c.halfH - (yy + 0.5f);
    for (int i = 0; i < n; i++) {
      const float sx = (xs - 1 + i) + 0.5f - c.halfW;
      const float rt = c.fr0 + (sx * c.sn - sy * c.ct) * c.kr, cc = c.fc0 + (sx * c.ct + sy * c.sn) * c.kc;
      const float fr = floorf(rt), fc = floorf(cc);
      float h;
      row[i] = bilinear(0, c.r0 + (int)fr, c.c0 + (int)fc, rt - fr, cc - fc, &h) ? h : kNoData;
    }
    const int yo = yy - 1;
    if (yo < y0) continue;
    const float* up = rows_[(yo - 1 + 1) % 3];
    const float* cur = rows_[(yo + 1) % 3];
    const float* dn = row;
    for (int x = xs; x < xe; x++) {
      const int i = x - xs + 1;
      const float h = cur[i];
      if (!valid(h)) continue;
      count++;
      const size_t o = (size_t)(yo - y0) * stride + (x - xs);
      const float hl = cur[i - 1], hr = cur[i + 1], hu = up[i], hd = dn[i];
      if (grey) {
        const float gx = ((valid(hr) ? hr : h) - (valid(hl) ? hl : h)) * c.inv2s * st.exaggeration;
        const float gy = ((valid(hu) ? hu : h) - (valid(hd) ? hd : h)) * c.inv2s * st.exaggeration;
        const float lam = (-gx * st.lightX - gy * st.lightY + st.lightZ) / sqrtf(1.0f + gx * gx + gy * gy);
        const float d = lam - st.lightZ;
        const float f = d * fabsf(d) / (fabsf(d) + st.knee);
        float lv = st.flat + (f < 0 ? f * darkSpan : f * lightSpan);
        if (lv < 0) lv = 0;
        if (lv > 16) lv = 16;
        grey[o] = (uint8_t)(lv + 0.5f);
      }
      if (contour) {
        uint8_t cc = CONTOUR_NONE, k;
        if (valid(hr) && (k = crossing(h, hr, c.interval, true)) > cc) cc = k;
        if (valid(hd) && (k = crossing(h, hd, c.interval, true)) > cc) cc = k;
        if (valid(hl) && (k = crossing(h, hl, c.interval, false)) > cc) cc = k;
        if (valid(hu) && (k = crossing(h, hu, c.interval, false)) > cc) cc = k;
        contour[o] = cc;
      }
    }
  }
  resumeValid_ = true;                               // rows y1 - 1 and y1 are in the ring
  resumeXs_ = xs; resumeXe_ = xe; resumeY_ = y1;
  return count;
}

int Terrain::relief(const Window& win, const ReliefStyle& st, int interval, int y0, int y1, uint8_t* grey,
                    uint8_t* contour) {
  resumeValid_ = false;
  ReliefCtx c;
  if (!reliefSetup(win, interval, &c)) return 0;
  const int W = c.W, H = c.H;
  if (y0 < 0) y0 = 0;
  if (y1 > H) y1 = H;
  if (y1 <= y0) return 0;
  const size_t total = (size_t)W * (size_t)(y1 - y0);
  if (grey) memset(grey, kNoShade, total);
  if (contour) memset(contour, CONTOUR_NONE, total);
  if (!grey && !contour) return 0;
  int count = 0;
  for (int xs = 0; xs < W; xs += c.sw) {
    const int xe = xs + c.sw < W ? xs + c.sw : W;
    count += reliefBand(c, st, xs, xe, y0, y1, W, grey ? grey + xs : nullptr, contour ? contour + xs : nullptr);
  }
  resumeValid_ = false;
  return count;
}

int Terrain::reliefStrip(const Window& win, const ReliefStyle& st, int interval, int xs, int xe, int y0, int y1,
                         uint8_t* grey, uint8_t* contour) {
  ReliefCtx c;
  if (!reliefSetup(win, interval, &c)) { resumeValid_ = false; return 0; }
  if (xs < 0) xs = 0;
  if (xe > c.W) xe = c.W;
  if (y0 < 0) y0 = 0;
  if (y1 > c.H) y1 = c.H;
  if (xe <= xs || xe - xs > kMaxStrip || y1 <= y0 || (!grey && !contour)) { resumeValid_ = false; return 0; }
  const size_t total = (size_t)(xe - xs) * (size_t)(y1 - y0);
  if (grey) memset(grey, kNoShade, total);
  if (contour) memset(contour, CONTOUR_NONE, total);
  return reliefBand(c, st, xs, xe, y0, y1, xe - xs, grey, contour);
}

}  // namespace terrain
