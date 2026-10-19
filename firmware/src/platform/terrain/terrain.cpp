#include "platform/terrain/terrain.h"
#include <math.h>
#include <string.h>
#include "platform/terrain/crc32.h"

namespace terrain {

namespace {

constexpr uint32_t kEmptyKey = 0xFFFFFFFFu;

bool toE7(double deg, double lim, int32_t* out) {
  if (!(deg >= -lim && deg <= lim)) return false;     // also rejects NaN
  *out = (int32_t)llround(deg * 1e7);
  return true;
}

bool parseLayer(IBlockReader* rd, const Section& s, int kind, Layer* L) {
  uint8_t d[48];
  if (s.len < 48 || !rd->read(s.off, d, 48)) return false;
  L->lat0_e7 = (int32_t)le32(d); L->lon0_e7 = (int32_t)le32(d + 4);
  L->dy_e7 = (int32_t)le32(d + 8); L->dx_e7 = (int32_t)le32(d + 12);
  L->rows = le16(d + 16); L->cols = le16(d + 18); L->ty = le16(d + 20); L->tx = le16(d + 22);
  L->q = d[24]; L->log2t = d[25]; L->kind = d[26];
  const uint8_t codec = d[27];
  const uint32_t idx = le32(d + 28), crc = le32(d + 32), data = le32(d + 36);
  if (codec != 3 || L->kind != kind || L->q == 0 || L->log2t < 1 || L->log2t > 5) return false;
  if (L->dy_e7 <= 0 || L->dx_e7 <= 0 || L->ty == 0 || L->tx == 0) return false;
  if (L->dy_e7 > (1 << 20) || L->dx_e7 > (1 << 20)) return false;     // split() divides exactly in float up to 20 x this
  const uint32_t T = 1u << L->log2t, n = (uint32_t)L->ty * L->tx;
  if (L->rows != L->ty * T || L->cols != L->tx * T) return false;
  if (idx != 48 || crc != idx + 3 * (n + 1) || data != crc + 4 * n || data > s.len) return false;
  L->idx_off = s.off + idx; L->crc_off = s.off + crc; L->data_off = s.off + data; L->data_len = s.len - data;
  L->present = true;
  return true;
}

}  // namespace

void Terrain::flush() {
  for (int i = 0; i < nTiles_; i++) { key_[i] = kEmptyKey; stamp_[i] = 0; ok_[i] = false; }
  mruKey_ = kEmptyKey;
  mruTile_ = nullptr;
}

Terrain::Status Terrain::open(IBlockReader* rd) {
  reader_ = nullptr;
  info_ = PackInfo();
  flush();
  uint8_t h[kHeaderSize];
  if (!rd || !rd->read(0, h, kHeaderSize)) return ERR_READ;
  if (memcmp(h, "BGMP", 4) != 0) return ERR_MAGIC;
  if (le16(h + 4) != kPackVersion || le16(h + 6) != kHeaderSize) return ERR_VERSION;
  const uint32_t hcrc = le32(h + 52);
  memset(h + 12, 0, 4);                               // the file CRC field and the header CRC field count as zero
  memset(h + 52, 0, 4);
  if (crc32(0, h, kHeaderSize) != hcrc) return ERR_HEADER_CRC;
  // Re-read the two fields we zeroed (file CRC is not needed here; verifyAll() reads it itself).
  if (!rd->read(0, h, kHeaderSize)) return ERR_READ;

  PackInfo& I = info_;
  I.fileSize = le32(h + 8);
  I.latS_e7 = (int32_t)le32(h + 16); I.latN_e7 = (int32_t)le32(h + 20);
  I.lonW_e7 = (int32_t)le32(h + 24); I.lonE_e7 = (int32_t)le32(h + 28);
  I.originLat_e7 = (int32_t)le32(h + 32); I.originLon_e7 = (int32_t)le32(h + 36);
  I.mPerDeg = le32(h + 40);
  uint32_t c0 = le32(h + 44);
  memcpy(&I.cos0, &c0, 4);
  I.unitM = le16(h + 48);
  I.nsec = le16(h + 50);
  if (I.fileSize < kHeaderSize || I.nsec == 0 || I.nsec > kMaxSections || I.mPerDeg == 0 || I.unitM == 0 ||
      !(I.cos0 > 0.1f && I.cos0 <= 1.0f))
    return ERR_LAYOUT;
  for (int i = 0; i < I.nsec; i++) {
    const uint8_t* e = h + 64 + 16 * i;
    Section& s = I.sec[i];
    s.id = le16(e); s.off = le32(e + 4); s.len = le32(e + 8); s.crc = le32(e + 12);
    if (s.off < kHeaderSize || (uint64_t)s.off + s.len > I.fileSize) return ERR_LAYOUT;
  }
  // Small sections are verified now: the vector layers are drawn without any further check.
  uint8_t buf[256];
  for (int i = 0; i < I.nsec; i++) {
    const Section& s = I.sec[i];
    if (s.len > kEagerCrcLimit) continue;
    uint32_t crc = 0;
    for (uint32_t o = 0; o < s.len;) {
      uint32_t n = s.len - o < sizeof buf ? s.len - o : (uint32_t)sizeof buf;
      if (!rd->read(s.off + o, buf, n)) return ERR_READ;
      crc = crc32(crc, buf, n);
      o += n;
    }
    if (crc != s.crc) return ERR_SECTION_CRC;
  }
  const Section* sm = I.find(SEC_MEAN);
  if (!sm) return ERR_NO_DEM;
  if (!parseLayer(rd, *sm, 0, &I.layer[0])) return ERR_LAYOUT;
  const Section* sx = I.find(SEC_MAX);
  if (sx && !parseLayer(rd, *sx, 1, &I.layer[1])) return ERR_LAYOUT;
  reader_ = rd;
  return OK;
}

bool Terrain::verifyAll() {
  if (!reader_) return false;
  uint8_t buf[256];
  uint32_t crc = 0, stored = 0;
  for (uint32_t o = 0; o < info_.fileSize;) {
    uint32_t n = info_.fileSize - o < sizeof buf ? info_.fileSize - o : (uint32_t)sizeof buf;
    if (!reader_->read(o, buf, n)) return false;
    if (o == 0) { stored = le32(buf + 12); memset(buf + 12, 0, 4); }
    crc = crc32(crc, buf, n);
    o += n;
  }
  return crc == stored;
}

void Terrain::split(int64_t num2, int32_t den2, int* whole, float* frac) {
  int64_t w = num2 / den2;
  int64_t rem = num2 - w * den2;
  if (rem < 0) { w--; rem += den2; }
  *whole = (int)w;
  // float / float of exact operands (|rem| < den2 <= 2^24, parseLayer guarantees it) equals the old (float)(double quotient): a double
  // carries more than 2 x 24 + 2 bits, so the double rounding cannot differ.
  *frac = (float)rem / (float)den2;
}

const int16_t* Terrain::tile(int layer, uint32_t ti) {
  const uint32_t key = ((uint32_t)layer << 24) | ti;
  if (key == mruKey_) return mruTile_;
  int victim = 0;
  for (int i = 0; i < nTiles_; i++) {
    if (key_[i] == key) {
      stamp_[i] = ++clock_;
      mruKey_ = key;
      mruTile_ = ok_[i] ? tiles_[i] : nullptr;
      return mruTile_;
    }
    if (stamp_[i] < stamp_[victim]) victim = i;
  }
  mruKey_ = kEmptyKey;
  mruTile_ = nullptr;
  key_[victim] = kEmptyKey;                          // stays empty if the load fails on a read error
  ok_[victim] = false;
  const Layer& L = info_.layer[layer];
  if (ti >= (uint32_t)L.ty * L.tx) return nullptr;
  uint8_t ib[6], cb[4];
  if (!reader_->read(L.idx_off + 3 * ti, ib, 6)) { readErrors_++; return nullptr; }
  const uint32_t a = le24(ib), b = le24(ib + 3);
  bool good = false, cache = true;
  if (b == a) {
    // tile not stored (outside the region): a normal, cacheable "no data"
  } else if (b < a || b - a > kMaxTileBytes || b > L.data_len) {
    crcErrors_++;                                    // index points outside the data: the pack is damaged
  } else if (!reader_->read(L.crc_off + 4 * ti, cb, 4) || !reader_->read(L.data_off + a, coded_, b - a)) {
    readErrors_++;
    cache = false;
  } else if (crc32(0, coded_, b - a) != le32(cb)) {
    crcErrors_++;
  } else if (!decodeTile(coded_, b - a, L.tileEdge(), tiles_[victim])) {
    crcErrors_++;
  } else {
    good = true;
    decodes_++;
  }
  if (!cache) return nullptr;
  key_[victim] = key;
  ok_[victim] = good;
  stamp_[victim] = ++clock_;
  mruKey_ = key;
  mruTile_ = good ? tiles_[victim] : nullptr;
  return mruTile_;
}

bool Terrain::cell(int layer, int r, int c, int* v) {
  const Layer& L = info_.layer[layer];
  if (r < 0 || c < 0 || r >= L.rows || c >= L.cols) return false;
  const int sh = L.log2t;
  const int16_t* t = tile(layer, (uint32_t)(r >> sh) * L.tx + (uint32_t)(c >> sh));
  if (!t) return false;
  const int m = (1 << sh) - 1;
  *v = t[(r & m) * (1 << sh) + (c & m)];
  return true;
}

bool Terrain::bilinear(int layer, int r, int c, float fr, float fc, float* h) {
  int a, b, d, e;
  if (!cell(layer, r, c, &a) || !cell(layer, r, c + 1, &b) || !cell(layer, r + 1, c, &d) ||
      !cell(layer, r + 1, c + 1, &e))
    return false;
  *h = info_.layer[layer].q * ((a * (1 - fc) + b * fc) * (1 - fr) + (d * (1 - fc) + e * fc) * fr);
  return true;
}

bool Terrain::elevationE7(int32_t lat_e7, int32_t lon_e7, float* m) {
  if (!reader_) return false;
  const Layer& L = info_.layer[0];
  int r, c;
  float fr, fc;
  split(2 * (int64_t)(L.lat0_e7 - lat_e7) - L.dy_e7, 2 * L.dy_e7, &r, &fr);     // cell-centre coordinates
  split(2 * (int64_t)(lon_e7 - L.lon0_e7) - L.dx_e7, 2 * L.dx_e7, &c, &fc);
  return bilinear(0, r, c, fr, fc, m);
}

bool Terrain::elevation(double lat, double lon, float* m) {
  int32_t la, lo;
  if (!toE7(lat, 90, &la) || !toE7(lon, 180, &lo)) return false;
  return elevationE7(la, lo, m);
}

bool Terrain::ceilingE7(int32_t lat_e7, int32_t lon_e7, float* m) {
  if (!reader_ || !info_.layer[1].present) return false;
  const Layer& L = info_.layer[1];
  const int64_t nr = (int64_t)(L.lat0_e7 - lat_e7), nc = (int64_t)(lon_e7 - L.lon0_e7);
  int r0, c0, rlo, rhi, clo, chi;
  float f;
  split(nr, L.dy_e7, &r0, &f);                       // the cell that contains the point
  split(nc, L.dx_e7, &c0, &f);
  split(nr * 20 - 3 * (int64_t)L.dy_e7, 20 * L.dy_e7, &rlo, &f);   // +- 0.15 cell
  split(nr * 20 + 3 * (int64_t)L.dy_e7, 20 * L.dy_e7, &rhi, &f);
  split(nc * 20 - 3 * (int64_t)L.dx_e7, 20 * L.dx_e7, &clo, &f);
  split(nc * 20 + 3 * (int64_t)L.dx_e7, 20 * L.dx_e7, &chi, &f);
  int v;
  if (!cell(1, r0, c0, &v)) return false;
  int best = v;
  for (int r = rlo; r <= rhi; r++)
    for (int c = clo; c <= chi; c++)
      if (cell(1, r, c, &v) && v > best) best = v;
  float out = (float)(best * L.q), e;
  if (elevationE7(lat_e7, lon_e7, &e) && e > out) out = e;
  *m = out;
  return true;
}

bool Terrain::ceiling(double lat, double lon, float* m) {
  int32_t la, lo;
  if (!toE7(lat, 90, &la) || !toE7(lon, 180, &lo)) return false;
  return ceilingE7(la, lo, m);
}

bool Terrain::covers(double lat, double lon) const {
  if (!reader_) return false;
  int32_t la, lo;
  if (!toE7(lat, 90, &la) || !toE7(lon, 180, &lo)) return false;
  return la >= info_.latS_e7 && la <= info_.latN_e7 && lo >= info_.lonW_e7 && lo <= info_.lonE_e7;
}

}  // namespace terrain
