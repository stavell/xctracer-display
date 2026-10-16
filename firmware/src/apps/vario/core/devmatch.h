#pragma once
// Pure device-matching logic for the BLE central: name globs, the auto-connect policy and the
// strongest-signal-first ordering of the scan list.
#include <stdint.h>

// Nordic BLE_GAP_ADDR_TYPE_* values.
enum DevAddrType : uint8_t {
  ADDR_PUBLIC = 0,
  ADDR_RANDOM_STATIC = 1,
  ADDR_RANDOM_PRIVATE_RESOLVABLE = 2,
  ADDR_RANDOM_PRIVATE_NON_RESOLVABLE = 3,
};

struct DevAddr {
  uint8_t addr[6] = {0, 0, 0, 0, 0, 0};
  uint8_t type = ADDR_PUBLIC;
};

bool sameAddr(const DevAddr& a, const DevAddr& b);      // the 6 address bytes only

// '*' any run, '?' any one character, case-insensitive. An empty pattern matches everything.
bool globMatch(const char* pattern, const char* name);

// Connect to the saved vario when the address matches exactly. A name match is only a fallback for
// devices whose address changes (private addresses): with a static address, two units sharing a name
// must not be mixed up.
bool shouldAutoConnect(bool hasSaved, const DevAddr& saved, const char* savedName,
                       const DevAddr& seen, const char* seenName);

// Fills order[0..n) with indices into rssi[], strongest first (stable for equal values).
void orderByRssi(const int8_t* rssi, int n, int* order);
