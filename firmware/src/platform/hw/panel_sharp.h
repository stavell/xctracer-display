#pragma once
#include "platform/util/fastmem.h"
#include <Adafruit_SharpMem.h>
#include "platform/hw/fast_sharp.h"
#include "platform/ports.h"

// The 400x240 Sharp memory display: SPI refresh, change detection, and the VCOM flip (at least once a second) that
// keeps the liquid crystal from degrading. EXTCOMIN is not wired: VCOM is the bit in each SPI command.
class SharpPanel : public IPanel {
 public:
  SharpPanel();
  void begin();
  Adafruit_GFX& gfx() override { return display_; }
  Bitmap1 bitmap() override { return display_.bm(); }
  void setRotation(uint8_t r) override { display_.setRotation(r); }
  void clear() override { fastFill(display_.getBuffer(), 0xFF, 400 * 240 / 8); }
  void present() override;
  void invalidate() override { dirty_ = true; }
  void refresh() override;
  void showRaw(const uint8_t* frame) override;
  bool ready() override;                  // the previous transfer is done: present() will not wait
  void stopVcom();                        // last transfer done, no more VCOM flips (before System OFF)
  void vcomPulse();                       // VCOM flip over SPI, no data (the clock sleep, once a second)
  void keepVcom(uint32_t nowMs);          // from the loop: a VCOM flip when nothing was sent for a second
  // Blocks until the in-flight transfer has finished (aborts it after 50 ms). Call before anything
  // that stops or powers down the panel.
  void waitIdle();

  // Diagnostics. refreshUs: CPU time in the last present()/refresh() (wait + pack + start).
  uint32_t refreshUs() const { return refreshUs_; }
  uint32_t sendUs() const;                              // start to done of the last transfer (DWT)
  uint32_t waitUs() const { return waitUs_; }           // time the last send spent waiting for the previous one
  uint32_t takeWaitUs() { uint32_t w = waitAccUs_; waitAccUs_ = 0; return w; }   // all waiting for a transfer since the last call
  uint32_t timeouts() const { return timeouts_; }       // transfers aborted after 50 ms

  int lastLines() const { return lastLines_; }          // lines sent by the last refresh

 private:
  void send(const uint8_t* idx, int n);   // pack, update the snapshot, start the DMA
  void sendAll();
  int lastLines_ = 0;
  uint8_t vcomBit_ = SHARPMEM_BIT_VCOM;   // mirror of the library's private _sharpmem_vcom
  FastSharp display_;
  uint32_t lastTxMs_ = 0;                 // last transfer of any kind (frame or VCOM flip)
  bool vcomStopped_ = false;
  uint8_t* snap_ = nullptr;
  bool dirty_ = true;
  bool ready_ = false;                    // SPIM2 initialised
  uint32_t refreshUs_ = 0;
  uint32_t waitUs_ = 0;
  uint32_t waitAccUs_ = 0;
  uint32_t timeouts_ = 0;
};
