#pragma once
// BMP280 (the Feather Sense's barometer): the datasheet's compensation, pure so it is tested against the
// datasheet's own example (emu/tests). The bus side (forced single measurement) is platform/hw/baro_bmp.cpp.
#include <stdint.h>

struct Bmp280Cal {
  uint16_t t1 = 0, p1 = 0;
  int16_t t2 = 0, t3 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;

  // Fills from the 24 bytes at register 0x88 (little endian: T1..T3, P1..P9).
  void parse(const uint8_t* b) {
    auto u = [&](int i) { return (uint16_t)(b[i] | (b[i + 1] << 8)); };
    t1 = u(0); t2 = (int16_t)u(2); t3 = (int16_t)u(4);
    p1 = u(6); p2 = (int16_t)u(8); p3 = (int16_t)u(10); p4 = (int16_t)u(12); p5 = (int16_t)u(14);
    p6 = (int16_t)u(16); p7 = (int16_t)u(18); p8 = (int16_t)u(20); p9 = (int16_t)u(22);
  }

  // Raw 20-bit temperature -> degrees C (the datasheet's integer variant).
  float tempC(int32_t adcT) const {
    const int32_t v1 = ((((adcT >> 3) - ((int32_t)t1 * 2))) * (int32_t)t2) >> 11;
    const int32_t v2 = (((((adcT >> 4) - (int32_t)t1) * ((adcT >> 4) - (int32_t)t1)) >> 12) * (int32_t)t3) >> 14;
    return (float)(((v1 + v2) * 5 + 128) >> 8) / 100.0f;
  }

  // Raw 20-bit temperature and pressure -> Pa (the datasheet's integer variant with 64-bit maths). 0 when invalid.
  uint32_t pressurePa(int32_t adcT, int32_t adcP) const {
    const int32_t v1 = ((((adcT >> 3) - ((int32_t)t1 * 2))) * (int32_t)t2) >> 11;
    const int32_t v2 = (((((adcT >> 4) - (int32_t)t1) * ((adcT >> 4) - (int32_t)t1)) >> 12) * (int32_t)t3) >> 14;
    const int32_t tFine = v1 + v2;
    int64_t a = (int64_t)tFine - 128000;
    int64_t b = a * a * (int64_t)p6;
    b += a * (int64_t)p5 * 131072;                          // << 17 (multiplied: the operands can be negative)
    b += (int64_t)p4 * 34359738368LL;                       // << 35
    a = ((a * a * (int64_t)p3) >> 8) + (a * (int64_t)p2 * 4096);
    a = (((int64_t)1 << 47) + a) * (int64_t)p1 >> 33;
    if (a == 0) return 0;
    int64_t p = 1048576 - adcP;
    p = (((p * 2147483648LL) - b) * 3125) / a;
    a = ((int64_t)p9 * (p >> 13) * (p >> 13)) >> 25;
    b = ((int64_t)p8 * p) >> 19;
    p = ((p + a + b) >> 8) + (int64_t)p7 * 16;
    return p > 0 ? (uint32_t)(p >> 8) : 0;                  // Q24.8 -> Pa
  }
};
