#pragma once
// Pet snapshots for the emulator: a builder (bytes exactly as the daemon writes them, docs/claude_pet.md 7.3), a mock IPetFeed with a
// Mailbox behind it, and the reader of the feed file format shared with the daemon (`claude_pet.py --fake-ble FILE`):
//   one snapshot per line: `<ms> <hex>`; <ms> = milliseconds since the file start (decimal), <hex> = the full snapshot bytes as
//   lowercase hex without spaces, starting with 30. Lines starting with # are comments.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "platform/pet/pet_feed.h"

namespace petfx {

struct SlotSpec {
  uint8_t idx, state, tool, sub, species, variant, ctx, minutes;
  const char* label;
};

inline std::vector<uint8_t> make(uint8_t seq, uint8_t flags, uint8_t bowl, uint8_t hidden, uint8_t hiddenWaiting, const std::vector<SlotSpec>& slots) {
  std::vector<uint8_t> b = {pet::OP_SNAPSHOT, pet::VERSION, seq, flags, bowl, hidden, hiddenWaiting, (uint8_t)slots.size()};
  for (const SlotSpec& s : slots) {
    const uint8_t h[8] = {s.idx, s.state, s.tool, s.sub, s.species, s.variant, s.ctx, s.minutes};
    b.insert(b.end(), h, h + 8);
    char l[4] = {' ', ' ', ' ', ' '};
    for (int i = 0; i < 4 && s.label[i]; i++) l[i] = s.label[i];
    b.insert(b.end(), l, l + 4);
  }
  return b;
}

// One slot of every state, for a quick feed.
inline std::vector<uint8_t> oneOfEach(uint8_t seq) {
  return make(seq, 0, 40, 0, 0, {{0, pet::S_PERMISSION, 0, 0, 0, 0, 30, 1, "WIDG"}, {1, pet::S_QUESTION, 0, 1, 1, 1, 60, 3, "API"},
                                 {2, pet::S_FAILED, 0, 0, 2, 0, 90, 20, "DOCS"}, {3, pet::S_DONE, 0, 0, 3, 2, 70, 2, "WEB"},
                                 {4, pet::S_WORKING, pet::T_EDIT, 2, 4, 0, 45, 5, "CORE"}, {5, pet::S_IDLE, 0, 0, 5, 3, 10, 90, "OLD"}});
}

struct Feed {
  uint32_t atMs;
  std::vector<uint8_t> bytes;
};

inline int hexNibble(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// False (with a message) when the file cannot be read or a line is malformed.
inline bool loadFeed(const char* path, std::vector<Feed>& out, std::string& err) {
  FILE* f = fopen(path, "r");
  if (!f) { err = std::string("cannot open ") + path; return false; }
  char line[1024];
  int n = 0;
  while (fgets(line, sizeof line, f)) {
    n++;
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
    char* end = nullptr;
    const unsigned long ms = strtoul(p, &end, 10);
    if (end == p) { err = "line " + std::to_string(n) + ": no time"; fclose(f); return false; }
    p = end;
    while (*p == ' ' || *p == '\t') p++;
    Feed e;
    e.atMs = (uint32_t)ms;
    while (hexNibble(p[0]) >= 0 && hexNibble(p[1]) >= 0) { e.bytes.push_back((uint8_t)(hexNibble(p[0]) * 16 + hexNibble(p[1]))); p += 2; }
    if (e.bytes.empty() || (*p != '\n' && *p != '\r' && *p != 0)) { err = "line " + std::to_string(n) + ": bad hex"; fclose(f); return false; }
    out.push_back(e);
  }
  fclose(f);
  return true;
}

inline std::string hex(const std::vector<uint8_t>& b) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (uint8_t c : b) { s += d[c >> 4]; s += d[c & 15]; }
  return s;
}

}  // namespace petfx
