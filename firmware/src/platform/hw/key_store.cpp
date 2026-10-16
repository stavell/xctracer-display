#include "platform/hw/key_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {
const uint32_t MAGIC1 = 0x424f5831;                 // "BOX1": the first file, 8 slots (boot app, Tetris best)
const uint32_t MAGIC2 = 0x424f5832;                 // "BOX2": 16 slots
const char* PATH = "/box.cfg";
struct File1 { uint32_t magic; uint32_t v[8]; };
struct File2 { uint32_t magic; uint32_t v[16]; };
}  // namespace

void FlashKeyStore::begin() {
  File f(InternalFS);
  if (!f.open(PATH, FILE_O_READ)) return;
  File2 d;
  memset(&d, 0, sizeof d);
  const int n = f.read((uint8_t*)&d, sizeof d);
  f.close();
  if (n == (int)sizeof(File2) && d.magic == MAGIC2) memcpy(v_, d.v, sizeof v_);
  else if (n == (int)sizeof(File1) && d.magic == MAGIC1) memcpy(v_, d.v, sizeof(File1) - sizeof(uint32_t));   // keeps the old values
}

void FlashKeyStore::set(StoreSlot slot, uint32_t value) {
  if (slot >= SLOTS || v_[slot] == value) return;
  v_[slot] = value;
  if (batch_ > 0) { dirty_ = true; return; }
  persist();
}

void FlashKeyStore::endBatch() {
  if (batch_ > 0) batch_--;
  if (batch_ == 0 && dirty_) { dirty_ = false; persist(); }
}

void FlashKeyStore::persist() {
  File2 d;
  d.magic = MAGIC2;
  memcpy(d.v, v_, sizeof d.v);
  InternalFS.remove("/box.cfg.tmp");
  File f(InternalFS);
  if (!f.open("/box.cfg.tmp", FILE_O_WRITE)) return;
  size_t n = f.write((const uint8_t*)&d, sizeof d);
  f.close();
  if (n != sizeof d || !InternalFS.rename("/box.cfg.tmp", PATH)) InternalFS.remove("/box.cfg.tmp");
}
