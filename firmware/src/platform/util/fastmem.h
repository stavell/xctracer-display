#pragma once
// memset / memcpy / memcmp replacements that move a word at a time. newlib-nano's versions on the nRF52840 are byte loops: measured on
// the device 6.3 cycles per byte for memset, 8.1 for memcpy (a 12 kB frame clear took 1.1 ms; this is about 0.3 ms). Use them for
// buffers of a few dozen bytes and up in per-frame work. Pure C++: the PC build uses the same code.
#include <stddef.h>
#include <stdint.h>

void fastFill(void* dst, uint8_t value, size_t n);
void fastCopy(void* dst, const void* src, size_t n);       // the ranges must not overlap
bool fastEqual(const void* a, const void* b, size_t n);
