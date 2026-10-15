// Host mock of the Sharp memory display: same drawing API, renders into a pixel array.
#pragma once
#include <Adafruit_GFX.h>
class SPIClass;
class Adafruit_SharpMem : public Adafruit_GFX {
 public:
  Adafruit_SharpMem(SPIClass *, uint8_t, uint16_t w, uint16_t h, uint32_t = 0) : Adafruit_GFX(w, h) {
    memset(buf, 0xFF, sizeof buf);
  }
  bool begin() { return true; }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    switch (rotation) {
      case 1: { int16_t t = x; x = WIDTH - 1 - y; y = t; break; }
      case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
      case 3: { int16_t t = x; x = y; y = HEIGHT - 1 - t; break; }
    }
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    uint8_t m = 1 << (x & 7);
    uint8_t &b = buf[(y * WIDTH + x) / 8];
    if (color) b |= m; else b &= ~m;
  }
  void clearDisplay() { memset(buf, 0xFF, sizeof buf); }
  void refresh() {}
  uint8_t *getBuffer() { return buf; }
  void dumpPgm(const char *path) {   // logical view, 1 byte per pixel, 0 = black, 255 = white
    int w = width(), h = height();
    FILE *f = fopen(path, "wb");
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    for (int ly = 0; ly < h; ly++)
      for (int lx = 0; lx < w; lx++) {
        int x = lx, y = ly;
        switch (rotation) {
          case 1: x = WIDTH - 1 - ly; y = lx; break;
          case 2: x = WIDTH - 1 - lx; y = HEIGHT - 1 - ly; break;
          case 3: x = ly; y = HEIGHT - 1 - lx; break;
        }
        fputc((buf[(y * WIDTH + x) / 8] >> (x & 7)) & 1 ? 255 : 0, f);
      }
    fclose(f);
  }
 private:
  uint8_t buf[(400 * 240) / 8];
};
