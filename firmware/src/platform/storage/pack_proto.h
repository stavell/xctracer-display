#pragma once
// Framing of the map loader's USB serial protocol (host -> device); the device answers with text lines "pk ...".
// Pure C++. The host side is host/load_pack.py; both must agree on every byte here.
//
//   A5 5A | cmd u8 | len u16le | payload (len bytes) | crc u32le
//
// crc = CRC-32 (zlib) of cmd, len and payload. Bytes outside a frame (the status text lines, line noise, a terminal
// program) are ignored while the parser looks for the A5 5A sync, and a frame that stops half way is dropped after
// kFrameGapMs, so nothing a stray program types can start a load or corrupt one.
#include <stddef.h>
#include <stdint.h>

namespace storage {

constexpr uint8_t kSync0 = 0xA5, kSync1 = 0x5A;
constexpr uint32_t kMaxChunk = 512;                       // data bytes of one WRITE
constexpr uint32_t kMaxPayload = 4 + kMaxChunk;
constexpr uint32_t kFrameGapMs = 300;                     // silence inside a frame after which it is abandoned

enum Cmd : uint8_t {
  CMD_HELLO = 1,    // "LOADPACK": enter load mode (the map is unreadable from now on)
  CMD_ERASE = 2,    // u32 pack length: invalidate the old pack, erase header sector + room for the new one
  CMD_WRITE = 3,    // u32 pack offset + data (1..kMaxChunk bytes)
  CMD_FINISH = 4,   // u32 pack length + u32 CRC-32 of the pack: check the stored bytes, then write the header
  CMD_STATUS = 5,   // check the stored header and pack (whole-pack CRC) and report
  CMD_EXIT = 6,     // u8 reboot (1): leave load mode
};

size_t encodeFrame(uint8_t* out, size_t cap, uint8_t cmd, const uint8_t* payload, size_t len);   // 0 if it does not fit

class FrameParser {
 public:
  enum Result : uint8_t { NONE, FRAME, BAD };            // BAD: a frame with a wrong CRC or an impossible length
  Result feed(uint8_t b, uint32_t nowMs);
  uint8_t cmd() const { return cmd_; }
  const uint8_t* payload() const { return buf_; }
  uint32_t length() const { return len_; }
  void reset() { st_ = S0; }

 private:
  enum St : uint8_t { S0, S1, CMD, L0, L1, DATA, C0, C1, C2, C3 };
  St st_ = S0;
  uint8_t cmd_ = 0;
  uint32_t len_ = 0, pos_ = 0, crc_ = 0, last_ = 0;
  uint8_t buf_[kMaxPayload];
};

}  // namespace storage
