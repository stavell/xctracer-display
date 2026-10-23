#include "platform/hw/pack_service.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

using namespace storage;

void PackService::begin() {
  flashOk_ = flash_.begin(1);
  hdrStatus_ = flashOk_ ? readHeader(packView_, &header_) : HDR_BAD;
  hasPack_ = hdrStatus_ == HDR_OK && packHeaderConsistent(packView_, header_.packLen);
  if (flashOk_ && flash_.capacity() >= 2 * kLogBytes) log_.attach(&flash_, flash_.capacity() - kLogBytes);
  if (hasPack_) reader_.attach(header_.packLen);
  probeSpeed();
}

// Sequential 512-byte reads (64 kB) and 64 reads of 400 bytes at scattered places, which is what the terrain code does
// (one coded tile per read). The numbers go into the "pack" status line.
void PackService::probeSpeed() {
  if (!flashOk_) return;
  static uint8_t buf[512];
  const uint32_t span = hasPack_ && header_.packLen < 65536 ? header_.packLen : 65536;
  uint32_t t0 = micros();
  uint32_t n = 0;
  for (uint32_t off = 0; off + sizeof buf <= span; off += sizeof buf, n += sizeof buf)
    if (!packView_.read(kPackBase + off, buf, sizeof buf)) break;
  uint32_t us = micros() - t0;
  seqKBps_ = us ? (uint32_t)((uint64_t)n * 1000000ull / us / 1024) : 0;
  uint32_t x = 12345, rn = 0;
  t0 = micros();
  for (int i = 0; i < 64; i++) {
    x = x * 1664525u + 1013904223u;
    uint32_t off = (x >> 8) % (span > 512 ? span - 512 : 1);
    if (packView_.read(kPackBase + off, buf, 400)) rn++;
  }
  us = micros() - t0;
  rndUs_ = rn ? us / rn : 0;
}

PackService::Bench PackService::benchWholePack() {
  Bench b;
  if (!hasPack_) return b;
  static uint8_t buf[1024];
  uint32_t t0 = micros();
  for (uint32_t off = 0; off < header_.packLen; off += sizeof buf) {
    uint32_t n = header_.packLen - off < sizeof buf ? header_.packLen - off : (uint32_t)sizeof buf;
    if (!packView_.read(kPackBase + off, buf, n)) return b;
  }
  uint32_t us = micros() - t0;
  b.readKBps = us ? (uint32_t)((uint64_t)header_.packLen * 1000000ull / us / 1024) : 0;
  t0 = millis();
  CrcScan s;
  s.begin(&packView_, kPackBase, header_.packLen);
  while (!s.step(65536)) {}
  b.crcMs = millis() - t0;
  b.crcOk = !s.failed() && s.crc() == header_.packCrc;
  return b;
}

// While a terminal is attached the stored pack is checked against the CRC of its header, 8 kB per loop pass so the
// display never stalls (the whole check is about 0.5 s of work). A flight with no terminal never does it.
void PackService::checkStoredCrc() {
  if (!hasPack_ || loader_.active() || crc_ == CRC_OK || crc_ == CRC_BAD || !Serial) return;
  if (crc_ == CRC_PENDING) {
    if (millis() < 3000) return;
    scan_.begin(&packView_, kPackBase, header_.packLen);
    crc_ = CRC_RUNNING;
    crcT0_ = millis();
  }
  if (scan_.step(8192)) {
    crc_ = !scan_.failed() && scan_.crc() == header_.packCrc ? CRC_OK : CRC_BAD;
    crcMsTotal_ = millis() - crcT0_;
  }
}

void PackService::pumpSerial(uint32_t nowMs) {
  for (int guard = 0; guard < 4096; guard++) {
    if (const char* r = loader_.reply()) {
      Serial.print(r);
      Serial.print('\n');
      loader_.replyDone();
    }
    if (!Serial.available()) break;
    const uint8_t b = (uint8_t)Serial.read();
    loader_.feedByte(b, nowMs);
    shell_.feed(b, out_);                                  // text lines "LOG..." (the loader ignores them, this ignores its frames)
  }
}

void PackService::SerialOut::put(const char* line) {
  Serial.print(line);
  Serial.print('\n');
}

void PackService::service(uint32_t nowMs) {
  pumpSerial(nowMs);
  loader_.service(nowMs);
  if (loader_.reply()) pumpSerial(nowMs);
  if (!loader_.active()) log_.service(nowMs);              // the chip is the loader's while it loads
  const bool eraseNow = log_.erasing();
  if (logErasing_ && !eraseNow) out_.put(log_.state() == storage::FlashLog::READY ? "lg ok erased" : "lg err erase failed");
  logErasing_ = eraseNow;
  reader_.setLocked(loader_.active() || eraseNow);         // a chip that is erasing cannot be read: no map until the format is over
  if (loader_.active()) { crc_ = CRC_PENDING; return; }
  if (loader_.rebootRequested()) {                         // a load is over (or the host vanished): start from the flash as it is now
    Serial.flush();
    delay(150);
    NVIC_SystemReset();
  }
  checkStoredCrc();
  flash_.idle(nowMs);
}

void PackService::formatStatus(char* dst, size_t cap) const {
  const char* crc = crc_ == CRC_OK ? "ok" : crc_ == CRC_BAD ? "BAD" : crc_ == CRC_RUNNING ? "running" : "pending";
  snprintf(dst, cap, "pack hdr=%s len=%lu crc=%s(%lums) jedec=%06lx quad=%d mhz=%lu seq=%luKB/s rnd400=%luus terrain=%s loader=%s rd=%lu fail=%lu",
           flashOk_ ? headerStatusName(hdrStatus_) : "noflash", (unsigned long)(hasPack_ ? header_.packLen : 0), crc,
           (unsigned long)crcMsTotal_, (unsigned long)flash_.jedec(), flash_.quadRead() ? 1 : 0, (unsigned long)flash_.sckMHz(),
           (unsigned long)seqKBps_, (unsigned long)rndUs_, terrainOpen_ ? "open" : "none", loader_.active() ? "active" : "idle",
           (unsigned long)reader_.reads(), (unsigned long)reader_.failures());
}

void PackService::formatLogStatus(char* dst, size_t cap) const {
  snprintf(dst, cap, "lg state=%s used=%lu cap=%lu dropped=%lu", log_.stateName(), (unsigned long)log_.usedBytes(),
           (unsigned long)log_.capacityBytes(), (unsigned long)log_.dropped());
}
