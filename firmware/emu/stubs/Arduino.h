// Minimal Arduino shim so firmware UI code can be compiled and rendered on a PC.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <algorithm>
#include "avr/pgmspace.h"
using std::min;
using std::max;
typedef uint8_t byte;
#define DEG_TO_RAD 0.017453292519943295
#define PIN_VBAT 0
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) / DEG_TO_RAD)
unsigned long millis();
class __FlashStringHelper;
#ifndef F
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))
#endif
#include "WString.h"
#include "Print.h"
