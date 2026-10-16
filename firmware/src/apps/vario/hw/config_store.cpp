#include "apps/vario/hw/config_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "platform/sensors/mount.h"

using namespace Adafruit_LittleFS_Namespace;

static const uint32_t MAGIC = 0x56415233;
static const char* PATH = "/vario.cfg";

// Write to path.tmp, check the byte count, then rename over the real file. littlefs rename replaces an
// existing target atomically, so a power loss leaves either the old or the new file, never neither.
// A leftover .tmp from an interrupted save is ignored by loading and overwritten by the next save.
static bool writeAtomic(const char* path, const void* data, size_t len) {
  char tmp[24];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  InternalFS.remove(tmp);
  File f(InternalFS);
  if (!f.open(tmp, FILE_O_WRITE)) return false;
  size_t n = f.write((const uint8_t*)data, len);
  f.close();
  if (n != len) { InternalFS.remove(tmp); return false; }
  if (!InternalFS.rename(tmp, path)) { InternalFS.remove(tmp); return false; }
  return true;
}

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
  writeAtomic(PATH, &sv_, sizeof sv_);
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

// The old per-vario files. Read once by migrateTo().
struct MountFile { uint32_t magic; int32_t index; };
static const uint32_t MOUNT_MAGIC = 0x4d4e5431;
struct MagFile { uint32_t magic; float off[3]; float radius; };
static const uint32_t MAG_MAGIC = 0x4d414733;

void ConfigStore::migrateTo(BoxSettings& dst) {
  if (dst.migrated()) return;
  BoxSettings::Legacy l;
  File f(InternalFS);
  if (f.open("/magcal3.bin", FILE_O_READ)) {
    MagFile m;
    if (f.read((uint8_t*)&m, sizeof m) == sizeof m && m.magic == MAG_MAGIC) {
      l.haveCal = true;
      l.cal.valid = true;
      for (int i = 0; i < 3; i++) l.cal.off[i] = m.off[i];
      l.cal.radius = m.radius;
    }
    f.close();
  }
  File mf(InternalFS);
  if (mf.open("/mount.bin", FILE_O_READ)) {
    MountFile x;
    if (mf.read((uint8_t*)&x, sizeof x) == sizeof x && x.magic == MOUNT_MAGIC && mountValid(x.index)) {
      l.haveMount = true;
      l.mount = x.index;
    }
    mf.close();
  }
  l.haveTz = true;                                 // sv_.tz is 0 when none was ever set: UTC, the default anyway
  l.tz = sv_.tz;
  dst.importLegacy(l);
}
