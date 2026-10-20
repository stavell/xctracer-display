#include "platform/pet/pet_snapshot.h"
#include <string.h>

namespace pet {

bool decode(const uint8_t* d, size_t len, Snapshot& out) {
  if (!d || len < (size_t)HEADER_BYTES) return false;
  if (d[0] != OP_SNAPSHOT || d[1] != VERSION) return false;
  const int n = d[7];
  if (n > MAX_SLOTS || len != (size_t)(HEADER_BYTES + SLOT_BYTES * n)) return false;
  if (d[4] > 100 || d[5] > 99 || d[6] > 99) return false;
  Snapshot s;
  s.seq = d[2]; s.flags = d[3]; s.bowl = d[4]; s.hidden = d[5]; s.hiddenWaiting = d[6]; s.n = (uint8_t)n;
  unsigned used = 0;
  for (int i = 0; i < n; i++) {
    const uint8_t* p = d + HEADER_BYTES + SLOT_BYTES * i;
    Slot& t = s.slot[i];
    t.index = p[0]; t.state = p[1]; t.tool = p[2]; t.subagents = p[3]; t.species = p[4]; t.variant = p[5]; t.context = p[6]; t.minutes = p[7];
    if (t.index >= MAX_SLOTS || (used & (1u << t.index))) return false;
    used |= 1u << t.index;
    if (t.state >= S_COUNT || t.tool >= T_COUNT || t.subagents > 15) return false;
    if (t.context > 100 && t.context != CONTEXT_UNKNOWN) return false;
    for (int k = 0; k < 4; k++) {
      if (p[8 + k] < 0x20 || p[8 + k] > 0x7e) return false;
      t.label[k] = (char)p[8 + k];
    }
    t.label[4] = 0;
  }
  out = s;
  return true;
}

void Mailbox::put(const uint8_t* d, uint16_t len, uint32_t nowMs) {
  const uint32_t v = ver_.load();
  ver_.store(v + 1);                                  // odd: writing
  std::atomic_thread_fence(std::memory_order_seq_cst);
  rawLen_ = len > CAP ? 0 : len;                      // 0 = too long to be a snapshot: the reader counts it as rejected
  if (rawLen_) memcpy(raw_, d, rawLen_);
  rawMs_ = nowMs;
  writes_++;
  std::atomic_thread_fence(std::memory_order_seq_cst);
  ver_.store(v + 2);
}

void Mailbox::refresh() {
  uint8_t buf[CAP];
  uint16_t len = 0;
  uint32_t ms = 0, w = 0;
  bool ok = false;
  for (int tries = 0; tries < 4; tries++) {           // a put() that lands mid-copy makes us copy again; give up after a few
    const uint32_t v1 = ver_.load();
    if (v1 & 1) continue;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    len = rawLen_; ms = rawMs_; w = writes_;
    if (len) memcpy(buf, raw_, len);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (ver_.load() == v1) { ok = true; break; }
  }
  if (!ok || w == seen_) return;                      // torn or odd all the time: try again at the next call
  seen_ = w;
  Snapshot s;
  if (len && decode(buf, len, s)) { good_ = s; seq_++; goodMs_ = ms; }
  else rejected_++;
}

bool Mailbox::latest(Snapshot& out, uint32_t& seq) {
  refresh();
  seq = seq_;
  if (!seq_) return false;
  out = good_;
  return true;
}

uint32_t Mailbox::lastWriteMs() { refresh(); return goodMs_; }

}  // namespace pet
