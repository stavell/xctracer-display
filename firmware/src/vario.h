#pragma once
#include <Arduino.h>
#include <bluefruit.h>

// BLE central side: finds and connects to the vario (XC Tracer) by name filter.
enum VarioState { VS_IDLE, VS_SEARCHING, VS_CONNECTING, VS_CONNECTED };

struct ScanItem {
  char name[24];
  ble_gap_addr_t addr;
  int8_t rssi;
  uint32_t seenMs;
};

static const int MAX_ITEMS = 8;

void varioBegin();                         // after Bluefruit.begin(1, 1)
void varioUpdate();                        // call every loop
VarioState varioState();
const char* varioDeviceName();             // connected / saved device name ("" if none)
bool varioHasSaved();

// discovery list
void varioListOpen();                      // starts an active scan for names matching the filter
void varioListClose();                     // stops scanning, back to auto-connect
int varioListCount();
const ScanItem& varioListItem(int i);      // sorted by RSSI, strongest first
bool varioListConnect(int i);              // connect + remember
void varioForget();                        // drop saved device and disconnect
void varioDisconnect();

// name filter, glob with * and ?, case-insensitive
const char* varioFilter();
void varioSetFilter(const char* f);

// last raw line received from the vario (for bring-up until the parser exists)
const char* varioLastRaw();
uint32_t varioLastRawMs();
