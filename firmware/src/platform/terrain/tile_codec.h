#pragma once
// Decoder of one DEM tile (MED/LOCO-I predictor + Rice(k), see host/terrain/FORMAT.md). Ported from
// host/terrain/tdec.h. Pure C++, no heap, no locals above a few words: safe on the 4 KB loop stack.
#include <stdint.h>

namespace terrain {

constexpr int kMaxTileEdge = 32;                                  // cells; the pack's tile edge is 1 << log2t <= this
constexpr int kMaxTileCells = kMaxTileEdge * kMaxTileEdge;
constexpr uint32_t kMaxTileBytes = 1 + 2 * kMaxTileCells;         // raw-mode tile: the largest coded tile

// Decode one coded tile of T x T cells (T a power of two <= kMaxTileEdge) into out[T*T], row-major.
// False if the bytes are malformed (unknown mode, truncated, bit stream overrun); out is then undefined.
// decodeTile() reads the unary part with a count-leading-zeros instruction; decodeTileSimple() reads it a
// bit at a time (the reference; tests check both give the same cells).
bool decodeTile(const uint8_t* s, uint32_t len, int T, int16_t* out);
bool decodeTileSimple(const uint8_t* s, uint32_t len, int T, int16_t* out);

}  // namespace terrain
