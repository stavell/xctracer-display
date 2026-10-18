#pragma once
// The map loader's state machine: turns protocol frames (pack_proto.h) into flash operations and answers with one
// text line "pk ok <what> ..." or "pk err <what> ..." per request. Pure C++: the flash and the clock are passed in,
// the caller moves bytes (USB serial on the device, a pipe in the tests) and prints the reply lines.
//
//   idle --HELLO--> ready --ERASE--> erasing --> ready (erased) --WRITE x N--> ready --FINISH--> scanning --> ready
//   ready --STATUS--> scanning --> ready;  ready --EXIT--> idle (+ reboot request);  30 s of silence --> idle + reboot
//
// Safety rules: ERASE wipes the flash header first, so from then on there is "no pack"; the header is written only by
// FINISH, after the CRC of the stored bytes matched the CRC the host announced. An interrupted load therefore leaves
// no pack, never half a pack. Long operations (erase, CRC scan) advance in service() in small steps, so the loop, the
// USB stack and the display keep running.
//
// Static RAM: about 1.4 kB (frame buffer, read-back buffer, CRC scan buffer).
#include <stdint.h>
#include "platform/storage/flash_port.h"
#include "platform/storage/pack_header.h"
#include "platform/storage/pack_proto.h"

namespace storage {

constexpr uint32_t kLoaderIdleMs = 30000;            // silence after which load mode ends (with a reboot request)

class PackLoader {
 public:
  explicit PackLoader(IFlash& flash) : flash_(flash) {}

  void feedByte(uint8_t b, uint32_t nowMs);          // one byte from the host
  void service(uint32_t nowMs);                      // advance a long operation; call every loop iteration

  bool active() const { return st_ != IDLE; }        // load mode: the map must not be read
  bool busy() const { return st_ == ERASING || st_ == SCANNING; }
  // The reply line waiting to be sent (no newline), or nullptr. replyDone() after sending. While a reply is pending the
  // loader accepts no more bytes (feedByte is ignored), so the caller drains it after every byte or call.
  const char* reply() const { return replyLen_ ? reply_ : nullptr; }
  void replyDone() { replyLen_ = 0; }
  bool rebootRequested() const { return reboot_; }
  uint32_t chunksWritten() const { return chunks_; }

 private:
  enum St : uint8_t { IDLE, READY, ERASING, SCANNING };
  enum Scan : uint8_t { SCAN_FINISH, SCAN_STATUS };

  void handle(uint8_t cmd, const uint8_t* p, uint32_t n, uint32_t now);
  void hello(uint32_t now);
  void erase(const uint8_t* p, uint32_t n, uint32_t now);
  void write(const uint8_t* p, uint32_t n);
  void finish(const uint8_t* p, uint32_t n, uint32_t now);
  void status(uint32_t now);
  void exitMode(const uint8_t* p, uint32_t n);
  void serviceErase(uint32_t now);
  void scanDone(uint32_t now);
  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  IFlash& flash_;
  FrameParser parser_;
  CrcScan scan_;
  St st_ = IDLE;
  Scan scanKind_ = SCAN_FINISH;
  bool erased_ = false;
  bool reboot_ = false;
  uint32_t packLen_ = 0, wantCrc_ = 0;               // of the load in progress
  uint32_t erasePos_ = 0, eraseEnd_ = 0, opStart_ = 0, lastAct_ = 0, chunks_ = 0;
  FlashHeader stored_;                               // STATUS: the header being checked
  char reply_[128];
  uint8_t replyLen_ = 0;
  uint8_t verify_[kMaxChunk];                        // read-back of a written chunk
};

}  // namespace storage
