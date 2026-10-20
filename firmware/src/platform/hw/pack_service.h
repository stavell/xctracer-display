#pragma once
// The map pack on the device: the QSPI flash, its header, the block reader for the terrain code and the USB serial
// loader (host/load_pack.py). One static object per image, used from the loop task only.
//
//   static PackService pack;                       // a global
//   setup(): Serial.begin(); pack.begin();         // flash, header, speed probe
//            if (pack.reader()) kit.open(pack.reader());
//   loop():  pack.service(millis());               // serial -> loader, long erase / check steps, reboot after a load
//
// While the loader is active (after HELLO) the reader returns false for every read: the map is never drawn from
// a flash that is being rewritten. The loader ends with a reboot (EXIT, or 30 s of silence), so the terrain code
// always starts from a flash that is either a complete valid pack or "no pack".
#include <stddef.h>
#include <stdint.h>
#include "platform/hw/qspi_flash.h"
#include "platform/storage/flash_log.h"
#include "platform/storage/flash_reader.h"
#include "platform/storage/flash_view.h"
#include "platform/storage/log_shell.h"
#include "platform/storage/pack_header.h"
#include "platform/storage/pack_loader.h"

class PackService {
 public:
  void setTimeSink(storage::ITimeSink* t) { shell_.setTimeSink(t); }   // the TIME command of the USB shell
  void setAppSink(storage::IAppSink* a) { shell_.setAppSink(a); }      // the APP / APP? bench commands (box image)
  PackService() : packView_(flash_, storage::kLogBytes), reader_(packView_), loader_(packView_), shell_(log_) {}
  void begin();
  void service(uint32_t nowMs);

  // The pack as a block reader, or nullptr when the flash holds no valid pack (blank, damaged header, chip not answering).
  terrain::IBlockReader* reader() { return hasPack_ ? &reader_ : nullptr; }
  bool hasPack() const { return hasPack_; }
  uint32_t packBytes() const { return hasPack_ ? header_.packLen : 0; }
  bool loading() const { return loader_.active(); }
  QspiFlash& flash() { return flash_; }
  // The flight log in the last 256 kB of the chip (the pack gets the rest). Records go in through the sink; the USB serial
  // commands LOGINFO / LOGREAD / LOGERASE (host/logdl.py) are handled in service().
  storage::IRecordSink& logSink() { return log_; }
  const storage::FlashLog& flightLog() const { return log_; }
  void formatLogStatus(char* dst, size_t cap) const;       // "lg state=ready used=... cap=... dropped=..."

  void setTerrainOpen(bool open) { terrainOpen_ = open; }   // the image tells whether the terrain code accepted the pack

  // One line for the serial status ("pack hdr=ok len=... crc=... jedec=... seq=... rnd=..."), no newline.
  void formatStatus(char* dst, size_t cap) const;

  // Diagnostics build: read the whole pack once and check its CRC, timed. Blocks for about a second.
  struct Bench { uint32_t readKBps = 0, crcMs = 0; bool crcOk = false; };
  Bench benchWholePack();

 private:
  void pumpSerial(uint32_t nowMs);
  void checkStoredCrc();
  void probeSpeed();

  struct SerialOut : storage::ITextOut {
    void put(const char* line) override;
  };
  QspiFlash flash_;
  storage::ReservedTailFlash packView_;             // the flash as the map pack sees it: without the log region
  storage::FlashBlockReader reader_;
  storage::PackLoader loader_;
  storage::CrcScan scan_;
  storage::FlashHeader header_;
  storage::FlashLog log_;
  storage::LogShell shell_;
  SerialOut out_;
  bool logErasing_ = false;
  storage::HeaderStatus hdrStatus_ = storage::HDR_BAD;
  bool flashOk_ = false, hasPack_ = false, terrainOpen_ = false;
  enum Crc : uint8_t { CRC_PENDING, CRC_RUNNING, CRC_OK, CRC_BAD } crc_ = CRC_PENDING;
  uint32_t seqKBps_ = 0, rndUs_ = 0, crcMsTotal_ = 0, crcT0_ = 0;
};
