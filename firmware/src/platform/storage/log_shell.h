#pragma once
// The text commands of the flight log on the USB serial port (host/logdl.py is the other end). One command per line:
//
//   LOGINFO            -> lg info state=ready used=N cap=M dropped=D
//   LOGREAD off len    -> lg d off HEX (one line per 128 bytes) ... then  lg ok read off len crc=CRC32
//   LOGERASE           -> lg ok erase started   (the final  lg ok erased  comes from the caller when the format is over)
//
// Bytes that are not a command line (the loader's binary frames, a terminal) are ignored. Pure C++.
#include <stddef.h>
#include <stdint.h>
#include "platform/storage/flash_log.h"

namespace storage {

struct ITextOut {
  virtual ~ITextOut() {}
  virtual void put(const char* line) = 0;            // one line, no newline
};

class LogShell {
 public:
  explicit LogShell(FlashLog& log) : log_(log) {}
  void feed(uint8_t b, ITextOut& out);
  static constexpr uint32_t kMaxRead = 2048;

 private:
  void run(ITextOut& out);
  FlashLog& log_;
  char line_[40];
  uint8_t n_ = 0;
  bool overflow_ = false;
  char buf_[300];
};

}  // namespace storage
