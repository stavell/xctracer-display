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

// Light from the upper left of the screen, 40 degrees above the horizon.
constexpr float kLx = -0.54167f, kLy = 0.54167f, kLz = 0.64279f;

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

int contourIntervalFor(float mPerPx) {
  static const int iv[] = {20, 50, 100, 200, 500, 1000};
  for (int i = 0; i < 6; i++)
    if ((float)iv[i] >= 2 * mPerPx) return iv[i];
  return 1000;
}

int Terrain::relief(const Window& win, const ReliefStyle& st, int interval, int y0, int y1, uint8_t* grey,
                    uint8_t* contour) {
  if (!reader_ || win.w <= 0 || win.h <= 0 || !(win.mPerPx > 0) || !isfinite(win.headingDeg)) return 0;
  const int W = win.w, H = win.h;
  if (y0 < 0) y0 = 0;
  if (y1 > H) y1 = H;
  if (y1 <= y0) return 0;
  const size_t total = (size_t)W * (size_t)(y1 - y0);
  if (grey) memset(grey, kNoShade, total);
  if (contour) memset(contour, CONTOUR_NONE, total);
  if (!grey && !contour) return 0;
  if (interval <= 0) interval = contourIntervalFor(win.mPerPx);

  const Layer& L = info_.layer[0];
  if (!(win.lat >= -90 && win.lat <= 90 && win.lon >= -180 && win.lon <= 180)) return 0;
  const int32_t clat = (int32_t)llround(win.lat * 1e7), clon = (int32_t)llround(win.lon * 1e7);
  int r0, c0;
  float fr0, fc0;
  split(2 * (int64_t)(L.lat0_e7 - clat) - L.dy_e7, 2 * L.dy_e7, &r0, &fr0);
  split(2 * (int64_t)(clon - L.lon0_e7) - L.dx_e7, 2 * L.dx_e7, &c0, &fc0);

  const float scale = win.mPerPx;
  const float cellH = L.dy_e7 * 1e-7f * (float)info_.mPerDeg;
  const float cellW = L.dx_e7 * 1e-7f * (float)info_.mPerDeg * cosf((float)(win.lat * 0.017453292519943295));
  const float th = win.headingDeg * 0.017453292f, ct = cosf(th), sn = sinf(th);
  const float kr = scale / cellH, kc = scale / cellW;
  const float halfW = W * 0.5f, halfH = H * 0.5f;
  const float inv2s = 1.0f / (2.0f * scale);

  int sw = (int)(L.tileEdge() * cellH / scale + 0.5f);
  if (sw < 16) sw = 16;
  if (sw > kMaxStrip) sw = kMaxStrip;

  const float darkSpan = (st.flat - st.darkest) / 0.6f, lightSpan = (st.lightest - st.flat) / 0.3f;
  int count = 0;

  for (int xs = 0; xs < W; xs += sw) {
    const int xe = xs + sw < W ? xs + sw : W;
    const int n = xe - xs + 2;                       // one extra pixel left and right
    for (int yy = y0 - 1; yy <= y1; yy++) {
      float* row = rows_[(yy - (y0 - 1)) % 3];
      const float sy = halfH - (yy + 0.5f);
      for (int i = 0; i < n; i++) {
        const float sx = (xs - 1 + i) + 0.5f - halfW;
        const float rt = fr0 + (sx * sn - sy * ct) * kr, cc = fc0 + (sx * ct + sy * sn) * kc;
        const float fr = floorf(rt), fc = floorf(cc);
        float h;
        row[i] = bilinear(0, r0 + (int)fr, c0 + (int)fc, rt - fr, cc - fc, &h) ? h : kNoData;
      }
      const int yo = yy - 1;
      if (yo < y0) continue;
      const float* up = rows_[(yo - 1 - (y0 - 1)) % 3];
      const float* cur = rows_[(yo - (y0 - 1)) % 3];
      const float* dn = row;
      for (int x = xs; x < xe; x++) {
        const int i = x - xs + 1;
        const float h = cur[i];
        if (!valid(h)) continue;
        count++;
        const size_t o = (size_t)(yo - y0) * W + x;
        const float hl = cur[i - 1], hr = cur[i + 1], hu = up[i], hd = dn[i];
        if (grey) {
          const float gx = ((valid(hr) ? hr : h) - (valid(hl) ? hl : h)) * inv2s * st.exaggeration;
          const float gy = ((valid(hu) ? hu : h) - (valid(hd) ? hd : h)) * inv2s * st.exaggeration;
          const float lam = (-gx * kLx - gy * kLy + kLz) / sqrtf(1.0f + gx * gx + gy * gy);
          const float d = lam - kLz;
          const float f = d * fabsf(d) / (fabsf(d) + st.knee);
          float lv = st.flat + (f < 0 ? f * darkSpan : f * lightSpan);
          if (lv < 0) lv = 0;
          if (lv > 16) lv = 16;
          grey[o] = (uint8_t)(lv + 0.5f);
        }
        if (contour) {
          uint8_t c = CONTOUR_NONE, k;
          if (valid(hr) && (k = crossing(h, hr, interval, true)) > c) c = k;
          if (valid(hd) && (k = crossing(h, hd, interval, true)) > c) c = k;
          if (valid(hl) && (k = crossing(h, hl, interval, false)) > c) c = k;
          if (valid(hu) && (k = crossing(h, hu, interval, false)) > c) c = k;
          contour[o] = c;
        }
      }
    }
  }
  return count;
}

}  // namespace terrain
