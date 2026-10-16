#include "apps/vario/core/home.h"

static const double D2R = 0.017453292519943295, R2D = 57.29577951308232;

HomeInfo computeHome(double lat, double lon, float altM, float headingDeg, const HomePoint& home,
                     const Wind& wind, float va, float sink) {
  HomeInfo r;
  if (!home.valid || !isfinite(lat) || !isfinite(lon)) return r;
  r.valid = true;
  double dy = (home.lat - lat) * 111320.0;
  double dx = (home.lon - lon) * 111320.0 * cos(0.5 * (lat + home.lat) * D2R);
  float dist = (float)hypot(dx, dy);
  r.distM = dist;
  r.bearingDeg = (float)fmod(atan2(dx, dy) * R2D + 360.0, 360.0);
  if (isfinite(headingDeg)) {
    float rel = fmodf(r.bearingDeg - headingDeg, 360.0f);
    if (rel > 180) rel -= 360;
    if (rel < -180) rel += 360;
    r.relBearingDeg = rel;
  }
  float above = (isfinite(altM) && isfinite(home.alt)) ? altM - home.alt : NAN;
  if (isfinite(above) && above > 1.0f) r.glideReq = dist / above;

  // wind components on the line home: along > 0 is a tailwind
  float along = 0, cross = 0;
  if (wind.valid && isfinite(wind.speedKmh) && isfinite(wind.toDeg)) {
    float ws = wind.speedKmh / 3.6f, th = (float)(wind.toDeg * D2R), b = (float)(r.bearingDeg * D2R);
    float we = ws * sinf(th), wn = ws * cosf(th);
    float ue = sinf(b), un = cosf(b);
    along = we * ue + wn * un;
    cross = fabsf(we * un - wn * ue);
  }
  if (cross >= va) return r;
  float gs = sqrtf(va * va - cross * cross) + along;
  if (gs <= 0.5f) return r;
  r.reachable = true;
  if (isfinite(above)) r.arrivalM = above - sink * (dist / gs);
  return r;
}
