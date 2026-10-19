#include "platform/sensors/sun.h"
#include <math.h>

namespace {

const double kD2R = 0.017453292519943295;

// Days from 1970-01-01 to a civil date (proleptic Gregorian).
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

double wrap360(double a) {
  a = fmod(a, 360.0);
  return a < 0 ? a + 360.0 : a;
}

}  // namespace

bool sunPosition(int year, int month, int day, int utcSecOfDay, double lat, double lon, float* azDeg, float* elDeg) {
  if (year < 1950 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  if (utcSecOfDay < 0 || utcSecOfDay >= 86400 || !(fabs(lat) <= 90) || !(fabs(lon) <= 180)) return false;
  // days since J2000.0 (2000-01-01 12:00 UTC is Unix day 10957.5)
  const double n = (double)(daysFromCivil(year, month, day) - 10957) - 0.5 + utcSecOfDay / 86400.0;
  const double L = wrap360(280.460 + 0.9856474 * n);                       // mean longitude
  const double g = wrap360(357.528 + 0.9856003 * n) * kD2R;                // mean anomaly
  const double lam = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * kD2R;     // ecliptic longitude
  const double eps = (23.439 - 0.0000004 * n) * kD2R;
  const double ra = atan2(cos(eps) * sin(lam), cos(lam));
  const double dec = asin(sin(eps) * sin(lam));
  const double gmst = wrap360(280.46061837 + 360.98564736629 * n);
  const double H = (gmst + lon) * kD2R - ra;                               // hour angle
  const double la = lat * kD2R;
  const double sinEl = sin(la) * sin(dec) + cos(la) * cos(dec) * cos(H);
  const double el = asin(sinEl > 1 ? 1 : (sinEl < -1 ? -1 : sinEl));
  const double az = atan2(-sin(H), tan(dec) * cos(la) - sin(la) * cos(H));
  *elDeg = (float)(el / kD2R);
  *azDeg = (float)wrap360(az / kD2R);
  return true;
}
