#pragma once
// What the box reports to the laptop on the notify characteristic D15A0003 (docs/claude_pet.md, "Status"). Pure.
#include <stdint.h>
#include <string.h>

namespace pet {

const int STATUS_BYTES = 8;
const uint8_t STATUS_VERSION = 1;
enum StatusFlags : uint8_t { SF_SHOWN_VALID = 1, SF_PET_ACTIVE = 2 };

struct Status {
  bool shownValid = false;       // the pet app has drawn a snapshot since the box started
  uint8_t shownSeq = 0;          // its seq byte
  bool petActive = false;        // the Agents app is the active app
  int battPct = -1;              // < 0 unknown
  int battMv = 0;                // 0 unknown
  uint8_t appId = 255;           // index of the active app in the box's host (main_box.cpp APP_*), 255 none
};

// [0] version 1, [1] seq of the last shown snapshot, [2] flags (bit 0 shown valid, bit 1 pet app active), [3] battery % (255 unknown),
// [4..5] battery mV little-endian (0 unknown), [6] active app id (255 none), [7] 0.
inline void encode(const Status& s, uint8_t out[STATUS_BYTES]) {
  memset(out, 0, STATUS_BYTES);
  out[0] = STATUS_VERSION;
  out[1] = s.shownSeq;
  out[2] = (uint8_t)((s.shownValid ? SF_SHOWN_VALID : 0) | (s.petActive ? SF_PET_ACTIVE : 0));
  out[3] = s.battPct < 0 ? 255 : (uint8_t)(s.battPct > 100 ? 100 : s.battPct);
  const int mv = s.battMv < 0 ? 0 : s.battMv > 65535 ? 65535 : s.battMv;
  out[4] = (uint8_t)(mv & 0xff);
  out[5] = (uint8_t)(mv >> 8);
  out[6] = s.appId;
}

// Decides when to notify: when something but the millivolts changed (the divider jitters) and every 30 s while connected.
class Reporter {
 public:
  static const uint32_t kPeriodMs = 30000;
  // True when `s` should be sent now. A fresh connection sends at once.
  bool due(uint32_t nowMs, bool connected, const Status& s) {
    if (!connected) { sent_ = false; return false; }
    const bool changed = !sent_ || s.shownValid != last_.shownValid || s.shownSeq != last_.shownSeq || s.petActive != last_.petActive ||
                         s.appId != last_.appId || s.battPct != last_.battPct;
    if (!changed && nowMs - lastMs_ < kPeriodMs) return false;
    sent_ = true;
    last_ = s;
    lastMs_ = nowMs;
    return true;
  }

 private:
  bool sent_ = false;
  Status last_;
  uint32_t lastMs_ = 0;
};

}  // namespace pet
