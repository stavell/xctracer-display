#pragma once
#include "apps/vario/core/heading.h"
#include "apps/vario/core/windprofile.h"
#include "apps/vario/core/thermal.h"
#include "apps/vario/core/types.h"

class TerrainMap;

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

  enum Page { PAGE_COMPASS, PAGE_THERMAL, PAGE_PROFILE, PAGE_HOME, PAGE_MAP, PAGE_COUNT };   // PAGE_MAP last: the host's page numbers stay; cycling puts it after the thermal page
  Page page = PAGE_COMPASS;
  static const int TRAIL_MAX = 180;      // 3 minutes of dense track
  AirPoint trail[TRAIL_MAX];      // wind-compensated track, filled for the thermal page
  int trailCount = 0;
  static const int MARKS_MAX = ThermalMemory::MAX;
  ThermalRel marks[MARKS_MAX];     // remembered thermals, newest first
  int markCount = 0;
  bool mapHeadingUp = true;
  // Continuous map motion (app/core/map_motion.h). mapLive = false (static renders, tests): the map is
  // centred on the newest trail point, turned and zoomed on its own. mapLive = true: the trail holds only
  // the points up to the display time (trailCount already reduced), the view centre is (mapCx, mapCy) in
  // the trail's frame, and the line is drawn on to the centre with the vario mapTailVario.
  bool mapLive = false;
  float mapCx = 0, mapCy = 0;      // metres east / north of the newest trail point
  float mapAx = 0, mapAy = 0;      // the same centre in a frame that does not change with the trail origin (app: movement test)
  float mapHdg = NAN;              // radians clockwise from north (NaN: let the view derive it)
  float mapScale = NAN;            // pixels per metre (NaN: the view picks a fixed zoom step)
  float mapTailVario = 0;

  // wind profile page: bands with data, lowest first
  static const int PROFILE_MAX = WindProfile::BANDS;
  ProfileBand profile[PROFILE_MAX];
  int profileCount = 0;
  float profileAlt = NAN;          // current altitude

  // home page
  bool homeValid = false;          // a home position exists
  bool homeProvisional = false;    // first fix, takeoff not seen yet
  float homeDist = NAN;            // metres
  float homeRelBearing = NAN;      // degrees from the heading (NaN: no heading), -180..180
  float homeBearing = NAN;         // true bearing
  float homeGlideReq = NAN;        // NaN when not above home
  float homeArrival = NAN;         // metres above home on arrival (NaN: unknown)
  bool homeReachable = false;

  // terrain (only with a map pack; without one terrainOn is false and nothing of this is drawn)
  bool terrainOn = false;
  float agl = NAN;                 // GNSS altitude minus the terrain model's elevation under the fix; NaN: no fix, no data
  float clearance = NAN;           // GNSS altitude minus the highest terrain around the fix (for warnings)
  TerrainMap* map = nullptr;          // the map page's cached picture and ground track
  float mapMPerPx = 100;           // map page scale
  bool mapAbout = false;           // first seconds on the map page: attribution and the AGL caveat

  // ambient air, NaN without a reading or when the last one is older than 10 s
  float ambientC = NAN;
  float humidityPct = NAN;

  // heading for the compass dial: the GPS track when there is one, else the magnetometer, else none
  HeadingSource headingSrc = HDG_NONE;
  float heading = NAN;
  // magnetometer calibration screen
  bool calibrating = false;
  bool calValid = false;
  float calSpan[3] = {0, 0, 0};    // microtesla covered per axis
  float fieldUt = 0;       // thermal map: your direction of travel is up (false = north is up)
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
  int selected = 0;              // 0 = "< back", [1 = "Home"], then the tracers found
  bool homeRow = false;          // box image: a row that leaves for the home screen, after "< back"
  bool tools = false;            // the compass tool's menu: no tracer, so no filter line and no scanning text
};
