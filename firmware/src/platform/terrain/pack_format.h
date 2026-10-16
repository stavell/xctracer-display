#pragma once
// Constants and parsed header of the map pack (host/terrain/FORMAT.md). Little endian throughout.
#include <stdint.h>

namespace terrain {

constexpr uint16_t kPackVersion = 1;
constexpr uint32_t kHeaderSize = 192;
constexpr int kMaxSections = 8;
constexpr uint32_t kEagerCrcLimit = 64 * 1024;           // sections up to this size are CRC-checked at open

enum SectionId : uint16_t { SEC_MEAN = 1, SEC_MAX = 2, SEC_TOWNS = 3, SEC_ROADS = 4, SEC_STRINGS = 5 };

inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
inline uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint32_t le24(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16; }

struct Section {
  uint16_t id = 0;
  uint32_t off = 0, len = 0, crc = 0;                    // absolute file offset, length, CRC32 of the section bytes
};

// One DEM layer (the mean layer or the MAX layer). All offsets are absolute file offsets.
struct Layer {
  bool present = false;
  int32_t lat0_e7 = 0, lon0_e7 = 0;                      // NW corner of the stored grid, degrees * 1e7
  int32_t dy_e7 = 1, dx_e7 = 1;                          // cell size, degrees * 1e7
  uint16_t rows = 0, cols = 0, ty = 0, tx = 0;           // cells, tiles
  uint8_t q = 1, log2t = 5, kind = 0;                    // metres per unit, tile edge, 0 mean / 1 absolute max (rounded up)
  uint32_t idx_off = 0, crc_off = 0, data_off = 0, data_len = 0;
  int tileEdge() const { return 1 << log2t; }
};

struct PackInfo {
  uint32_t fileSize = 0;
  int32_t latS_e7 = 0, latN_e7 = 0, lonW_e7 = 0, lonE_e7 = 0;   // bounding box of the stored mean grid
  int32_t originLat_e7 = 0, originLon_e7 = 0;                  // origin of the vector projection (45 N, 22 E)
  uint32_t mPerDeg = 111195;
  float cos0 = 0.7343f;                                        // fixed cos of the grid's reference latitude (42.7)
  uint16_t unitM = 10;                                         // vector coordinate unit, metres
  uint16_t nsec = 0;
  Section sec[kMaxSections];
  Layer layer[2];                                              // 0 mean, 1 max
  const Section* find(uint16_t id) const {
    for (int i = 0; i < nsec; i++) if (sec[i].id == id) return &sec[i];
    return nullptr;
  }
};

}  // namespace terrain
