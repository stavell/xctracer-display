#pragma once
// Tests for core/sharp_frame (the packed SPI transaction). Included by tests.cpp.
#include "core/sharp_frame.h"

static void test_sharp_pack() {
  puts("sharp pack");
  static uint8_t frame[SHARP_LINES * SHARP_LINE_BYTES];
  for (size_t i = 0; i < sizeof frame; i++) frame[i] = (uint8_t)(i * 7 + (i >> 8) + 1);
  static uint8_t out[SHARP_PACK_MAX + 4];
  static uint8_t idx[SHARP_LINES];

  CHECK(SHARP_PACK_MAX == 240u * 52 + 2);

  // n = 0: command and final trailer only
  memset(out, 0xEE, sizeof out);
  size_t k = sharpPack(frame, idx, 0, 0x81, out);
  CHECK(k == 2); CHECK(out[0] == 0x81); CHECK(out[1] == 0x00); CHECK(out[2] == 0xEE);

  // one line: the first, a middle one and the last
  int lines[3] = {0, 117, SHARP_LINES - 1};
  for (int li = 0; li < 3; li++) {
    int y = lines[li];
    idx[0] = (uint8_t)y;
    memset(out, 0xEE, sizeof out);
    k = sharpPack(frame, idx, 1, 0x80, out);
    CHECK(k == 54);
    CHECK(out[0] == 0x80);
    CHECK(out[1] == y + 1);
    CHECK(memcmp(out + 2, frame + y * SHARP_LINE_BYTES, SHARP_LINE_BYTES) == 0);
    CHECK(out[52] == 0x00); CHECK(out[53] == 0x00); CHECK(out[54] == 0xEE);
  }

  // all lines against a reference built byte by byte, independently of the packer's layout
  for (int i = 0; i < SHARP_LINES; i++) idx[i] = (uint8_t)i;
  memset(out, 0xEE, sizeof out);
  k = sharpPack(frame, idx, SHARP_LINES, 0x80 | 0x40, out);
  CHECK(k == SHARP_PACK_MAX);
  CHECK(out[SHARP_PACK_MAX] == 0xEE);
  bool same = out[0] == 0xC0;
  for (int y = 0; y < SHARP_LINES; y++) {
    size_t base = 1 + (size_t)y * 52;
    same &= out[base] == (uint8_t)(y + 1);                          // address byte, 1-based
    for (int x = 0; x < SHARP_LINE_BYTES; x++) same &= out[base + 1 + x] == frame[y * SHARP_LINE_BYTES + x];
    same &= out[base + 51] == 0x00;                                 // line trailer
  }
  same &= out[SHARP_PACK_MAX - 1] == 0x00;                          // final trailer
  CHECK(same);

  // a sparse, out-of-order selection keeps its order and addresses
  idx[0] = 200; idx[1] = 3; idx[2] = 4;
  k = sharpPack(frame, idx, 3, 0x00, out);
  CHECK(k == 3 * 52 + 2);
  CHECK(out[1] == 201); CHECK(out[53] == 4); CHECK(out[105] == 5);
  CHECK(memcmp(out + 54, frame + 3 * SHARP_LINE_BYTES, SHARP_LINE_BYTES) == 0);
}
