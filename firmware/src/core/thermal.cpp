#include "core/thermal.h"
#include <math.h>

bool findThermal(const AirPoint* tr, int n, Thermal& out) {
  out = Thermal();
  const float CLIMB = 0.5f, MIN_GAIN = 15.0f;
  const int MAX_GAP = 6, MIN_LEN = 8, MAX_EP = 24;
  struct Ep { int a, b; float gain; } ep[MAX_EP];
  int ne = 0;
  float best = 0;
  int i = 0;
  while (i < n) {
    if (!(tr[i].vario >= CLIMB)) { i++; continue; }
    int a = i, b = i, gap = 0;
    for (int j = i + 1; j < n; j++) {
      if (tr[j].vario >= CLIMB) { b = j; gap = 0; }
      else if (++gap > MAX_GAP) break;
    }
    float gain = 0;
    for (int k = a; k <= b; k++) if (tr[k].vario > 0) gain += tr[k].vario;
    if (b - a + 1 >= MIN_LEN && gain >= MIN_GAIN && ne < MAX_EP) {
      ep[ne++] = {a, b, gain};
      if (gain > best) best = gain;
    }
    i = b + 1;
  }
  int pick = -1;
  for (int e = 0; e < ne; e++) if (ep[e].gain >= 0.6f * best) pick = e;   // most recent of the strong ones
  if (pick < 0) return false;
  // Centre on the circling part when there is one: the straight climb on the way in would pull it sideways.
  int turning = 0;
  for (int k = ep[pick].a; k <= ep[pick].b; k++) if (tr[k].turn >= 5.0f) turning++;
  const bool onlyTurning = turning >= 8;
  double sx = 0, sy = 0, sw = 0;
  for (int k = ep[pick].a; k <= ep[pick].b; k++) {
    if (onlyTurning && tr[k].turn < 5.0f) continue;
    double w = tr[k].vario > 0 ? tr[k].vario : 0;
    sx += tr[k].x * w; sy += tr[k].y * w; sw += w;
  }
  if (sw <= 0) return false;
  out.valid = true;
  out.x = (float)(sx / sw);
  out.y = (float)(sy / sw);
  out.gain = ep[pick].gain;
  out.avgClimb = ep[pick].gain / (ep[pick].b - ep[pick].a + 1);
  out.ageS = (uint32_t)(n - 1 - ep[pick].b);
  return true;
}

void ThermalMemory::reset() { for (int i = 0; i < MAX; i++) m_[i].used = false; }

int ThermalMemory::count() const {
  int n = 0;
  for (int i = 0; i < MAX; i++) if (m_[i].used) n++;
  return n;
}

void ThermalMemory::expire(uint32_t now) {
  for (int i = 0; i < MAX; i++) if (m_[i].used && now - m_[i].endMs > LIFETIME_MS) m_[i].used = false;
}

static const double D2R = 0.017453292519943295;

void ThermalMemory::update(double lat, double lon, const Thermal& f, uint32_t now, double wx, double wy) {
  if (!f.valid) return;
  const double ky = 111320.0, kx = 111320.0 * cos(lat * D2R);
  double clat = lat + f.y / ky, clon = lon + f.x / kx;      // the new centre in ground coordinates, valid now
  uint32_t endMs = now - f.ageS * 1000u;

  int match = -1;
  for (int i = 0; i < MAX; i++) {
    if (!m_[i].used) continue;
    double dt = (now - m_[i].atMs) / 1000.0;                // where that mark has drifted to by now
    double dx = (m_[i].lon - clon) * kx + wx * dt, dy = (m_[i].lat - clat) * ky + wy * dt;
    if (hypot(dx, dy) < 50.0) { match = i; break; }
  }
  if (match >= 0) {
    ThermalMark& m = m_[match];
    double dt = (now - m.atMs) / 1000.0;
    if (f.gain >= m.gain * 0.9f) {                          // a better or equal measurement replaces it
      m.lat = clat; m.lon = clon;
      m.avgClimb = f.avgClimb;
      if (f.gain > m.gain) m.gain = f.gain;
    } else {                                                // keep the old measurement, just move it with the wind
      m.lat += wy * dt / ky;
      m.lon += wx * dt / kx;
    }
    m.atMs = now;
    if (endMs > m.endMs) m.endMs = endMs;
    return;
  }
  int slot = -1;
  for (int i = 0; i < MAX; i++) if (!m_[i].used) { slot = i; break; }
  if (slot < 0) {                                           // full: replace the one that ended longest ago
    slot = 0;
    for (int i = 1; i < MAX; i++) if (m_[i].endMs < m_[slot].endMs) slot = i;
  }
  ThermalMark& m = m_[slot];
  m.used = true; m.lat = clat; m.lon = clon; m.avgClimb = f.avgClimb; m.gain = f.gain; m.atMs = now; m.endMs = endMs;
}

int ThermalMemory::relative(ThermalRel* out, int maxN, double lat, double lon, uint32_t now, double wx, double wy) const {
  const double ky = 111320.0, kx = 111320.0 * cos(lat * D2R);
  int idx[MAX], n = 0;
  for (int i = 0; i < MAX; i++) if (m_[i].used) idx[n++] = i;
  for (int a = 0; a < n; a++)                               // newest first
    for (int b = a + 1; b < n; b++)
      if (m_[idx[b]].endMs > m_[idx[a]].endMs) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
  int cnt = 0;
  for (int k = 0; k < n && cnt < maxN; k++) {
    const ThermalMark& m = m_[idx[k]];
    double dt = (now - m.atMs) / 1000.0;
    out[cnt].x = (float)((m.lon - lon) * kx + wx * dt);
    out[cnt].y = (float)((m.lat - lat) * ky + wy * dt);
    out[cnt].avgClimb = m.avgClimb;
    out[cnt].gain = m.gain;
    out[cnt].ageS = (now - m.endMs) / 1000u;
    cnt++;
  }
  return cnt;
}
