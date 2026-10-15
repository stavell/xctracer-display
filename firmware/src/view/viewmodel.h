#pragma once
#include "core/thermal.h"
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

  enum Page { PAGE_COMPASS, PAGE_THERMAL };
  Page page = PAGE_COMPASS;
  static const int TRAIL_MAX = 180;      // 3 minutes of dense track
  AirPoint trail[TRAIL_MAX];      // wind-compensated track, filled for the thermal page
  int trailCount = 0;
  static const int MARKS_MAX = ThermalMemory::MAX;
  ThermalRel marks[MARKS_MAX];     // remembered thermals, newest first
  int markCount = 0;
  bool mapHeadingUp = true;       // thermal map: your direction of travel is up (false = north is up)
};

struct ListItem {
  char name[24];
  int rssi;
  char id[8];                    // last two address bytes, to tell same-named devices apart
};

struct ListModel {
  char filter[24] = "*";
  int count = 0;
  ListItem items[8];
  int selected = 0;              // 0 = "< back", 1.. = items
};
