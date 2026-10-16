#pragma once
#include <stddef.h>
#include <stdint.h>

// Wire format of one Sharp memory LCD "write lines" transaction, packed into a flat buffer so it can
// be sent by DMA. The panel is 240 lines of 50 bytes (native orientation, same as platform/panel/linediff.h).
static const int SHARP_LINES = 240;
static const int SHARP_LINE_BYTES = 50;
// command + 240 * (address + 50 data + trailer) + final trailer
static const size_t SHARP_PACK_MAX = (size_t)SHARP_LINES * (SHARP_LINE_BYTES + 2) + 2;

// Packs the lines idx[0..n-1] of frame (line i at frame + i * 50) as: cmd; per line: idx + 1, the 50
// bytes, 0x00; then a final 0x00. Bit order is not reversed here: the SPI peripheral sends LSB first.
// out must hold SHARP_PACK_MAX bytes. Returns the byte count, n * 52 + 2.
size_t sharpPack(const uint8_t* frame, const uint8_t* idx, int n, uint8_t cmd, uint8_t* out);
