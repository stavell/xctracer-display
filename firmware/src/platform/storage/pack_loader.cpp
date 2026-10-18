#include "platform/storage/pack_loader.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace storage {

static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void PackLoader::say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(reply_, sizeof reply_, fmt, ap);
  va_end(ap);
  if (n < 0) n = 0;
  if (n >= (int)sizeof reply_) n = sizeof reply_ - 1;
  replyLen_ = (uint8_t)(n ? n : 1);
}

void PackLoader::feedByte(uint8_t b, uint32_t now) {
  if (replyLen_) return;                                  // the caller must send the pending line first
  FrameParser::Result r = parser_.feed(b, now);
  if (r == FrameParser::FRAME) handle(parser_.cmd(), parser_.payload(), parser_.length(), now);
  else if (r == FrameParser::BAD && st_ != IDLE) say("pk err frame crc-or-length");
}

void PackLoader::handle(uint8_t cmd, const uint8_t* p, uint32_t n, uint32_t now) {
  if (cmd == CMD_HELLO) { if (n == 8 && memcmp(p, "LOADPACK", 8) == 0) hello(now); return; }   // anything else: not for us
  if (st_ == IDLE) { say("pk err idle send HELLO first"); return; }
  lastAct_ = now;
  if (busy()) { say("pk err busy"); return; }
  switch (cmd) {
    case CMD_ERASE: erase(p, n, now); break;
    case CMD_WRITE: write(p, n); break;
    case CMD_FINISH: finish(p, n, now); break;
    case CMD_STATUS: status(now); break;
    case CMD_EXIT: exitMode(p, n); break;
    default: say("pk err cmd %u", (unsigned)cmd);
  }
}

void PackLoader::hello(uint32_t now) {
  st_ = READY;
  lastAct_ = now;
  erased_ = false;
  say("pk ok hello jedec=%06lx cap=%lu base=%lu chunk=%lu ver=1", (unsigned long)flash_.jedecId(),
      (unsigned long)flash_.capacity(), (unsigned long)kPackBase, (unsigned long)kMaxChunk);
}

void PackLoader::erase(const uint8_t* p, uint32_t n, uint32_t now) {
  if (n != 4) { say("pk err erase args"); return; }
  uint32_t len = get32(p);
  if (len < kMinPackBytes || flash_.capacity() < kPackBase || len > flash_.capacity() - kPackBase) {
    say("pk err erase length %lu (max %lu)", (unsigned long)len, (unsigned long)(flash_.capacity() - kPackBase));
    return;
  }
  packLen_ = len;
  erased_ = false;
  erasePos_ = 0;                                          // the header sector first: from now on there is no pack
  eraseEnd_ = kPackBase + ((len + kSectorBytes - 1) / kSectorBytes) * kSectorBytes;
  opStart_ = now;
  st_ = ERASING;
}

void PackLoader::serviceErase(uint32_t now) {
  if (flash_.busy()) {
    if (now - opStart_ > 15000) { st_ = READY; say("pk err erase timeout at %lu", (unsigned long)erasePos_); }
    return;
  }
  if (erasePos_ >= eraseEnd_) {
    erased_ = true;
    st_ = READY;
    lastAct_ = now;
    say("pk ok erase end=%lu", (unsigned long)eraseEnd_);
    return;
  }
  const uint32_t size = (erasePos_ % kBlockBytes == 0 && eraseEnd_ - erasePos_ >= kBlockBytes) ? kBlockBytes : kSectorBytes;
  if (!flash_.startErase(erasePos_, size)) { st_ = READY; say("pk err erase start at %lu", (unsigned long)erasePos_); return; }
  erasePos_ += size;
  opStart_ = now;
}

void PackLoader::write(const uint8_t* p, uint32_t n) {
  if (!erased_) { say("pk err write not erased"); return; }
  if (n < 5 || n - 4 > kMaxChunk) { say("pk err write size %lu", (unsigned long)n); return; }
  const uint32_t off = get32(p), len = n - 4;
  if (off > packLen_ || len > packLen_ - off) { say("pk err write range off=%lu n=%lu", (unsigned long)off, (unsigned long)len); return; }
  if (!flash_.program(kPackBase + off, p + 4, len) || !flash_.read(kPackBase + off, verify_, len) || memcmp(verify_, p + 4, len) != 0) {
    say("pk err write verify off=%lu", (unsigned long)off);
    return;
  }
  chunks_++;
  say("pk ok w off=%lu n=%lu", (unsigned long)off, (unsigned long)len);
}

void PackLoader::finish(const uint8_t* p, uint32_t n, uint32_t) {
  if (!erased_) { say("pk err finish not erased"); return; }
  if (n != 8 || get32(p) != packLen_) { say("pk err finish length"); return; }
  wantCrc_ = get32(p + 4);
  scanKind_ = SCAN_FINISH;
  scan_.begin(&flash_, kPackBase, packLen_);
  st_ = SCANNING;
}

void PackLoader::status(uint32_t) {
  HeaderStatus h = readHeader(flash_, &stored_);
  if (h != HDR_OK) { say("pk ok status hdr=%s", headerStatusName(h)); return; }
  scanKind_ = SCAN_STATUS;
  scan_.begin(&flash_, kPackBase, stored_.packLen);
  st_ = SCANNING;
}

void PackLoader::exitMode(const uint8_t* p, uint32_t n) {
  reboot_ = n < 1 || p[0] != 0;
  st_ = IDLE;
  say("pk ok exit reboot=%d", reboot_ ? 1 : 0);
}

void PackLoader::scanDone(uint32_t now) {
  st_ = READY;
  lastAct_ = now;
  if (scan_.failed()) { say("pk err %s read", scanKind_ == SCAN_FINISH ? "finish" : "status"); return; }
  if (scanKind_ == SCAN_STATUS) {
    say("pk ok status hdr=ok len=%lu crc=%08lx scan=%08lx match=%d", (unsigned long)stored_.packLen, (unsigned long)stored_.packCrc,
        (unsigned long)scan_.crc(), scan_.crc() == stored_.packCrc ? 1 : 0);
    return;
  }
  if (scan_.crc() != wantCrc_) {
    say("pk err finish crc=%08lx want=%08lx", (unsigned long)scan_.crc(), (unsigned long)wantCrc_);
    return;
  }
  if (!packHeaderConsistent(flash_, packLen_)) { say("pk err finish pack header (not a BGMP pack of %lu bytes)", (unsigned long)packLen_); return; }
  uint8_t hdr[kFlashHeaderBytes], back[kFlashHeaderBytes];
  FlashHeader h;
  h.packLen = packLen_;
  h.packCrc = wantCrc_;
  encodeHeader(hdr, h);
  if (!flash_.program(0, hdr, sizeof hdr) || !flash_.read(0, back, sizeof back) || memcmp(hdr, back, sizeof hdr) != 0) {
    say("pk err finish header write");
    return;
  }
  say("pk ok finish len=%lu crc=%08lx", (unsigned long)packLen_, (unsigned long)wantCrc_);
}

void PackLoader::service(uint32_t now) {
  if (st_ == IDLE || replyLen_) return;
  if (st_ == ERASING) { serviceErase(now); return; }
  if (st_ == SCANNING) {
    if (scan_.step(4096)) scanDone(now);
    return;
  }
  if (now - lastAct_ > kLoaderIdleMs) {                   // the host went away: leave load mode the safe way
    st_ = IDLE;
    reboot_ = true;
  }
}

}  // namespace storage
