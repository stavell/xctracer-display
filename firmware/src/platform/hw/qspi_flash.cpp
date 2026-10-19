#include "platform/hw/qspi_flash.h"
#include <Arduino.h>
#include <string.h>
#include "nrfx_qspi.h"

// Opcodes of the GD25Q16C (and most 25-series chips).
static const uint8_t OP_RDSR1 = 0x05, OP_RDSR2 = 0x35, OP_WRSR = 0x01, OP_WRSR2 = 0x31, OP_JEDEC = 0x9F;
static const uint8_t OP_DPD = 0xB9, OP_RELEASE = 0xAB;
static const uint8_t QE_BIT = 0x02;                       // status register 2: quad enable (non-volatile on this chip)

static bool isQuadSafe(uint8_t sr2) { return (sr2 & QE_BIT) != 0; }

bool QspiFlash::startPeripheral(bool quadRead) {
  nrfx_qspi_config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.pins.sck_pin = g_ADigitalPinMap[PIN_QSPI_SCK];
  cfg.pins.csn_pin = g_ADigitalPinMap[PIN_QSPI_CS];
  cfg.pins.io0_pin = g_ADigitalPinMap[PIN_QSPI_IO0];
  cfg.pins.io1_pin = g_ADigitalPinMap[PIN_QSPI_IO1];
  cfg.pins.io2_pin = g_ADigitalPinMap[PIN_QSPI_IO2];
  cfg.pins.io3_pin = g_ADigitalPinMap[PIN_QSPI_IO3];
  cfg.prot_if.readoc = quadRead ? NRF_QSPI_READOC_READ4IO : NRF_QSPI_READOC_FASTREAD;
  cfg.prot_if.writeoc = NRF_QSPI_WRITEOC_PP;              // single-wire page program: the chip's own program time dominates anyway
  cfg.prot_if.addrmode = NRF_QSPI_ADDRMODE_24BIT;
  cfg.prot_if.dpmconfig = false;
  cfg.phy_if.sck_delay = 0x05;
  cfg.phy_if.dpmen = false;
  cfg.phy_if.spi_mode = NRF_QSPI_MODE_0;
  cfg.phy_if.sck_freq = (nrf_qspi_frequency_t)(sckDiv_ - 1);
  cfg.irq_priority = 7;                                   // unused in blocking mode; never 0..1 (SoftDevice)
  nrfx_err_t e = nrfx_qspi_init(&cfg, nullptr, nullptr);
  started_ = true;                                        // the driver is initialised even when the chip is slow to answer
  if (e == NRFX_SUCCESS) return true;
  if (!asleep_) { nrfx_qspi_uninit(); started_ = false; }
  return false;
}

bool QspiFlash::cinstr(uint8_t opcode, int len, const void* tx, void* rx, bool wren, bool wipwait) {
  nrf_qspi_cinstr_conf_t c;
  c.opcode = opcode;
  c.length = (nrf_qspi_cinstr_len_t)len;
  c.io2_level = true;                                     // WP# and HOLD# inactive while the chip is in single-wire mode
  c.io3_level = true;
  c.wipwait = wipwait;
  c.wren = wren;
  return nrfx_qspi_cinstr_xfer(&c, tx, rx) == NRFX_SUCCESS;
}

bool QspiFlash::waitReady(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (nrfx_qspi_mem_busy_check() != NRFX_SUCCESS) {
    if (millis() - t0 > timeoutMs) return false;
  }
  return true;
}

bool QspiFlash::readStatus(uint8_t* sr1, uint8_t* sr2) {
  return cinstr(OP_RDSR1, NRF_QSPI_CINSTR_LEN_2B, nullptr, sr1) && cinstr(OP_RDSR2, NRF_QSPI_CINSTR_LEN_2B, nullptr, sr2);
}

bool QspiFlash::ensureQuadEnable() {
  uint8_t s1 = 0, s2 = 0;
  if (!readStatus(&s1, &s2)) return false;
  if (isQuadSafe(s2)) return true;
  uint8_t both[2] = {s1, (uint8_t)(s2 | QE_BIT)};
  cinstr(OP_WRSR, NRF_QSPI_CINSTR_LEN_3B, both, nullptr, true, false);   // status registers 1 and 2 in one write
  waitReady(100);
  if (readStatus(&s1, &s2) && isQuadSafe(s2)) return true;
  uint8_t only2 = (uint8_t)(s2 | QE_BIT);                  // chips that take status register 2 on its own opcode
  cinstr(OP_WRSR2, NRF_QSPI_CINSTR_LEN_2B, &only2, nullptr, true, false);
  waitReady(100);
  return readStatus(&s1, &s2) && isQuadSafe(s2);
}

bool QspiFlash::rawRead(void* dst, uint32_t len, uint32_t addr) {
  return nrfx_qspi_read(dst, len, addr) == NRFX_SUCCESS;
}

bool QspiFlash::begin(uint8_t sckDiv) {
  if (started_) return ok_;
  sckDiv_ = sckDiv < 1 ? 1 : sckDiv > 16 ? 16 : sckDiv;
  ok_ = quad_ = asleep_ = false;
  asleep_ = true;                                          // startPeripheral keeps the driver up on a timeout then (the chip may be asleep)
  startPeripheral(false);
  asleep_ = false;
  cinstr(OP_RELEASE, NRF_QSPI_CINSTR_LEN_1B, nullptr, nullptr);   // a chip left in deep power-down by an earlier run answers nothing else; harmless on an awake one
  delayMicroseconds(60);
  if (!started_) return false;
  uint8_t id[4] = {0, 0, 0, 0};
  if (!cinstr(OP_JEDEC, NRF_QSPI_CINSTR_LEN_4B, nullptr, id)) { nrfx_qspi_uninit(); started_ = false; return false; }
  jedec_ = (uint32_t)id[0] << 16 | id[1] << 8 | id[2];
  if (jedec_ == 0 || jedec_ == 0xFFFFFF) { nrfx_qspi_uninit(); started_ = false; return false; }
  capacity_ = (id[2] >= 0x10 && id[2] <= 0x1C) ? (1u << id[2]) : 0;
  ok_ = capacity_ != 0;
  if (ok_ && ensureQuadEnable()) {
    // Switch the read opcode to the 4-wire one, and fall back to the plain fast read unless the same bytes come back
    // from two places (the first 64 bytes of the pack area and the last 64 bytes of the chip).
    alignas(4) uint8_t a[64], b[64], c[64], d[64];
    bool same = rawRead(a, 64, 4096) && rawRead(b, 64, capacity_ - 64);
    if (same) {
      nrfx_qspi_uninit();
      started_ = false;
      same = startPeripheral(true) && rawRead(c, 64, 4096) && rawRead(d, 64, capacity_ - 64) && memcmp(a, c, 64) == 0 && memcmp(b, d, 64) == 0;
      if (!same) {
        if (started_) { nrfx_qspi_uninit(); started_ = false; }
        ok_ = startPeripheral(false);
      }
      quad_ = same;
    }
  }
  touch();
  return ok_;
}

void QspiFlash::touch() { lastUse_ = millis(); }

void QspiFlash::powerDown() {
  if (!ok_ || asleep_ || !started_) return;
  if (nrfx_qspi_mem_busy_check() != NRFX_SUCCESS) return;     // never power down in the middle of an erase or program
  cinstr(OP_DPD, NRF_QSPI_CINSTR_LEN_1B, nullptr, nullptr);
  nrfx_qspi_uninit();
  started_ = false;
  asleep_ = true;
}

void QspiFlash::wake() {
  if (!asleep_) return;
  // The peripheral's start-up reads the status register, which a chip in deep power-down does not answer: the
  // driver reports a timeout but is initialised; the release command (0xAB) fixes the chip.
  startPeripheral(quad_);
  cinstr(OP_RELEASE, NRF_QSPI_CINSTR_LEN_1B, nullptr, nullptr);
  delayMicroseconds(60);                                      // tRES1: 20 us typical
  asleep_ = false;
  if (!waitReady(10)) ok_ = false;
  touch();
}

void QspiFlash::idle(uint32_t nowMs) {
  if (idleMs_ && ok_ && !asleep_ && nowMs - lastUse_ > idleMs_) powerDown();
}

bool QspiFlash::read(uint32_t addr, void* dst, uint32_t len) {
  if (!ok_) return false;
  if (asleep_) wake();
  if (addr > capacity_ || len > capacity_ - addr) return false;
  uint8_t* d = (uint8_t*)dst;
  while (len) {
    if ((((uintptr_t)d | addr) & 3) == 0 && len >= 4) {
      uint32_t n = len & ~3u;
      if (n > 4096) n = 4096;
      if (rawRead(d, n, addr)) { d += n; addr += n; len -= n; continue; }
    }
    const uint32_t skip = addr & 3;
    uint32_t n = len < 248 ? len : 248;
    const uint32_t total = (skip + n + 3) & ~3u;
    if (!rawRead(stage_, total, addr - skip)) return false;
    memcpy(d, stage_ + skip, n);
    d += n; addr += n; len -= n;
  }
  touch();
  return true;
}

bool QspiFlash::program(uint32_t addr, const void* src, uint32_t len) {
  if (!ok_) return false;
  if (asleep_) wake();
  if (addr > capacity_ || len > capacity_ - addr) return false;
  const uint8_t* s = (const uint8_t*)src;
  while (len) {
    uint32_t n = 256 - (addr & 255);
    if (n > len) n = len;
    const uint32_t skip = addr & 3, total = (skip + n + 3) & ~3u;
    memset(stage_, 0xFF, total);                              // programming 0xFF changes nothing: the padding is harmless
    memcpy(stage_ + skip, s, n);
    if (nrfx_qspi_write(stage_, total, addr - skip) != NRFX_SUCCESS) return false;
    if (!waitReady(50)) return false;                         // page program: 0.4 ms typical, 3 ms max
    s += n; addr += n; len -= n;
  }
  touch();
  return true;
}

bool QspiFlash::startErase(uint32_t addr, uint32_t size) {
  if (!ok_) return false;
  if (asleep_) wake();
  if ((size != storage::kSectorBytes && size != storage::kBlockBytes) || addr % size || addr >= capacity_ || size > capacity_ - addr) return false;
  if (nrfx_qspi_mem_busy_check() != NRFX_SUCCESS) return false;
  touch();
  return nrfx_qspi_erase(size == storage::kSectorBytes ? NRF_QSPI_ERASE_LEN_4KB : NRF_QSPI_ERASE_LEN_64KB, addr) == NRFX_SUCCESS;
}

bool QspiFlash::startChipErase() {
  if (!ok_) return false;
  if (asleep_) wake();
  if (nrfx_qspi_mem_busy_check() != NRFX_SUCCESS) return false;
  touch();
  return nrfx_qspi_chip_erase() == NRFX_SUCCESS;
}

bool QspiFlash::busy() {
  if (!ok_) return true;                                      // a chip that does not answer never finishes: callers time out
  if (asleep_) wake();
  touch();
  return nrfx_qspi_mem_busy_check() != NRFX_SUCCESS;
}
