#pragma once
// Terrain: elevation, ceiling (terrain maximum) and relief rendering (hillshade + contours) from a map pack
// (format: host/terrain/FORMAT.md). Pure C++, float maths, int32 grid indexing; the pack is read through an
// IBlockReader (QSPI on the device). It never invents a value: outside the stored area, on a tile that is
// not stored, on a read error or on a tile whose CRC fails, the query returns false.
//
//   static Terrain terrain;                       // ~20 kB of tile cache: a global, never a local
//   MemReader flash(bytes, size);
//   if (terrain.open(&flash) != Terrain::OK) ...  // header, section table, small sections checked
//   float m;
//   if (terrain.elevation(lat, lon, &m)) ...      // bilinear on the 200 m mean layer
//   if (terrain.ceiling(lat, lon, &m)) ...        // highest terrain around the point, conservative
//
// Integrity: open() checks the header CRC and every section of at most 64 kB (towns, roads, strings), so the
// vector layers are known good before they are drawn. The DEM sections (1.4 MB + 0.2 MB) are checked lazily:
// each tile has its own CRC32 in the pack, verified every time a tile is read from flash (~5k cycles on
// a ~420 B tile against ~60k for the decode). A full-file check (about 1.7 MB x 12 cycles = 0.3 s on the
// M4 plus the flash reads) is available as verifyAll(), meant for after a transfer, not for boot.
#include <stddef.h>
#include <stdint.h>
#include "platform/terrain/block_reader.h"
#include "platform/terrain/pack_format.h"
#include "platform/terrain/tile_codec.h"

namespace terrain {

constexpr int kCacheTiles = 9;                 // 3 x 3 tiles of 32 x 32 cells: 18 kB
constexpr int kMaxStrip = 64;                  // relief renders columns in strips of at most this many pixels

// A map window: what the relief renderer paints into a w x h pixel grid.
struct Window {
  double lat = 0, lon = 0;                     // centre, degrees
  float mPerPx = 100;                          // ground metres per pixel
  int16_t w = 240, h = 400;
  float headingDeg = 0;                        // direction shown at the top of the window, 0 = north up
};

// How heights become grey levels 0 (black) .. 16 (white) for the gfx dither. The light always comes from the
// upper left of the SCREEN, so the relief does not flip while the window turns. A slope contributes
// d = lambert - flat; the curve d|d| / (|d| + knee) keeps gentle hills quiet (the study found the plain
// hillshade noisy in rolling terrain) and lets steep faces through.
struct ReliefStyle {
  float exaggeration = 2.0f;                   // vertical
  float knee = 0.10f;                          // larger = quieter gentle slopes
  float flat = 14.0f;                          // level of level ground
  float darkest = 3.0f;                        // level of the steepest shaded face
  float lightest = 16.0f;                      // level of the steepest lit face
  static ReliefStyle strong() { return ReliefStyle(); }
  // Toned down so black contour lines stay readable on top.
  static ReliefStyle light() {
    ReliefStyle s;
    s.exaggeration = 1.5f; s.knee = 0.15f; s.flat = 16.0f; s.darkest = 10.0f;
    return s;
  }
};

constexpr uint8_t kNoShade = 255;              // grey code for "no data here"

// Contour interval (m) that keeps lines at least ~4 pixels apart on a 25 degree slope: the smallest of
// 20, 50, 100, 200, 500, 1000 that is >= 2 x mPerPx (100 m/px: 200 m, 50 m/px: 100 m, 25 m/px: 50 m).
int contourIntervalFor(float mPerPx);
// Contour codes in the output mask.
enum : uint8_t { CONTOUR_NONE = 0, CONTOUR_LINE = 1, CONTOUR_INDEX = 2 };
constexpr int kIndexEvery = 5;                 // every 5th contour is an index contour

class Terrain {
 public:
  enum Status : uint8_t { OK, ERR_READ, ERR_MAGIC, ERR_VERSION, ERR_HEADER_CRC, ERR_LAYOUT, ERR_SECTION_CRC, ERR_NO_DEM };

  Terrain() { flush(); }

  // Validate and attach to a pack. The reader must outlive the Terrain.
  Status open(IBlockReader* reader);
  bool isOpen() const { return reader_ != nullptr; }
  const PackInfo& info() const { return info_; }
  IBlockReader* reader() const { return reader_; }

  // Whole-file CRC and every section CRC. Slow (reads the whole pack); false on any mismatch or read error.
  bool verifyAll();

  // Bilinear height (m) on the mean layer. False outside the coverage, where a needed tile is not stored,
  // on a read error or CRC failure, and for non-finite input.
  bool elevation(double lat, double lon, float* m);
  bool elevationE7(int32_t lat_e7, int32_t lon_e7, float* m);
  // Highest terrain near the point (m): the MAX layer's cells within 0.15 cell of the point (up to 2 x 2 of
  // 500 m), never below elevation() where that exists. For "terrain above me" logic, not for drawing.
  bool ceiling(double lat, double lon, float* m);
  bool ceilingE7(int32_t lat_e7, int32_t lon_e7, float* m);
  // Inside the stored rectangle (tiles outside the border buffer may still be missing).
  bool covers(double lat, double lon) const;

  // Relief of rows [y0, y1) of the window. grey: w*(y1-y0) bytes, levels 0..16 or kNoShade; contour: same size,
  // CONTOUR_* codes. Either pointer may be null. interval <= 0 picks contourIntervalFor(mPerPx). Returns the
  // number of pixels that have data (0: nothing to draw). Rendering in bands (e.g. 40 rows) keeps the buffers
  // small; the result does not depend on the band split. Deterministic.
  int relief(const Window& win, const ReliefStyle& style, int interval, int y0, int y1, uint8_t* grey, uint8_t* contour);
  int hillshade(const Window& win, const ReliefStyle& style, int y0, int y1, uint8_t* grey) {
    return relief(win, style, 0, y0, y1, grey, nullptr);
  }
  int contours(const Window& win, int interval, int y0, int y1, uint8_t* contour) {
    return relief(win, ReliefStyle(), interval, y0, y1, nullptr, contour);
  }

  // Cache statistics.
  uint32_t tileDecodes() const { return decodes_; }
  uint32_t crcErrors() const { return crcErrors_; }
  uint32_t readErrors() const { return readErrors_; }
  void resetStats() { decodes_ = crcErrors_ = readErrors_ = 0; }
  void flush();                                // forget every cached tile

 private:
  const int16_t* tile(int layer, uint32_t ti);
  bool cell(int layer, int r, int c, int* v);
  bool bilinear(int layer, int r, int c, float fr, float fc, float* h);
  static void split(int64_t num2, int32_t den2, int* whole, float* frac);

  IBlockReader* reader_ = nullptr;
  PackInfo info_;
  uint32_t key_[kCacheTiles];
  uint32_t stamp_[kCacheTiles];
  bool ok_[kCacheTiles];
  uint32_t clock_ = 0;
  uint32_t mruKey_ = 0xFFFFFFFFu;
  const int16_t* mruTile_ = nullptr;
  uint32_t decodes_ = 0, crcErrors_ = 0, readErrors_ = 0;
  int16_t tiles_[kCacheTiles][kMaxTileCells];
  uint8_t coded_[kMaxTileBytes];               // flash read buffer for one coded tile
  float rows_[3][kMaxStrip + 2];               // relief: height rows of the strip being shaded
};

}  // namespace terrain
