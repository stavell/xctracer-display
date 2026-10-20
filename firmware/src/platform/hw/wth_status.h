#pragma once
// Prints the "wth" status line (platform/power/wth_line.h) every 5 s while a terminal is attached to the USB serial port. Box image only.
#include <stdint.h>
#include "platform/hw/battery_adc.h"
#include "platform/ports.h"
#include "platform/power/weather.h"

namespace wth {

// Reads the barometer (a 10 ms single shot) and the thermometer (single shot, the apps' own cadence is untouched) only when the line is
// printed. ret: the restore result of this boot ("ok" / "crc" / "none").
void tick(uint32_t nowMs, IBaro& baro, IThermo& thermo, BatteryAdc& battery, const WeatherLog& weather, const char* ret);

}  // namespace wth
