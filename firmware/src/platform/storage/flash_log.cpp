#include "platform/storage/flash_log.h"
#include <string.h>

namespace storage {

static const uint8_t kMagic[4] = {'B', 'L', 'O', 'G'};
static const uint32_t kFlushAgeMs = 10000;           // a partial page older than this goes out, so little is lost at power-off

const char* FlashLog::stateName() const {
  switch (st_) {
    case OFF: return "off";
    case UNFORMATTED: return "unformatted";
    case READY: return "ready";
    case FULL: return "full";
    case ERASING: return "erasing";
    default: return "error";
  }
}

void FlashLog::startCur() {
  memset(cur_, 0xFF, sizeof cur_);
  cur_[0] = kLogPageMark;
  fill_ = 1;
}

void FlashLog::attach(IFlash* flash, uint32_t base, uint32_t bytes) {
  f_ = flash;
  base_ = base;
  pages_ = bytes / kLogPage;
  next_ = 1;
  haveOut_ = false;
  dropped_ = 0;
  startCur();
  st_ = OFF;
  if (!f_ || f_->jedecId() == 0 || f_->capacity() < base + bytes || base % kBlockBytes || bytes % kBlockBytes || pages_ < 4) return;
  uint8_t hdr[16];
  if (!f_->read(base_, hdr, sizeof hdr)) { st_ = ERROR; return; }
  const uint32_t size = hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24;
  if (memcmp(hdr, kMagic, 4) != 0 || hdr[4] != 1 || size != bytes) { st_ = UNFORMATTED; return; }
  uint32_t lo = 1, hi = pages_;                      // first page that starts with 0xFF (pages are written in order)
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    uint8_t b;
    if (!f_->read(base_ + mid * kLogPage, &b, 1)) { st_ = ERROR; return; }
    if (b != 0xFF) lo = mid + 1; else hi = mid;
  }
  next_ = lo;
  st_ = next_ >= pages_ ? FULL : READY;
}

bool FlashLog::commit() {
  if (haveOut_) return false;
  if (next_ >= pages_) { st_ = FULL; return false; }
  memcpy(out_, cur_, sizeof out_);
  outPage_ = next_++;
  haveOut_ = true;
  startCur();
  return true;
}

bool FlashLog::record(const uint8_t* rec, size_t n) {
  if (st_ != READY || n == 0 || n > kLogPage - 1) return false;
  if (fill_ + n > kLogPage && !commit()) { dropped_++; return false; }
  if (fill_ == 1) curSinceMs_ = nowMs_;
  memcpy(cur_ + fill_, rec, n);
  fill_ += (uint32_t)n;
  return true;
}

void FlashLog::flush() {
  if (st_ == READY && fill_ > 1) commit();
}

bool FlashLog::startErase() {
  if (st_ == OFF || st_ == ERASING) return false;
  haveOut_ = false;
  startCur();
  st_ = ERASING;
  eraseStep_ = 0;
  return true;
}

void FlashLog::service(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (st_ == ERASING) {
    if (f_->busy()) return;
    const uint32_t blocks = pages_ * kLogPage / kBlockBytes;
    if (eraseStep_ < blocks) {
      if (f_->startErase(base_ + eraseStep_ * kBlockBytes, kBlockBytes)) eraseStep_++;
      return;
    }
    uint8_t hdr[kLogPage];
    memset(hdr, 0xFF, sizeof hdr);
    memcpy(hdr, kMagic, 4);
    hdr[4] = 1; hdr[5] = 0;
    hdr[6] = kLogPage & 0xFF; hdr[7] = kLogPage >> 8;
    const uint32_t bytes = pages_ * kLogPage;
    for (int i = 0; i < 4; i++) hdr[8 + i] = (uint8_t)(bytes >> (8 * i));
    if (!f_->program(base_, hdr, sizeof hdr)) { st_ = ERROR; return; }
    next_ = 1;
    st_ = READY;
    return;
  }
  if (st_ != READY && st_ != FULL) return;
  if (haveOut_ && !f_->busy()) {
    if (!f_->program(base_ + outPage_ * kLogPage, out_, sizeof out_)) { st_ = ERROR; return; }
    haveOut_ = false;
    if (outPage_ + 1 >= pages_) st_ = FULL;
  }
  if (st_ == READY && fill_ > 1 && !haveOut_ && nowMs - curSinceMs_ > kFlushAgeMs) commit();
}

bool FlashLog::read(uint32_t off, void* dst, uint32_t n) {
  if (!f_ || st_ == OFF || st_ == ERASING || st_ == UNFORMATTED) return false;
  if (off > usedBytes() || n > usedBytes() - off) return false;
  return f_->read(base_ + kLogPage + off, dst, n);
}

}  // namespace storage
