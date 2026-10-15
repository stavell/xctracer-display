#pragma once
#include <Arduino.h>
#include <bluefruit.h>

// BLE central side: finds and connects to the vario (XC Tracer) by name filter,
// subscribes to its $XCTRC stream and derives flight data from it.
enum VarioState { VS_IDLE, VS_SEARCHING, VS_CONNECTING, VS_CONNECTED };

struct ScanItem {
  char name[24];
  ble_gap_addr_t addr;
  int8_t rssi;
  uint32_t seenMs;
};
static const int MAX_ITEMS = 8;

// one parsed $XCTRC line
struct VarioData {
  bool fix = false;
  double lat = 0, lon = 0;
  float alt = NAN, speedKmh = NAN, course = NAN, vario = NAN, pressure = NAN;
  int battPct = -1;
  uint8_t hh = 0, mm = 0, ss = 0;   // UTC from the vario
};

// one sample per second, for averages and the thermal screens
struct VarioSecond {
  bool fix;
  double lat, lon;
  float alt, vario, speedKmh, course;
};
static const int VARIO_HIST = 90;

void varioBegin();                         // after Bluefruit.begin(1, 1)
void varioUpdate();                        // call every loop
VarioState varioState();
const char* varioDeviceName();
bool varioHasSaved();

// discovery list
void varioListOpen();
void varioListClose();
int varioListCount();
const ScanItem& varioListItem(int i);      // strongest first
bool varioListConnect(int i);
void varioForget();
void varioDisconnect();

// name filter, glob with * and ?, case-insensitive
const char* varioFilter();
void varioSetFilter(const char* f);

// live data
const VarioData& varioData();
bool varioFresh();                         // a valid line arrived in the last 3 s
uint32_t varioAgeMs();
uint32_t varioLines();
uint32_t varioBadLines();
const char* varioLastRaw();

// derived
float varioAvg();                          // m/s over VARIO_AVG_S
float varioGlide();                        // NAN when not sinking / no GPS
bool varioFlying();
uint32_t varioFlightSec();
bool varioCircling();                      // sustained turning (thermalling)
int varioHistCount();
const VarioSecond& varioHist(int i);       // 0 = oldest
int varioLocalMinutes();                   // minutes since midnight, local (UTC + tz)

// local time zone offset in minutes, stored in flash
int16_t varioTz();
void varioSetTz(int16_t minutes);

// feed a raw $XCTRC line (no CR/LF) as if it came from the tracer; used by log replay
void varioInject(const char* line);

// bench simulator: synthetic glide / thermal / glide flight
void varioSim(bool on);
bool varioSimOn();
