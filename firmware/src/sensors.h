#pragma once
#include <Arduino.h>

enum Page { PAGE_VARIO, PAGE_ENV, PAGE_COMPASS, PAGE_MOTION, PAGE_LIGHT, PAGE_SOUND, PAGE_COUNT };

struct Readings {
  float tempC = NAN, hum = NAN, pressHpa = NAN, altM = NAN;
  float lux = NAN;
  uint16_t r = 0, g = 0, b = 0, c = 0;
  uint8_t prox = 0;
  uint8_t gesture = 0;       // 1 down, 2 up, 3 left, 4 right (APDS9960 codes)
  uint32_t gestureMs = 0;
  float ax = 0, ay = 0, az = 0;     // m/s^2, board frame after remap
  float gx = 0, gy = 0, gz = 0;     // deg/s
  float mx = 0, my = 0, mz = 0;     // uT, calibrated
  float heading = NAN;              // degrees, 0 = north, top of screen direction
  float pitch = 0, roll = 0;        // degrees
  float fieldUt = 0;
  float battV = 0;
  int battPct = 0;
  float micDb = -90, micPeakDb = -90;   // dBFS
};

struct CalState {
  bool valid = false;       // stored calibration exists
  bool active = false;      // calibration in progress
  float spanX = 0, spanY = 0, spanZ = 0;   // uT covered so far
};

void sensorsBegin();
void sensorsSetPage(Page p);     // powers sensors up/down for the page
void sensorsSleep();             // everything off
void sensorsUpdate();            // call often; each sensor runs on its own timer
const Readings& readings();
const CalState& calState();
void calStart();
void calFinish(bool save);
