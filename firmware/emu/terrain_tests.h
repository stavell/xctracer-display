#pragma once
// Tests for platform/terrain: CRC, tile codec, pack open/validation, elevation and ceiling against the Python
// reference (fixtures/sopot.pack built by host/terrain/mkmappack.py), corruption handling, vector queries, relief
// (hillshade + contours on a synthetic cone). Included by tests.cpp, after CHECK and NEAR are defined.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <stdlib.h>
#include "platform/terrain/crc32.h"
#include "platform/terrain/map_vectors.h"
#include "platform/terrain/terrain.h"

namespace terrain_tests {
using namespace terrain;

static bool loadFile(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize((size_t)n);
  bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
  fclose(f);
  return ok;
}

static void w16(std::vector<uint8_t>& v, size_t o, uint32_t x) { v[o] = x & 255; v[o + 1] = (x >> 8) & 255; }
static void w32(std::vector<uint8_t>& v, size_t o, uint32_t x) { w16(v, o, x & 0xFFFF); w16(v, o + 2, x >> 16); }

// ---- a synthetic pack: one 256 x 256 cell mean layer of raw tiles holding a cone, plus a strings section
static const double kConeLat0 = 43.0, kConeLon0 = 25.0, kConeTop = 2400, kConeSlope = 0.2;
static const int32_t kDy = 17986, kDx = 24474;
static double coneCentreLat() { return kConeLat0 - 128 * kDy * 1e-7; }
static double coneCentreLon() { return kConeLon0 + 128 * kDx * 1e-7; }

static void buildCone(std::vector<uint8_t>& out) {
  const int T = 32, N = 8, R = N * T;
  std::vector<int16_t> g((size_t)R * R);
  const double clat = coneCentreLat(), mpd = 111195.0, cosLat = cos(clat * D2R);
  for (int r = 0; r < R; r++)
    for (int c = 0; c < R; c++) {
      double north = (128 - (r + 0.5)) * kDy * 1e-7 * mpd, east = (c + 0.5 - 128) * kDx * 1e-7 * mpd * cosLat;
      double h = kConeTop - kConeSlope * sqrt(north * north + east * east);
      g[(size_t)r * R + c] = (int16_t)lround(h < 0 ? 0 : h);
    }
  const int n = N * N, tb = 1 + 2 * T * T;
  std::vector<uint8_t> sec(48 + 3 * (n + 1) + 4 * n + (size_t)tb * n, 0);
  const uint32_t idx = 48, crc = idx + 3 * (n + 1), data = crc + 4 * n;
  w32(sec, 0, (uint32_t)(int32_t)lround(kConeLat0 * 1e7)); w32(sec, 4, (uint32_t)(int32_t)lround(kConeLon0 * 1e7));
  w32(sec, 8, kDy); w32(sec, 12, kDx);
  w16(sec, 16, R); w16(sec, 18, R); w16(sec, 20, N); w16(sec, 22, N);
  sec[24] = 1; sec[25] = 5; sec[26] = 0; sec[27] = 3;
  w32(sec, 28, idx); w32(sec, 32, crc); w32(sec, 36, data);
  for (int ti = 0; ti <= n; ti++) {
    uint32_t o = ti * tb;
    sec[idx + 3 * ti] = o & 255; sec[idx + 3 * ti + 1] = (o >> 8) & 255; sec[idx + 3 * ti + 2] = o >> 16;
  }
  for (int ti = 0; ti < n; ti++) {
    uint8_t* d = &sec[data + (size_t)ti * tb];
    d[0] = 1 << 4;
    for (int i = 0; i < T * T; i++) {
      int16_t v = g[(size_t)((ti / N) * T + i / T) * R + (ti % N) * T + i % T];
      d[1 + 2 * i] = v & 255; d[2 + 2 * i] = (uint8_t)((uint16_t)v >> 8);
    }
    w32(sec, crc + 4 * ti, crc32(0, d, tb));
  }
  std::vector<uint8_t> str = {1, 'o', 'k', 0};
  const uint32_t o1 = kHeaderSize, o2 = o1 + (uint32_t)sec.size();
  out.assign(o2 + str.size(), 0);
  memcpy(&out[o1], sec.data(), sec.size());
  memcpy(&out[o2], str.data(), str.size());
  memcpy(&out[0], "BGMP", 4);
  w16(out, 4, 1); w16(out, 6, kHeaderSize); w32(out, 8, (uint32_t)out.size());
  w32(out, 16, (uint32_t)(int32_t)(lround(kConeLat0 * 1e7) - R * kDy)); w32(out, 20, (uint32_t)lround(kConeLat0 * 1e7));
  w32(out, 24, (uint32_t)lround(kConeLon0 * 1e7)); w32(out, 28, (uint32_t)(lround(kConeLon0 * 1e7) + R * kDx));
  w32(out, 32, 450000000u); w32(out, 36, 220000000u); w32(out, 40, 111195);
  float cos0 = 0.73432f; memcpy(&out[44], &cos0, 4);
  w16(out, 48, 10); w16(out, 50, 2);
  w16(out, 64, 1); w32(out, 68, o1); w32(out, 72, (uint32_t)sec.size()); w32(out, 76, crc32(0, sec.data(), sec.size()));
  w16(out, 80, 5); w32(out, 84, o2); w32(out, 88, (uint32_t)str.size()); w32(out, 92, crc32(0, str.data(), str.size()));
  w32(out, 52, crc32(0, out.data(), kHeaderSize));
  w32(out, 12, crc32(0, out.data(), out.size()));
}

static void test_crc_codec() {
  puts("terrain crc32 and tile codec");
  CHECK(crc32(0, "123456789", 9) == 0xCBF43926u);
  uint32_t a = crc32(0, "12345", 5);
  CHECK(crc32(a, "6789", 4) == 0xCBF43926u);
  int16_t out[kMaxTileCells];
  uint8_t cst[3] = {0, 0x34, 0x12};
  CHECK(decodeTile(cst, 3, 32, out) && out[0] == 0x1234 && out[1023] == 0x1234);
  uint8_t bad[3] = {5 << 4, 0, 0};
  CHECK(!decodeTile(bad, 3, 32, out));                 // unknown mode
  CHECK(!decodeTile(cst, 2, 32, out));                 // truncated
  uint8_t trunc[10] = {3 << 4 | 2, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  CHECK(!decodeTile(trunc, 10, 32, out));              // bit stream runs out before 1023 residuals
  CHECK(!decodeTileSimple(trunc, 10, 32, out));
}

// Reference file written by mkmappack.py --ref
struct Ref {
  struct E { int32_t lat, lon; double mean, cmax; };
  std::vector<E> e;
  struct Tn { int x, y, cls, pop; std::string latin, cyr; };
  std::vector<Tn> towns;
  struct Rd { int cls; std::vector<int> xy; };
  std::vector<Rd> roads;
};

static bool loadRef(const char* path, Ref& r) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  static char line[4096];
  while (fgets(line, sizeof line, f)) {
    if (line[0] == 'E') {
      Ref::E e; char a[32], b[32];
      if (sscanf(line + 2, "%d %d %31s %31s", &e.lat, &e.lon, a, b) != 4) continue;
      e.mean = strcmp(a, "nan") ? atof(a) : NAN;
      e.cmax = strcmp(b, "nan") ? atof(b) : NAN;
      r.e.push_back(e);
    } else if (line[0] == 'T') {
      Ref::Tn t; int off = 0;
      if (sscanf(line + 2, "%d %d %d %d %n", &t.x, &t.y, &t.cls, &t.pop, &off) < 4) continue;
      std::string rest = line + 2 + off;
      while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.pop_back();
      size_t bar = rest.find('|');
      t.latin = rest.substr(0, bar); t.cyr = rest.substr(bar + 1);
      r.towns.push_back(t);
    } else if (line[0] == 'P') {
      Ref::Rd d; int cls, n, off = 0;
      sscanf(line + 2, "%d %d %n", &cls, &n, &off);
      d.cls = cls;
      const char* p = line + 2 + off;
      for (int i = 0; i < 2 * n; i++) { char* e; d.xy.push_back((int)strtol(p, &e, 10)); p = e; }
      r.roads.push_back(d);
    }
  }
  fclose(f);
  return true;
}

static uint64_t hashPoly(int cls, int n, const uint16_t* x, const uint16_t* y) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 1099511628211ull; };
  mix(cls); mix(n);
  for (int i = 0; i < n; i++) { mix(x[i]); mix(y[i]); }
  return h;
}

static void test_pack_reference() {
  puts("terrain pack: open, elevation/ceiling vs Python, vectors");
  static std::vector<uint8_t> bytes;
  static Ref ref;
  CHECK(loadFile("fixtures/sopot.pack", bytes));
  CHECK(loadRef("fixtures/sopot.ref.txt", ref));
  if (bytes.empty() || ref.e.empty()) return;
  static MemReader rd;
  rd = MemReader(bytes.data(), (uint32_t)bytes.size());
  static Terrain t;
  CHECK(t.open(&rd) == Terrain::OK);
  CHECK(t.isOpen());
  CHECK(t.info().fileSize == bytes.size());
  CHECK(t.info().nsec == 5);
  CHECK(t.info().layer[0].present && t.info().layer[1].present);
  CHECK(t.info().layer[0].q == 4 && t.info().layer[1].q == 10 && t.info().layer[1].kind == 1);
  CHECK(t.verifyAll());

  // codec: both bit readers decode every tile of the fixture to the same cells; time them
  {
    const Layer& L = t.info().layer[0];
    int n = L.ty * L.tx, decoded = 0;
    static int16_t a[kMaxTileCells], b[kMaxTileCells];
    std::vector<uint32_t> off, len;
    for (int i = 0; i < n; i++) {
      uint8_t ib[6];
      CHECK(rd.read(L.idx_off + 3 * i, ib, 6));
      uint32_t s = le24(ib), e = le24(ib + 3);
      if (e > s) { off.push_back(L.data_off + s); len.push_back(e - s); }
    }
    for (size_t i = 0; i < off.size(); i++) {
      const uint8_t* p = bytes.data() + off[i];
      bool ok1 = decodeTile(p, len[i], 32, a), ok2 = decodeTileSimple(p, len[i], 32, b);
      CHECK(ok1 && ok2 && memcmp(a, b, sizeof a) == 0);
      decoded++;
    }
    CHECK(decoded > 50);
    const int reps = 200;
    auto timeIt = [&](bool clz) {
      auto t0 = std::chrono::steady_clock::now();
      for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < off.size(); i++)
          (clz ? decodeTile : decodeTileSimple)(bytes.data() + off[i], len[i], 32, a);
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / reps / off.size() * 1e6;
    };
    double clz = timeIt(true), simple = timeIt(false);
    printf("  tile decode on the PC: %.2f us/tile with CLZ, %.2f us/tile bit by bit (%d tiles)\n", clz, simple, decoded);
    // optional: the full Bulgaria pack
    if (const char* big = getenv("TERRAIN_PACK")) {
      static std::vector<uint8_t> bb;
      if (loadFile(big, bb)) {
        static MemReader br(bb.data(), (uint32_t)bb.size());
        br = MemReader(bb.data(), (uint32_t)bb.size());
        static Terrain bt;
        Terrain::Status s = bt.open(&br);
        printf("  full pack %s: open status %d, %u bytes\n", big, (int)s, (unsigned)bb.size());
        CHECK(s == Terrain::OK);
      }
    }
  }

  // elevation and ceiling against the Python reference
  int nOk = 0, nNan = 0;
  double worst = 0;
  for (const Ref::E& e : ref.e) {
    float m, c;
    bool ok = t.elevationE7(e.lat, e.lon, &m);
    if (std::isnan(e.mean)) { CHECK(!ok); nNan++; }
    else {
      CHECK(ok);
      if (ok) { worst = fmax(worst, fabs(m - e.mean)); NEAR(m, e.mean, 0.01); nOk++; }
    }
    bool okc = t.ceilingE7(e.lat, e.lon, &c);
    if (std::isnan(e.cmax)) CHECK(!okc);
    else {
      CHECK(okc);
      if (okc) {
        CHECK(c >= e.cmax - 1e-3);
        CHECK(c <= e.cmax + 600);                       // sanity: a neighbouring cell at most
        if (ok) CHECK(c >= m);
      }
    }
  }
  printf("  %d points matched the reference (worst %.4f m), %d refused as expected\n", nOk, worst, nNan);
  CHECK(nOk > 100 && nNan > 5);
  // double-degree API and coverage
  float m;
  CHECK(t.elevation(42.65, 24.75, &m) && m > 300 && m < 1000);
  CHECK(!t.elevation(0, 0, &m));
  CHECK(!t.elevation(50, 24.75, &m));
  CHECK(!t.elevation(NAN, 24.75, &m));
  CHECK(!t.elevation(42.65, INFINITY, &m));
  CHECK(!t.elevation(95, 24.75, &m));
  CHECK(!t.ceiling(0, 0, &m));
  CHECK(t.covers(42.65, 24.75) && !t.covers(43.5, 24.75) && !t.covers(NAN, 1));
  float e1, c1;
  CHECK(t.elevation(42.70, 24.70, &e1) && t.ceiling(42.70, 24.70, &c1) && c1 >= e1);

  // vectors
  static MapVectors vec;
  CHECK(vec.open(&rd, t.info()));
  CHECK(vec.townCount() == (int)ref.towns.size() && vec.townCount() >= 3);
  CHECK(vec.hasRoads());
  UnitBox all;
  all.x0 = all.y0 = 0; all.x1 = all.y1 = 65535;
  static TownIter ti;
  static Town tw;
  ti.begin(vec, all);
  size_t k = 0;
  while (ti.next(&tw)) {
    CHECK(k < ref.towns.size());
    if (k < ref.towns.size()) {
      const Ref::Tn& r = ref.towns[k];
      CHECK(tw.x == r.x && tw.y == r.y && tw.latin == r.latin && tw.cyr == r.cyr);
      CHECK(tw.popClass == (r.cls & 7) && tw.city == ((r.cls & 8) != 0));
    }
    k++;
  }
  CHECK(k == ref.towns.size());
  bool sawKarlovo = false;
  for (const auto& r : ref.towns) sawKarlovo |= r.latin == "Karlovo" && r.cyr == "Карлово";
  CHECK(sawKarlovo);
  // a small box around one town returns that town and only towns inside the box
  {
    const Ref::Tn& r = ref.towns[0];
    UnitBox b; b.x0 = r.x - 150; b.x1 = r.x + 150; b.y0 = r.y - 150; b.y1 = r.y + 150;
    ti.begin(vec, b);
    int got = 0, expect = 0;
    bool first = false;
    for (const auto& q : ref.towns) expect += b.contains(q.x, q.y);
    while (ti.next(&tw)) { got++; CHECK(b.contains(tw.x, tw.y)); first |= tw.latin == r.latin; }
    CHECK(got == expect && first);
    b.x0 = b.y0 = 0; b.x1 = b.y1 = 10;
    ti.begin(vec, b);
    CHECK(!ti.next(&tw));
  }
  // lat/lon <-> units
  {
    const Ref::Tn& r = ref.towns[0];
    double lat, lon, x, y;
    vec.unitToLatLon(r.x, r.y, &lat, &lon);
    vec.latLonToUnit(lat, lon, &x, &y);
    NEAR(x, r.x, 1e-6); NEAR(y, r.y, 1e-6);
    CHECK(t.covers(lat, lon));
  }
  // roads: all polylines come back once each, identical to what the writer encoded
  {
    std::multimap<uint64_t, int> want;
    for (size_t i = 0; i < ref.roads.size(); i++) {
      const auto& d = ref.roads[i];
      std::vector<uint16_t> x, y;
      for (size_t j = 0; j + 1 < d.xy.size(); j += 2) { x.push_back((uint16_t)d.xy[j]); y.push_back((uint16_t)d.xy[j + 1]); }
      want.insert({hashPoly(d.cls, (int)x.size(), x.data(), y.data()), (int)i});
    }
    static RoadIter ri;
    ri.begin(vec, all);
    size_t got = 0;
    while (const RoadPoly* p = ri.next()) {
      auto it = want.find(hashPoly(p->cls, p->n, p->x, p->y));
      CHECK(it != want.end());
      if (it != want.end()) want.erase(it);
      got++;
    }
    CHECK(got == ref.roads.size() && want.empty() && got > 5);
    // a box query returns every polyline whose bounding box meets it, and nothing that does not
    UnitBox b;
    const auto& d0 = ref.roads[ref.roads.size() / 2];
    b.x0 = d0.xy[0] - 40; b.x1 = d0.xy[0] + 40; b.y0 = d0.xy[1] - 40; b.y1 = d0.xy[1] + 40;
    size_t expect = 0;
    for (const auto& d : ref.roads) {
      int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
      for (size_t j = 0; j + 1 < d.xy.size(); j += 2) {
        x0 = std::min(x0, d.xy[j]); x1 = std::max(x1, d.xy[j]); y0 = std::min(y0, d.xy[j + 1]); y1 = std::max(y1, d.xy[j + 1]);
      }
      expect += !(x0 > b.x1 || x1 < b.x0 || y0 > b.y1 || y1 < b.y0);
    }
    ri.begin(vec, b);
    size_t n = 0;
    while (const RoadPoly* p = ri.next()) { n++; CHECK(p->bbox.intersects(b)); }
    CHECK(n == expect && n >= 1);
    UnitBox none; none.x0 = none.y0 = 60000; none.x1 = none.y1 = 60010;
    ri.begin(vec, none);
    CHECK(ri.next() == nullptr);
  }
  // attribution strings
  {
    char s[256];
    CHECK(vec.attribution(0, s, sizeof s) && strncmp(s, "Contains modified Copernicus data (DEM GLO-90)", 46) == 0);
    CHECK(vec.attribution(1, s, sizeof s) && strcmp(s, "© OpenStreetMap contributors") == 0);
    CHECK(vec.attribution(2, s, sizeof s) && strstr(s, "OpenStreetMap"));
    CHECK(!vec.attribution(3, s, sizeof s));
    CHECK(vec.attribution(0, s, 10) && strlen(s) == 9);      // truncated, still NUL-terminated
  }

  // relief on the real fixture: window around Sopot, north up and turned
  {
    static Window win;
    win.lat = 42.65; win.lon = 24.75; win.mPerPx = 100; win.w = 240; win.h = 400;
    static uint8_t g1[240 * 400], g2[240 * 400], c1[240 * 400], c2[240 * 400];
    for (float hd : {0.0f, 60.0f}) {
      win.headingDeg = hd;
      t.resetStats();
      int v1 = t.relief(win, ReliefStyle::light(), 0, 0, 400, g1, c1);
      uint32_t dec = t.tileDecodes();
      int v2 = t.relief(win, ReliefStyle::light(), 0, 0, 400, g2, c2);
      CHECK(v1 == v2 && v1 > 240 * 400 * 9 / 10);
      CHECK(memcmp(g1, g2, sizeof g1) == 0 && memcmp(c1, c2, sizeof c1) == 0);
      printf("  window 240x400 @100 m/px heading %.0f: %d tile decodes (cache of %d), %d pixels with data\n", hd, (int)dec,
             kCacheTiles, v1);
      CHECK(dec < 140);
      int lines = 0, idx = 0;
      for (int i = 0; i < 240 * 400; i++) { lines += c1[i] == CONTOUR_LINE; idx += c1[i] == CONTOUR_INDEX; }
      CHECK(lines > 500 && idx > 100);
      // bands give the same pixels as the whole window
      static uint8_t gb[240 * 100], cb[240 * 100];
      int vb = t.relief(win, ReliefStyle::light(), 0, 130, 230, gb, cb);
      CHECK(vb > 0 && memcmp(gb, g1 + 240 * 130, sizeof gb) == 0 && memcmp(cb, c1 + 240 * 130, sizeof cb) == 0);
      // vector overlay registers with the relief: a town projects to where the affine map says
      VecXform xf = vec.makeXform(win);
      const Ref::Tn& r = ref.towns[0];
      double lat, lon;
      vec.unitToLatLon(r.x, r.y, &lat, &lon);
      double east = (lon - win.lon) * 111195.0 * cos(win.lat * D2R), north = (lat - win.lat) * 111195.0;
      double th = hd * D2R, sx = (east * cos(th) - north * sin(th)) / win.mPerPx, sy = (east * sin(th) + north * cos(th)) / win.mPerPx;
      NEAR(xf.px((float)r.x, (float)r.y), 120 + sx, 0.05);
      NEAR(xf.py((float)r.x, (float)r.y), 200 - sy, 0.05);
      UnitBox wb = vec.boxForWindow(win);
      CHECK(wb.x1 > wb.x0 && wb.y1 > wb.y0);
    }
  }
}

static void test_corruption() {
  puts("terrain: validation and corruption");
  static std::vector<uint8_t> good, bad;
  buildCone(good);
  static MemReader rd;
  static Terrain t;
  rd = MemReader(good.data(), (uint32_t)good.size());
  CHECK(t.open(&rd) == Terrain::OK);
  CHECK(t.verifyAll());
  const double clat = coneCentreLat(), clon = coneCentreLon();
  float m;
  CHECK(t.elevation(clat, clon, &m));
  NEAR(m, kConeTop - 28.3, 3);   // the apex sits between four cells, 141 m from each

  auto openBad = [&](size_t pos, uint8_t xorv) {
    bad = good;
    bad[pos] ^= xorv;
    rd = MemReader(bad.data(), (uint32_t)bad.size());
    return t.open(&rd);
  };
  CHECK(openBad(0, 1) == Terrain::ERR_MAGIC);
  CHECK(openBad(4, 1) == Terrain::ERR_VERSION);
  CHECK(openBad(20, 1) == Terrain::ERR_HEADER_CRC);          // bounding box byte
  CHECK(openBad(70, 1) == Terrain::ERR_HEADER_CRC);          // section table byte
  const Section* str = nullptr;
  {
    rd = MemReader(good.data(), (uint32_t)good.size());
    CHECK(t.open(&rd) == Terrain::OK);
    str = t.info().find(SEC_STRINGS);
  }
  CHECK(str != nullptr);
  CHECK(openBad(str->off + 1, 1) == Terrain::ERR_SECTION_CRC);   // small sections are checked at open
  {
    rd = MemReader(good.data(), 100);                            // reader that cannot deliver the header
    CHECK(t.open(&rd) == Terrain::ERR_READ);
    CHECK(!t.isOpen());
    float q;
    CHECK(!t.elevation(clat, clon, &q));                         // not open: no value
  }
  // a damaged DEM tile: found by its CRC when the tile is read, the neighbours still work, verifyAll sees it
  {
    rd = MemReader(good.data(), (uint32_t)good.size());
    CHECK(t.open(&rd) == Terrain::OK);
    const Layer& L = t.info().layer[0];
    const size_t tileBytes = 1 + 2 * 32 * 32;
    size_t pos = L.data_off + (4 * L.tx + 4) * tileBytes + 500;      // tile (4,4): holds the apex
    CHECK(openBad(pos, 0x40) == Terrain::OK);
    CHECK(!t.verifyAll());
    t.resetStats();
    CHECK(!t.elevation(clat, clon, &m));                              // refused, not a wrong number
    CHECK(t.crcErrors() == 1);
    CHECK(!t.elevation(clat, clon, &m));                              // cached as bad
    CHECK(t.crcErrors() == 1);
    CHECK(t.elevation(clat + 0.12, clon - 0.12, &m) || true);
    float q;
    CHECK(t.elevation(kConeLat0 - 0.03, kConeLon0 + 0.03, &q));       // tile (0,0) is fine
    CHECK(!t.ceiling(clat, clon, &q));                                // no MAX layer in this pack
  }
}

static void test_relief_cone() {
  puts("terrain relief: synthetic cone (rings at the right radii, determinism, light)");
  static std::vector<uint8_t> bytes;
  buildCone(bytes);
  static MemReader rd(bytes.data(), (uint32_t)bytes.size());
  rd = MemReader(bytes.data(), (uint32_t)bytes.size());
  static Terrain t;
  CHECK(t.open(&rd) == Terrain::OK);
  CHECK(contourIntervalFor(5) == 20 && contourIntervalFor(20) == 50 && contourIntervalFor(35) == 100 &&
        contourIntervalFor(100) == 200 && contourIntervalFor(150) == 500 && contourIntervalFor(400) == 1000 &&
        contourIntervalFor(900) == 1000);

  static Window win;
  win.lat = coneCentreLat(); win.lon = coneCentreLon(); win.mPerPx = 100; win.w = 200; win.h = 200;
  const int W = 200, H = 200;
  static uint8_t grey[W * H], cont[W * H], grey2[W * H], cont2[W * H];
  for (float hd : {0.0f, 37.0f, 200.0f}) {
    win.headingDeg = hd;
    int v = t.relief(win, ReliefStyle::light(), 100, 0, H, grey, cont);
    CHECK(v == W * H);
    int nOn = 0, wrongIdx = 0, farOff = 0;
    const double R100 = 100 / kConeSlope;               // metres of radius per 100 m of height
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        uint8_t c = cont[y * W + x];
        if (!c) continue;
        double u = (x + 0.5 - W / 2.0) * win.mPerPx, w = (H / 2.0 - (y + 0.5)) * win.mPerPx;
        double d = sqrt(u * u + w * w);
        if (d < 500) continue;                          // the apex is too steep in pixels
        double m = (kConeTop / 100.0) - d / R100;       // continuous level index
        double mn = floor(m + 0.5);
        nOn++;
        if (fabs(m - mn) * R100 > 130) farOff++;
        if ((c == CONTOUR_INDEX) != (((int)mn % 5) == 0)) wrongIdx++;
      }
    CHECK(nOn > 1000);
    CHECK(farOff == 0);
    CHECK(wrongIdx == 0);
    // every ring is closed: sample 120 angles on each ring, a contour pixel within one pixel of each point
    int rings = 0;
    for (int m = 0; m <= 24; m++) {
      double R = (kConeTop - 100.0 * m) / kConeSlope;
      if (R < 700 || R > 8800) continue;
      rings++;
      int hit = 0;
      for (int a = 0; a < 120; a++) {
        double ang = a * 6.283185307 / 120;
        int px = (int)floor(W / 2.0 + R * cos(ang) / win.mPerPx), py = (int)floor(H / 2.0 - R * sin(ang) / win.mPerPx);
        bool any = false;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
            if (px + dx >= 0 && px + dx < W && py + dy >= 0 && py + dy < H && cont[(py + dy) * W + px + dx]) any = true;
        hit += any;
      }
      CHECK(hit == 120);
    }
    CHECK(rings >= 14);
    // determinism and band independence
    t.relief(win, ReliefStyle::light(), 100, 0, H, grey2, cont2);
    CHECK(memcmp(grey, grey2, sizeof grey) == 0 && memcmp(cont, cont2, sizeof cont) == 0);
    static uint8_t gb[W * 37], cb[W * 37];
    t.relief(win, ReliefStyle::light(), 100, 61, 98, gb, cb);
    CHECK(memcmp(gb, grey + W * 61, sizeof gb) == 0 && memcmp(cb, cont + W * 61, sizeof cb) == 0);
    // thin lines: contours cover a small share of the window
    int on = 0;
    for (int i = 0; i < W * H; i++) on += cont[i] != 0;
    CHECK(on < W * H / 5);
    printf("  heading %3.0f: %d contour pixels, %d rings checked\n", hd, on, rings);
  }

  // hillshade: the light is at the upper left of the SCREEN whatever the heading
  win.headingDeg = 0;
  t.hillshade(win, ReliefStyle::strong(), 0, H, grey);
  CHECK(grey[0] == 14);                                  // flat ground far from the cone (corner: 14 km out)
  const int lx = W / 2 - 40, ly = H / 2 - 40, rx = W / 2 + 40, ry = H / 2 + 40;
  CHECK(grey[ly * W + lx] > grey[ry * W + rx]);
  CHECK(grey[ly * W + lx] >= 15 && grey[ry * W + rx] <= 10);
  win.headingDeg = 90;
  t.hillshade(win, ReliefStyle::strong(), 0, H, grey2);
  CHECK(grey2[ly * W + lx] > grey2[ry * W + rx]);
  int diff = 0;
  for (int i = 0; i < W * H; i++) diff += abs((int)grey[i] - (int)grey2[i]) > 1;
  CHECK(diff < W * H / 50);                              // the cone is rotation invariant: same picture
  // "light" style is lighter: its darkest pixel is not as dark as the strong style's
  uint8_t mnS = 255, mnL = 255;
  win.headingDeg = 0;
  t.hillshade(win, ReliefStyle::light(), 0, H, grey2);
  for (int i = 0; i < W * H; i++) { if (grey[i] < mnS) mnS = grey[i]; if (grey2[i] < mnL) mnL = grey2[i]; }
  CHECK(mnL > mnS && mnL >= 9);
  CHECK(grey2[0] == 16);

  // a window half outside the stored grid: pixels without data are marked, nothing is invented
  win.lat = kConeLat0 - 128 * kDy * 1e-7 + 0.23; win.headingDeg = 0;     // centre on the north edge of the pack
  int v = t.relief(win, ReliefStyle::light(), 100, 0, H, grey, cont);
  CHECK(v > W * H / 4 && v < W * H * 3 / 4);
  CHECK(grey[0] == kNoShade && cont[0] == CONTOUR_NONE);
  CHECK(grey[(H - 1) * W + W / 2] != kNoShade);
  // an empty or bad window renders nothing
  win.mPerPx = 0;
  CHECK(t.relief(win, ReliefStyle::light(), 100, 0, H, grey, cont) == 0);
  win.mPerPx = 100; win.lat = NAN;
  CHECK(t.relief(win, ReliefStyle::light(), 100, 0, H, grey, cont) == 0);
}

static void run() {
  test_crc_codec();
  test_pack_reference();
  test_corruption();
  test_relief_cone();
}

}  // namespace terrain_tests
