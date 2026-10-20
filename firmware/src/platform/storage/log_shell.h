#pragma once
// The text commands of the flight log on the USB serial port (host/logdl.py is the other end). One command per line:
//
//   LOGINFO            -> lg info state=ready used=N cap=M dropped=D
//   LOGREAD off len    -> lg d off HEX (one line per 128 bytes) ... then  lg ok read off len crc=CRC32
//   TIME epoch tz      -> tm ok epoch tz=N   (set the clock: UTC seconds since 1970 and the minutes east of UTC; tm err ...)
//   TIME?              -> tm now HH:MM   (the box's local time) or tm now unknown
//   APP name           -> app ok name   (bench only: switch to an app by its registry name, through the host's normal app switch;
//                         app err unknown name when there is none)
//   APP?               -> app now name   (the active app)
//   REBOOT             -> rb ok   (bench only: save the retained block, then a soft reset; "rb err" without a box image)
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

struct ITimeSink {
  virtual ~ITimeSink() {}
  virtual bool setTime(int64_t utcEpochSec, int tzMinutes) = 0;   // false: refused
  virtual int localMinutes() { return -1; }                       // minutes of the local day, -1 unknown (TIME? answers it)
};

// The apps of the box, for the USB bench commands APP / APP?. A debug aid: it is the only way to enter an app without the
// gestures, so the box image documents it (CLAUDE.md, flight safety) and the vario-only image has none.
struct IAppSink {
  virtual ~IAppSink() {}
  virtual bool openApp(const char* name) = 0;      // false: no such app
  virtual const char* activeApp() = 0;
  virtual bool reboot() { return false; }          // REBOOT: save what must survive, then a soft reset (does not return)
};

class LogShell {
 public:
  void setTimeSink(ITimeSink* t) { time_ = t; }
  void setAppSink(IAppSink* a) { apps_ = a; }
  explicit LogShell(FlashLog& log) : log_(log) {}
  void feed(uint8_t b, ITextOut& out);
  static constexpr uint32_t kMaxRead = 2048;

 private:
  void run(ITextOut& out);
  FlashLog& log_;
  ITimeSink* time_ = nullptr;
  IAppSink* apps_ = nullptr;
  char line_[40];
  uint8_t n_ = 0;
  bool overflow_ = false;
  char buf_[300];
};

}  // namespace storage
