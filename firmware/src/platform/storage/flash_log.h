#pragma once
// The flight log: a append-only record store in the tail of the QSPI flash (the map pack gives that room up, see
// flash_view.h). Pure C++ over IFlash; the loop calls service() and nothing here ever waits for the chip.
//
//   region (kLogBytes at the end of the chip), 256-byte pages:
//   page 0      header: "BLOG", u16 version = 1, u16 page size, u32 region size; the rest stays erased
//   page 1...   data pages: byte 0 = 0xA5 (so a written page never starts with 0xFF), then 255 bytes of whole records, padded
//               with 0xFF. A record never spans pages. The first page that starts with 0xFF is the end of the log.
//
// Writing: records are collected in a RAM page; a full page moves to a second RAM page and is programmed by service()
// when the chip is idle (a page program is 0.5 to 3 ms, once per 2.5 s at the usual rate). If that second page is still
// waiting, the record is dropped and counted. Erasing (format) happens only on the host's command, never during flight:
// the chip cannot be read while it erases, so the map is locked meanwhile (erasing()).
// Static RAM: about 560 bytes.
#include <stddef.h>
#include <stdint.h>
#include "platform/storage/flash_port.h"
#include "platform/storage/record_sink.h"

namespace storage {

constexpr uint32_t kLogBytes = 256u * 1024u;        // the region: 4 blocks of 64 kB at the end of the 2 MB chip
constexpr uint32_t kLogPage = 256;
constexpr uint8_t kLogPageMark = 0xA5;

class FlashLog : public IRecordSink {
 public:
  enum State : uint8_t { OFF, UNFORMATTED, READY, FULL, ERASING, ERROR };
  // Look at the region. `base` = start of the region (block aligned). OFF if the chip is too small or does not answer.
  void attach(IFlash* flash, uint32_t base, uint32_t bytes = kLogBytes);
  State state() const { return st_; }
  const char* stateName() const;
  bool recording() const { return st_ == READY; }
  bool erasing() const { return st_ == ERASING; }

  bool record(const uint8_t* rec, size_t n) override;
  void flush() override;
  void service(uint32_t nowMs);                      // program a waiting page; advance the erase; flush an old partial page

  // Start the format (erase the region, write the header). Only when nothing is being written for sure: false if busy.
  bool startErase();
  // Raw bytes of the written pages (page marks included), offset 0 = start of page 1. Reads at most usedBytes().
  uint32_t usedBytes() const { return ((haveOut_ ? outPage_ : next_) - 1) * kLogPage; }   // programmed pages only
  uint32_t capacityBytes() const { return (pages_ - 1) * kLogPage; }
  bool read(uint32_t off, void* dst, uint32_t n);
  uint32_t dropped() const { return dropped_; }

 private:
  bool commit();
  void startCur();
  IFlash* f_ = nullptr;
  uint32_t base_ = 0, pages_ = 0, next_ = 1;         // next_: the page index the next committed page will take
  State st_ = OFF;
  uint8_t cur_[kLogPage], out_[kLogPage];
  uint32_t fill_ = 1, outPage_ = 0, dropped_ = 0, curSinceMs_ = 0, nowMs_ = 0;
  bool haveOut_ = false;
  uint8_t eraseStep_ = 0;
};

}  // namespace storage
