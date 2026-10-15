#include "core/devmatch.h"
#include <ctype.h>
#include <string.h>

bool sameAddr(const DevAddr& a, const DevAddr& b) { return memcmp(a.addr, b.addr, 6) == 0; }

static bool globRec(const char* p, const char* s) {
  while (*p) {
    if (*p == '*') {
      while (*p == '*') p++;
      if (!*p) return true;
      for (; *s; s++) if (globRec(p, s)) return true;
      return false;
    }
    if (!*s) return false;
    if (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s)) return false;
    p++; s++;
  }
  return !*s;
}

bool globMatch(const char* p, const char* s) { return !*p || globRec(p, s); }

bool shouldAutoConnect(bool hasSaved, const DevAddr& saved, const char* savedName,
                       const DevAddr& seen, const char* seenName) {
  if (!hasSaved) return false;
  if (sameAddr(saved, seen)) return true;
  bool priv = saved.type == ADDR_RANDOM_PRIVATE_RESOLVABLE || saved.type == ADDR_RANDOM_PRIVATE_NON_RESOLVABLE;
  return priv && strcmp(savedName, seenName) == 0;
}

void orderByRssi(const int8_t* rssi, int n, int* order) {
  for (int i = 0; i < n; i++) order[i] = i;
  for (int a = 1; a < n; a++) {
    int v = order[a], b = a;
    while (b > 0 && rssi[order[b - 1]] < rssi[v]) { order[b] = order[b - 1]; b--; }
    order[b] = v;
  }
}
