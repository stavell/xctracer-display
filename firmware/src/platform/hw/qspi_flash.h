#pragma once
// The Feather nRF52840 Sense's on-board 2 MB QSPI flash (GD25Q16C) through the nrfx QSPI driver that ships with the
// Adafruit core (blocking mode, no interrupt, no extra library). Hardware only; the rest of the map storage is pure
// code in platform/storage/ that talks to this class through IFlash.
//
//   - reads and programs take any address / length / buffer alignment (the peripheral wants 4-byte multiples and
//     word-aligned RAM buffers; a small staging buffer fixes that up), programs are split at 256-byte pages
//   - quad I/O read (0xEB) when the chip's QE bit is set (it is set here if it was not), else plain fast read
//   - erase is started and polled (4 kB sector, 64 kB block, whole chip), so the loop is never blocked for 100 ms+
//   - deep power-down behind powerDown()/wake(); idle(now) does it after setIdleSleepMs() of silence (default: never)
#include <stdint.h>
#include "platform/storage/flash_port.h"

class QspiFlash : public storage::IFlash {
 public:
  // Initialise the peripheral. sckDiv: SCK = 32 MHz / sckDiv (1..16), default 16 MHz. True if the chip answered.
  bool begin(uint8_t sckDiv = 2);
  bool ok() const { return ok_; }

  uint32_t jedecId() override { return jedec_; }
  uint32_t jedec() const { return jedec_; }
  uint32_t capacity() const override { return capacity_; }
  bool read(uint32_t addr, void* dst, uint32_t len) override;
  bool program(uint32_t addr, const void* src, uint32_t len) override;
  bool startErase(uint32_t addr, uint32_t size) override;
  bool busy() override;
  bool startChipErase();                           // whole chip (about 10 to 40 s); poll busy()

  bool quadRead() const { return quad_; }              // reads use the 4-wire mode
  uint32_t sckMHz() const { return 32 / sckDiv_; }

  // Deep power-down (about 1 uA instead of ~15 uA) and back. Every operation wakes the chip by itself.
  void powerDown();
  void wake();
  bool asleep() const { return asleep_; }
  void setIdleSleepMs(uint32_t ms) { idleMs_ = ms; }   // 0 = never (default)
  void idle(uint32_t nowMs);                       // call from the loop

 private:
  bool startPeripheral(bool quadRead);
  bool cinstr(uint8_t opcode, int len, const void* tx, void* rx, bool wren = false, bool wipwait = false);
  bool readStatus(uint8_t* sr1, uint8_t* sr2);
  bool ensureQuadEnable();
  bool rawRead(void* dst, uint32_t len, uint32_t addr);
  bool waitReady(uint32_t timeoutMs);
  void touch();

  bool ok_ = false, quad_ = false, asleep_ = false, started_ = false;
  uint8_t sckDiv_ = 2;
  uint32_t jedec_ = 0, capacity_ = 0, idleMs_ = 0, lastUse_ = 0;
  alignas(4) uint8_t stage_[260];                  // read and program staging (a page plus alignment padding)
};
