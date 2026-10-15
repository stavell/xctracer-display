#pragma once
#include <Adafruit_SharpMem.h>
#include "app/ports.h"

// The 400x240 Sharp memory display: SPI refresh, change detection, and the 1 Hz VCOM toggle that
// keeps the liquid crystal from degrading.
class SharpPanel : public IPanel {
 public:
  SharpPanel();
  void begin();
  Adafruit_GFX& gfx() override { return display_; }
  void setRotation(uint8_t r) override { display_.setRotation(r); }
  void clear() override { display_.clearDisplayBuffer(); }
  void present() override;
  void invalidate() override { dirty_ = true; }
  void refresh() override;
  void showRaw(const uint8_t* frame) override;
  void stopVcom();
  uint32_t refreshUs() const { return refreshUs_; }     // duration of the last SPI refresh, for diagnostics

 private:
  Adafruit_SharpMem display_;
  SoftwareTimer vcom_;
  uint8_t* snap_ = nullptr;
  bool dirty_ = true;
  uint32_t refreshUs_ = 0;
};
