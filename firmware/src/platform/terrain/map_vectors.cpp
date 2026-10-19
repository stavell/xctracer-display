#include "platform/terrain/map_vectors.h"
#include <math.h>
#include <string.h>

namespace terrain {

namespace {

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

}  // namespace

bool MapVectors::open(IBlockReader* rd, const PackInfo& info) {
  rd_ = rd;
  info_ = info;
  townCount_ = 0;
  roadsOk_ = false;
  strings_ = Section();
  if (const Section* s = info.find(SEC_STRINGS)) strings_ = *s;
  uint8_t h[24];
  if (const Section* s = info.find(SEC_TOWNS)) {
    if (s->len >= 4 && rd->read(s->off, h, 4)) {
      towns_ = *s;
      uint16_t n = le16(h), names = le16(h + 2);
      if (4u + 8u * n <= names && names <= s->len) { townCount_ = n; townNames_ = names; }
    }
  }
  if (const Section* s = info.find(SEC_ROADS)) {
    if (s->len >= 24 && rd->read(s->off, h, 24)) {
      roads_ = *s;
      blk_ = le16(h); bx0_ = le16(h + 2); by0_ = le16(h + 4); nbx_ = le16(h + 6); nby_ = le16(h + 8);
      maxPts_ = le16(h + 10);
      roadIdx_ = le32(h + 12); roadData_ = le32(h + 16);
      const uint32_t cells = (uint32_t)nbx_ * nby_;
      roadsOk_ = blk_ > 0 && nbx_ > 0 && nby_ > 0 && maxPts_ >= 2 && maxPts_ <= kMaxRoadPts && roadIdx_ == 24 &&
                 roadData_ == roadIdx_ + 3 * (cells + 1) && roadData_ <= s->len;
    }
  }
  return true;
}

void MapVectors::unitToLatLon(double x, double y, double* lat, double* lon) const {
  const double mpd = info_.mPerDeg, u = info_.unitM;
  *lat = info_.originLat_e7 * 1e-7 - y * u / mpd;
  *lon = info_.originLon_e7 * 1e-7 + x * u / (info_.cos0 * mpd);
}

void MapVectors::latLonToUnit(double lat, double lon, double* x, double* y) const {
  const double mpd = info_.mPerDeg, u = info_.unitM;
  *y = (info_.originLat_e7 * 1e-7 - lat) * mpd / u;
  *x = (lon - info_.originLon_e7 * 1e-7) * info_.cos0 * mpd / u;
}

UnitBox MapVectors::boxFromLatLon(double latS, double latN, double lonW, double lonE) const {
  double x0, y0, x1, y1;
  latLonToUnit(latN, lonW, &x0, &y0);
  latLonToUnit(latS, lonE, &x1, &y1);
  UnitBox b;
  b.x0 = (int32_t)floor(x0); b.y0 = (int32_t)floor(y0);
  b.x1 = (int32_t)ceil(x1); b.y1 = (int32_t)ceil(y1);
  return b;
}

void MapVectors::latLonToUnitE7(int32_t lat_e7, int32_t lon_e7, float* x, float* y) const {
  // Integer differences to the origin are exact; one float scale each, no double.
  const float k = (float)info_.mPerDeg * 1e-7f / (float)info_.unitM;
  *y = (float)(info_.originLat_e7 - lat_e7) * k;
  *x = (float)(lon_e7 - info_.originLon_e7) * (k * info_.cos0);
}

namespace {
constexpr float kD2Rf = 0.017453292f;

// Window centre relative to the pack origin in degrees (north, east of the origin): the only double work, then float.
void originOffsetDeg(const PackInfo& I, const Window& win, float* dLat, float* dLon) {
  *dLat = (float)(win.lat - I.originLat_e7 * 1e-7);
  *dLon = (float)(win.lon - I.originLon_e7 * 1e-7);
}
}  // namespace

UnitBox MapVectors::boxForWindow(const Window& win) const {
  const float mpd = (float)info_.mPerDeg, u = (float)info_.unitM;
  const float cosLat = cosf((float)win.lat * kD2Rf);
  const float th = win.headingDeg * kD2Rf, ct = cosf(th), sn = sinf(th);
  float dLat, dLon;
  originOffsetDeg(info_, win, &dLat, &dLon);
  const float xc = dLon * info_.cos0 * mpd / u, yc = -dLat * mpd / u;   // window centre in units
  const float kx = info_.cos0 / (cosLat * u), ky = 1.0f / u;                          // units per metre east / north
  float xmin = 1e30f, xmax = -1e30f, ymin = 1e30f, ymax = -1e30f;
  for (int k = 0; k < 4; k++) {
    const float sx = (k & 1 ? 0.5f : -0.5f) * win.w, sy = (k & 2 ? 0.5f : -0.5f) * win.h;
    const float east = (sx * ct + sy * sn) * win.mPerPx, north = (sy * ct - sx * sn) * win.mPerPx;
    const float x = xc + east * kx, y = yc - north * ky;
    if (x < xmin) xmin = x;
    if (x > xmax) xmax = x;
    if (y < ymin) ymin = y;
    if (y > ymax) ymax = y;
  }
  UnitBox b;
  b.x0 = (int32_t)floorf(xmin) - 1; b.y0 = (int32_t)floorf(ymin) - 1;
  b.x1 = (int32_t)ceilf(xmax) + 1; b.y1 = (int32_t)ceilf(ymax) + 1;
  return b;
}

VecXform MapVectors::makeXform(const Window& win) const {
  const float mpd = (float)info_.mPerDeg, u = (float)info_.unitM;
  const float cosLat = cosf((float)win.lat * kD2Rf);
  const float th = win.headingDeg * kD2Rf, ct = cosf(th), sn = sinf(th);
  float dLat, dLon;
  originOffsetDeg(info_, win, &dLat, &dLon);
  const float ex = u * cosLat / info_.cos0;                        // east metres per x unit
  const float e0 = -dLon * mpd * cosLat;                           // origin east of the centre, metres
  const float ny = -u;                                             // north metres per y unit
  const float n0 = -dLat * mpd;
  const float s = win.mPerPx;
  VecXform t;
  t.a = ex * ct / s;
  t.b = -ny * sn / s;
  t.c = win.w * 0.5f + (e0 * ct - n0 * sn) / s;
  t.d = -ex * sn / s;
  t.e = -ny * ct / s;
  t.f = win.h * 0.5f - (e0 * sn + n0 * ct) / s;
  return t;
}

bool MapVectors::attribution(int idx, char* dst, size_t cap) const {
  if (!rd_ || cap == 0 || strings_.len < 2) return false;
  uint8_t cnt;
  if (!rd_->read(strings_.off, &cnt, 1) || idx < 0 || idx >= cnt) return false;
  uint32_t pos = 1;
  int cur = 0;
  size_t w = 0;
  uint8_t chunk[32];
  while (pos < strings_.len) {
    uint32_t n = strings_.len - pos < sizeof chunk ? strings_.len - pos : (uint32_t)sizeof chunk;
    if (!rd_->read(strings_.off + pos, chunk, n)) return false;
    for (uint32_t i = 0; i < n; i++) {
      if (chunk[i] == 0) {
        if (cur == idx) { dst[w] = 0; return true; }
        cur++;
      } else if (cur == idx && w + 1 < cap) {
        dst[w++] = (char)chunk[i];
      }
    }
    pos += n;
  }
  return false;
}

// ---------------------------------------------------------------- towns

void TownIter::begin(const MapVectors& v, const UnitBox& box) {
  v_ = &v;
  box_ = box;
  pos_ = 0;
  have_ = 0;
  base_ = 0;
}

bool TownIter::next(Town* out) {
  if (!v_) return false;
  const MapVectors& v = *v_;
  while (pos_ < v.townCount_) {
    if (pos_ >= base_ + have_) {
      base_ = pos_;
      have_ = v.townCount_ - pos_ < 32 ? v.townCount_ - pos_ : 32;
      if (!v.rd_->read(v.towns_.off + 4 + 8 * (uint32_t)base_, chunk_, 8u * have_)) { pos_ = v.townCount_; return false; }
    }
    const uint8_t* r = chunk_ + 8 * (pos_ - base_);
    pos_++;
    const int32_t x = le16(r), y = le16(r + 2);
    if (!box_.contains(x, y)) continue;
    const uint32_t no = (uint32_t)v.townNames_ + le16(r + 6);
    uint8_t lens[2];
    if (no + 2 > v.towns_.len || !v.rd_->read(v.towns_.off + no, lens, 2)) continue;
    if (lens[0] >= kMaxLatin || lens[1] >= kMaxCyr || no + 2 + lens[0] + lens[1] > v.towns_.len) continue;
    if (!v.rd_->read(v.towns_.off + no + 2, out->latin, lens[0]) ||
        !v.rd_->read(v.towns_.off + no + 2 + lens[0], out->cyr, lens[1]))
      continue;
    out->latin[lens[0]] = 0;
    out->cyr[lens[1]] = 0;
    out->x = x; out->y = y;
    out->popClass = r[4] & 7;
    out->city = (r[4] & 8) != 0;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- roads

void RoadIter::begin(const MapVectors& v, const UnitBox& box) {
  v_ = &v;
  box_ = box;
  left_ = 0;
  inBlock_ = false;
  byB_ = -1; byA_ = 0; bx_ = by_ = 0;
  if (!v.roadsOk_) return;
  // A polyline belongs to the block of its first vertex and may reach one vertex into a neighbour: look one ring wider.
  int ax = floorDiv(box.x0, v.blk_) - 1, bx = floorDiv(box.x1, v.blk_) + 1;
  int ay = floorDiv(box.y0, v.blk_) - 1, by = floorDiv(box.y1, v.blk_) + 1;
  if (ax < v.bx0_) ax = v.bx0_;
  if (ay < v.by0_) ay = v.by0_;
  if (bx > v.bx0_ + v.nbx_ - 1) bx = v.bx0_ + v.nbx_ - 1;
  if (by > v.by0_ + v.nby_ - 1) by = v.by0_ + v.nby_ - 1;
  bxA_ = ax; bxB_ = bx; byA_ = ay; byB_ = by;
  bx_ = ax; by_ = ay;
}

namespace {

struct Cursor {
  const uint8_t* p;
  uint32_t pos, len;
  bool varint(uint32_t* v) {
    uint32_t r = 0;
    for (int s = 0; s < 35; s += 7) {
      if (pos >= len) return false;
      uint8_t b = p[pos++];
      r |= (uint32_t)(b & 127) << s;
      if (!(b & 128)) { *v = r; return true; }
    }
    return false;
  }
  bool zig(int32_t* v) {
    uint32_t u;
    if (!varint(&u)) return false;
    *v = (u & 1) ? -(int32_t)((u + 1) >> 1) : (int32_t)(u >> 1);
    return true;
  }
};

}  // namespace

bool RoadIter::loadBlock() {
  const MapVectors& v = *v_;
  const uint32_t cell = (uint32_t)(by_ - v.by0_) * v.nbx_ + (uint32_t)(bx_ - v.bx0_);
  uint8_t ib[6];
  if (!v.rd_->read(v.roads_.off + v.roadIdx_ + 3 * cell, ib, 6)) return false;
  const uint32_t a = le24(ib), b = le24(ib + 3);
  if (b <= a || b - a > kMaxRoadBlockBytes || v.roadData_ + b > v.roads_.len) return false;
  if (!v.rd_->read(v.roads_.off + v.roadData_ + a, buf_, b - a)) return false;
  Cursor c{buf_, 0, b - a};
  uint32_t np;
  if (!c.varint(&np) || np == 0) return false;
  pos_ = c.pos; len_ = b - a; left_ = np;
  return true;
}

const RoadPoly* RoadIter::next() {
  if (!v_) return nullptr;
  const MapVectors& v = *v_;
  for (;;) {
    if (left_ == 0) {
      bool got = false;
      while (!got && by_ <= byB_) {
        got = loadBlock();
        inBlock_ = got;
        curBx_ = bx_; curBy_ = by_;
        if (++bx_ > bxB_) { bx_ = bxA_; by_++; }
      }
      if (!got) return nullptr;
    }
    left_--;
    Cursor c{buf_, pos_, len_};
    uint32_t n;
    int32_t dx, dy;
    uint8_t cls;
    if (!c.varint(&n) || n < 2 || n > v.maxPts_ || c.pos >= c.len) { left_ = 0; continue; }
    cls = buf_[c.pos++];
    int32_t x = curBx_ * (int32_t)v.blk_, y = curBy_ * (int32_t)v.blk_;
    bool ok = true;
    RoadPoly& p = poly_;
    p.cls = cls;
    p.n = (uint8_t)n;
    p.bbox.x0 = p.bbox.y0 = 0x7FFFFFFF;
    p.bbox.x1 = p.bbox.y1 = -0x7FFFFFFF;
    for (uint32_t i = 0; i < n && ok; i++) {
      ok = c.zig(&dx) && c.zig(&dy);
      x += dx; y += dy;
      if (!ok || x < 0 || y < 0 || x > 65535 || y > 65535) { ok = false; break; }
      p.x[i] = (uint16_t)x; p.y[i] = (uint16_t)y;
      if (x < p.bbox.x0) p.bbox.x0 = x;
      if (x > p.bbox.x1) p.bbox.x1 = x;
      if (y < p.bbox.y0) p.bbox.y0 = y;
      if (y > p.bbox.y1) p.bbox.y1 = y;
    }
    if (!ok) { left_ = 0; continue; }       // damaged block: drop the rest of it
    pos_ = c.pos;
    if (p.bbox.intersects(box_)) return &p;
  }
}

}  // namespace terrain
