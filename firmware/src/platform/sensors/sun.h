#pragma once
// Where the sun is: azimuth and elevation from the UTC date and time of day and a position. The low-precision
// formulas of the Astronomical Almanac (about 0.01 degree for 1950-2050), more than a shaded map needs. Pure maths.
#include <stdint.h>

// azDeg: clockwise from north, 0..360; elDeg: above the horizon (no refraction), negative at night.
// False for an impossible date, time or position. The day count is a double (a day fraction in a float would be
// 1.4 minutes coarse), the rest float-sized work; it is called about once a minute.
bool sunPosition(int year, int month, int day, int utcSecOfDay, double lat, double lon, float* azDeg, float* elDeg);
