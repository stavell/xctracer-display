// Renders the real firmware UI (../src/ui.cpp) with canned data to a PGM image.
// usage: preview <scenario> <out.pgm> [rotation]
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include "../src/vario.h"
#include "../src/battery.h"
#include "../src/ui.h"

static VarioData D;
static VarioWind WND = {true, 14.0f, 262.0f, 12};
static bool fresh = true, sim = false;
static VarioState st = VS_CONNECTED;
static VarioSecond H[VARIO_HIST];
static int HN = 0;
static float avg = 1.8f, glide = 9.4f;
static int minutes = 13 * 60 + 52;
static ScanItem items[3];

unsigned long millis() { return 100000; }
VarioState varioState() { return st; }
const char* varioDeviceName() { return "XC-Tracer"; }
bool varioHasSaved() { return true; }
const char* varioFilter() { return "*"; }
bool varioFresh() { return fresh; }
const VarioData& varioData() { return D; }
uint32_t varioLines() { return 123; }
uint32_t varioBadLines() { return 0; }
float varioAvg() { return avg; }
float varioGlide() { return glide; }
VarioWind varioWind() { return WND; }
bool varioSimOn() { return sim; }
int varioLocalMinutes() { return minutes; }
int varioHistCount() { return HN; }
const VarioSecond& varioHist(int i) { return H[i]; }
uint32_t varioFlightSec() { return 4000; }
int varioListCount() { return 3; }
const ScanItem& varioListItem(int i) { return items[i]; }
int batteryPct() { return 76; }
float batteryVolts() { return 3.95f; }

int main(int argc, char** argv) {
  if (argc < 3) return 1;
  std::string sc = argv[1];
  int rot = argc > 3 ? atoi(argv[3]) : 0;
  D.fix = true; D.alt = 1413; D.speedKmh = 41; D.course = 120; D.vario = 2.4f; D.battPct = 85;
  if (sc == "nowind") { WND.valid = false; D.vario = -1.1f; glide = 9.4f; }
  if (sc == "tail")   { WND.toDeg = 120; }                 // wind blows the way we fly
  if (sc == "head")   { WND.toDeg = 300; D.speedKmh = 28; }
  if (sc == "big")    { D.alt = 2650; D.vario = -4.2f; D.speedKmh = 68; glide = 12.8f; avg = -3.0f; WND.speedKmh = 38; }
  if (sc == "nofix")  { D.fix = false; D.speedKmh = NAN; D.course = NAN; glide = NAN; WND.valid = false; }
  if (sc == "status") { fresh = false; }
  strcpy(items[0].name, "XC-Tracer"); items[0].rssi = -52;
  strcpy(items[1].name, "ATC_0CAA9D"); items[1].rssi = -66;
  strcpy(items[2].name, "Q-Series Soundbar"); items[2].rssi = -78;

  static Adafruit_SharpMem disp(nullptr, 0, 400, 240, 0);
  disp.setRotation(rot);
  uiBegin(&disp);
  if (sc == "list") uiDrawList(1, 0);
  else uiDraw(0);
  disp.dumpPgm(argv[2]);
  return 0;
}
