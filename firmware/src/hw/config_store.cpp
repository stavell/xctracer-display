#include "hw/config_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

static const uint32_t MAGIC = 0x56415233;
static const char* PATH = "/vario.cfg";

void ConfigStore::begin() {
  InternalFS.begin();
  memset(&sv_, 0, sizeof sv_);
  File f(InternalFS);
  bool ok = false;
  if (f.open(PATH, FILE_O_READ)) {
    ok = f.read((uint8_t*)&sv_, sizeof sv_) == sizeof sv_ && sv_.magic == MAGIC;
    f.close();
  }
  if (!ok) {
    memset(&sv_, 0, sizeof sv_);
    sv_.magic = MAGIC;
    strcpy(sv_.filter, "*");
  }
}

void ConfigStore::save() {
  InternalFS.remove(PATH);
  File f(InternalFS);
  if (f.open(PATH, FILE_O_WRITE)) { f.write((uint8_t*)&sv_, sizeof sv_); f.close(); }
}

void ConfigStore::setFilter(const char* glob) {
  strncpy(sv_.filter, glob[0] ? glob : "*", sizeof sv_.filter - 1);
  sv_.filter[sizeof sv_.filter - 1] = 0;
  save();
}

void ConfigStore::saveDevice(const char* name, const ble_gap_addr_t& addr) {
  sv_.has = true;
  strncpy(sv_.name, name, sizeof sv_.name - 1);
  sv_.name[sizeof sv_.name - 1] = 0;
  sv_.addr = addr;
  save();
}

void ConfigStore::forget() {
  sv_.has = false;
  sv_.name[0] = 0;
  save();
}
