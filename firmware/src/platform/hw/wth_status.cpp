#include "platform/hw/wth_status.h"
#include <Arduino.h>
#include "platform/power/wth_line.h"

namespace wth {

void tick(uint32_t now, IBaro& baro, IThermo& thermo, BatteryAdc& battery, const WeatherLog& weather, const char* ret) {
  static uint32_t last = 0;
  if (now - last < 5000) return;
  last = now;
  if (!Serial || Serial.availableForWrite() < 100) return;      // nobody listening: no sensor reads either
  WthData d;
  baro.readNowT(d.hPa, d.baroC);                                // on failure the NaNs stay: printed as --
  thermo.readNow(d.shtC, d.rh);
  d.battPct = battery.percent();
  d.battMv = battery.millivolts();
  d.logCount = weather.count();
  d.ret = ret;
  static char line[96];                                         // static: the loop stack is small
  formatWth(line, sizeof line, d);
  Serial.println(line);
}

}  // namespace wth
