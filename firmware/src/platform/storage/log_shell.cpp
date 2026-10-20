#include "platform/storage/log_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/terrain/crc32.h"

namespace storage {

void LogShell::feed(uint8_t b, ITextOut& out) {
  if (b == '\n' || b == '\r') {
    if (n_ && !overflow_) { line_[n_] = 0; run(out); }
    n_ = 0;
    overflow_ = false;
    return;
  }
  if (n_ + 1 < sizeof line_) line_[n_++] = (char)b; else overflow_ = true;
}

void LogShell::run(ITextOut& out) {
  if (strcmp(line_, "TIME?") == 0) {
    const int m = time_ ? time_->localMinutes() : -1;
    if (m < 0) out.put("tm now unknown");
    else { snprintf(buf_, sizeof buf_, "tm now %02d:%02d", m / 60, m % 60); out.put(buf_); }
    return;
  }
  if (strncmp(line_, "TIME ", 5) == 0) {
    char* end;
    const long long epoch = strtoll(line_ + 5, &end, 10);
    const long tz = strtol(end, nullptr, 10);
    if (!time_ || tz < -24 * 60 || tz > 24 * 60 || !time_->setTime(epoch, (int)tz)) { out.put("tm err refused"); return; }
    snprintf(buf_, sizeof buf_, "tm ok %lu tz=%ld", (unsigned long)epoch, tz);   // newlib-nano has no %lld
    out.put(buf_);
    return;
  }
  if (strcmp(line_, "APP?") == 0) {
    snprintf(buf_, sizeof buf_, "app now %s", apps_ ? apps_->activeApp() : "none");
    out.put(buf_);
    return;
  }
  if (strcmp(line_, "REBOOT") == 0) {
    if (!apps_) { out.put("rb err unsupported"); return; }
    out.put("rb ok");
    if (!apps_->reboot()) out.put("rb err unsupported");
    return;
  }
  if (strncmp(line_, "APP ", 4) == 0) {
    const char* name = line_ + 4;
    while (*name == ' ') name++;
    if (!apps_ || !*name || !apps_->openApp(name)) { snprintf(buf_, sizeof buf_, "app err unknown %.20s", name); out.put(buf_); return; }
    snprintf(buf_, sizeof buf_, "app ok %.20s", name);
    out.put(buf_);
    return;
  }
  if (strncmp(line_, "LOG", 3) != 0) return;
  if (strcmp(line_, "LOGINFO") == 0) {
    snprintf(buf_, sizeof buf_, "lg info state=%s used=%lu cap=%lu dropped=%lu", log_.stateName(), (unsigned long)log_.usedBytes(),
             (unsigned long)log_.capacityBytes(), (unsigned long)log_.dropped());
    out.put(buf_);
  } else if (strcmp(line_, "LOGERASE") == 0) {
    out.put(log_.startErase() ? "lg ok erase started" : "lg err erase refused");
  } else if (strncmp(line_, "LOGREAD ", 8) == 0) {
    char* end;
    const unsigned long off = strtoul(line_ + 8, &end, 10);
    const unsigned long len = strtoul(end, nullptr, 10);
    uint8_t chunk[128];
    if (len == 0 || len > kMaxRead || off > log_.usedBytes() || len > log_.usedBytes() - off) {
      snprintf(buf_, sizeof buf_, "lg err read range off=%lu len=%lu used=%lu", off, len, (unsigned long)log_.usedBytes());
      out.put(buf_);
      return;
    }
    uint32_t crc = 0;
    for (unsigned long done = 0; done < len;) {
      const uint32_t n = len - done < sizeof chunk ? (uint32_t)(len - done) : (uint32_t)sizeof chunk;
      if (!log_.read(off + done, chunk, n)) { out.put("lg err read failed"); return; }
      crc = terrain::crc32(crc, chunk, n);
      int k = snprintf(buf_, sizeof buf_, "lg d %lu ", off + done);
      static const char* hex = "0123456789abcdef";
      for (uint32_t i = 0; i < n; i++) { buf_[k++] = hex[chunk[i] >> 4]; buf_[k++] = hex[chunk[i] & 15]; }
      buf_[k] = 0;
      out.put(buf_);
      done += n;
    }
    snprintf(buf_, sizeof buf_, "lg ok read %lu %lu crc=%lu", off, len, (unsigned long)crc);
    out.put(buf_);
  } else {
    out.put("lg err unknown command");
  }
}

}  // namespace storage
