#pragma once
#include "core/types.h"

// Everything the screens need, as plain data. Built by the app, consumed by the view.
struct ViewModel {
  enum Link { LINK_NONE, LINK_SEARCHING, LINK_CONNECTING, LINK_CONNECTED };
  Link link = LINK_NONE;
  bool hasSaved = false;
  char device[24] = "";
  char filter[24] = "*";
  uint32_t linesOk = 0, linesBad = 0;
  bool fresh = false;            // a valid sample arrived recently
  bool sim = false;
  Sample s;                      // latest sample
  float avgVario = 0;
  float glide = NAN;
  Wind wind;
  int localMinutes = -1;         // minutes since midnight, local time; -1 = unknown
  int localBattPct = 0;
};

struct ListItem {
  char name[24];
  int rssi;
};

struct ListModel {
  char filter[24] = "*";
  int count = 0;
  ListItem items[8];
  int selected = 0;              // 0 = "< back", 1.. = items
};
