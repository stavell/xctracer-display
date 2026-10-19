#pragma once
// Where the flight logger puts its records: a byte sink that takes whole records (never splits one) and never blocks.
#include <stddef.h>
#include <stdint.h>

namespace storage {

struct IRecordSink {
  virtual ~IRecordSink() {}
  // One whole record (at most 100 bytes). False if it was dropped (log full, not formatted, buffer busy).
  virtual bool record(const uint8_t* rec, size_t n) = 0;
  virtual void flush() = 0;                      // push out a partly filled page (end of a session)
};

}  // namespace storage
