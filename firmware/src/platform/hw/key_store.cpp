#include "platform/hw/key_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {
const uint32_t MAGIC = 0x424f5831;                  // "BOX1"
const char* PATH = "/box.cfg";
struct File1 { uint32_t magic; uint32_t v[8]; };
}  // namespace

void FlashKeyStore::begin() {
  File f(InternalFS);
  File1 d;
  if (f.open(PATH, FILE_O_READ)) {
    if (f.read((uint8_t*)&d, sizeof d) == sizeof d && d.magic == MAGIC) memcpy(v_, d.v, sizeof v_);
    f.close();
  }
}

void FlashKeyStore::set(StoreSlot slot, uint32_t value) {
  if (slot >= SLOTS || v_[slot] == value) return;
  v_[slot] = value;
  File1 d;
  d.magic = MAGIC;
  memcpy(d.v, v_, sizeof d.v);
  InternalFS.remove("/box.cfg.tmp");
  File f(InternalFS);
  if (!f.open("/box.cfg.tmp", FILE_O_WRITE)) return;
  size_t n = f.write((const uint8_t*)&d, sizeof d);
  f.close();
  if (n != sizeof d || !InternalFS.rename("/box.cfg.tmp", PATH)) InternalFS.remove("/box.cfg.tmp");
}
