#pragma once
#include <stdint.h>

// Which lines of a line-organised frame buffer differ from the last-sent snapshot.
// Used for partial refresh of the Sharp memory LCD (native 240 lines of 50 bytes).
static const int LD_LINES = 240;
static const int LD_BYTES_PER_LINE = 50;

// Writes the indices (ascending) of lines that differ into out[], which must hold LD_LINES entries.
// Returns the count.
int changedLines(const uint8_t* cur, const uint8_t* snap, uint8_t* out);
