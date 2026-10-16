#pragma once
// CRC-32 (IEEE 802.3, the zlib / Python zlib.crc32 polynomial) with a 16-entry table: 64 bytes of flash
// instead of 1 KB, about 12 cycles per byte on a Cortex-M4. Pure C++.
#include <stddef.h>
#include <stdint.h>

namespace terrain {

// Continue a CRC: start with crc = 0 and feed chunks; the result of one call is the input of the next.
uint32_t crc32(uint32_t crc, const void* data, size_t len);

}  // namespace terrain
