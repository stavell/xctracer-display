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

// Magnetometer calibration lives in its own file so that changing it never resets the saved tracer.
struct MagFile { uint32_t magic; float off[3]; float radius; };
static const uint32_t MAG_MAGIC = 0x4d414733;

bool ConfigStore::loadMagCal(MagCal& out) {
  File f(InternalFS);
  bool ok = false;
  if (f.open("/magcal3.bin", FILE_O_READ)) {
    MagFile m;
    if (f.read((uint8_t*)&m, sizeof m) == sizeof m && m.magic == MAG_MAGIC) {
      out.valid = true;
      for (int i = 0; i < 3; i++) out.off[i] = m.off[i];
      out.radius = m.radius;
      ok = true;
    }
    f.close();
  }
  return ok;
}

void ConfigStore::saveMagCal(const MagCal& cal) {
  MagFile m;
  m.magic = MAG_MAGIC;
  for (int i = 0; i < 3; i++) m.off[i] = cal.off[i];
  m.radius = cal.radius;
  InternalFS.remove("/magcal3.bin");
  File f(InternalFS);
  if (f.open("/magcal3.bin", FILE_O_WRITE)) { f.write((uint8_t*)&m, sizeof m); f.close(); }
}
