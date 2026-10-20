#pragma once
// The pet snapshot: what the laptop daemon (host/claude_pet.py) writes with opcode 0x30 and the Agents app shows
// (docs/claude_pet.md section 7.3). Pure: the decoder is strict and the mailbox only copies bytes, so it can sit in a BLE callback.
#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace pet {

const uint8_t OP_SNAPSHOT = 0x30;
const uint8_t VERSION = 1;
const int MAX_SLOTS = 6;
const int HEADER_BYTES = 8, SLOT_BYTES = 12;
const int MAX_BYTES = HEADER_BYTES + SLOT_BYTES * MAX_SLOTS;   // 80

// Session states, numbered by urgency (higher = needs me more).
enum State : uint8_t { S_IDLE, S_WORKING, S_DONE, S_FAILED, S_QUESTION, S_PERMISSION, S_COUNT };
// Tool categories (spec section 6, in the order of its table).
enum Tool : uint8_t { T_THINK, T_READ, T_EDIT, T_SHELL, T_WEB, T_AGENT, T_MCP, T_OTHER, T_COUNT };
enum Flags : uint8_t { F_BOWL_IS_LIMIT = 1, F_PAUSED = 2 };
const uint8_t CONTEXT_UNKNOWN = 255;

inline bool waiting(uint8_t state) { return state == S_PERMISSION || state == S_QUESTION; }

struct Slot {
  uint8_t index = 0, state = S_IDLE, tool = T_THINK, subagents = 0, species = 0, variant = 0, context = CONTEXT_UNKNOWN, minutes = 0;
  char label[5] = "    ";
};

struct Snapshot {
  uint8_t seq = 0, flags = 0, bowl = 0, hidden = 0, hiddenWaiting = 0, n = 0;
  Slot slot[MAX_SLOTS];
  bool bowlIsLimit() const { return flags & F_BOWL_IS_LIMIT; }
  bool paused() const { return flags & F_PAUSED; }
};

// Strict decoder: false (out untouched) for a wrong opcode or version, a length that does not match n exactly, n > 6, a slot index
// above 5 or used twice, an unknown state or tool category, bowl > 100, hidden counts > 99, subagents > 15, context 101..254, or a
// label byte outside printable ASCII. Species and variant are free (the app takes them modulo what it can draw).
bool decode(const uint8_t* d, size_t len, Snapshot& out);

// Latest write wins. put() runs in the BLE callback: it copies the bytes (a seqlock keeps the reader from seeing half a write)
// and nothing else. latest() / lastWriteMs() run in the loop: they decode the newest write and keep the last GOOD snapshot when
// the newest is bad. One writer context, one reader context.
class Mailbox {
 public:
  void put(const uint8_t* d, uint16_t len, uint32_t nowMs);
  // True once a good snapshot exists. out = the last good one; seq counts the good snapshots taken so far (changes when a new one
  // arrives, 0 before the first).
  bool latest(Snapshot& out, uint32_t& seq);
  uint32_t lastWriteMs();           // when the last GOOD snapshot was written (0 before the first)
  bool everGood() { refresh(); return seq_ != 0; }
  uint32_t rejected() const { return rejected_; }   // writes that were not a good snapshot (too long, or decode failed)

 private:
  void refresh();
  static const int CAP = 96;
  std::atomic<uint32_t> ver_{0};    // odd while put() writes
  uint8_t raw_[CAP] = {};
  uint16_t rawLen_ = 0;
  uint32_t rawMs_ = 0;
  uint32_t writes_ = 0;             // written by put() only
  // reader side
  uint32_t seen_ = 0, seq_ = 0, goodMs_ = 0, rejected_ = 0;
  Snapshot good_;
};

}  // namespace pet
