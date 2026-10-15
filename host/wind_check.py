"""Offline check of the circle-fit wind estimator on an IGC log (same maths as the firmware)."""
import math
import sys
from igc import read_igc

def fit(samples):
    # samples: list of (east, north) ground velocity m/s. Kasa fit: x^2+y^2 = 2a x + 2b y + c
    n = len(samples)
    sx = sy = sxx = syy = sxy = sz = sxz = syz = 0.0
    for x, y in samples:
        z = x * x + y * y
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y
        sz += z; sxz += x * z; syz += y * z
    A = [[sxx, sxy, sx], [sxy, syy, sy], [sx, sy, n]]
    B = [sxz, syz, sz]
    # solve 3x3 by Cramer
    def det(m):
        return (m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))
    D = det(A)
    if abs(D) < 1e-9:
        return None
    sol = []
    for k in range(3):
        M = [row[:] for row in A]
        for i in range(3):
            M[i][k] = B[i]
        sol.append(det(M) / D)
    a, b = sol[0] / 2, sol[1] / 2
    r2 = sol[2] + a * a + b * b
    if r2 <= 0:
        return None
    r = math.sqrt(r2)
    res = math.sqrt(sum((math.hypot(x - a, y - b) - r) ** 2 for x, y in samples) / n)
    return a, b, r, res

def coverage(courses):
    a = sorted(c % 360 for c in courses)
    gaps = [(a[(i + 1) % len(a)] - a[i]) % 360 for i in range(len(a))]
    return 360 - max(gaps)

date, fx = read_igc(sys.argv[1])
W = 30
out = []
for i in range(W, len(fx)):
    seg = fx[i - W:i]
    if min(f.speed for f in seg) < 8:
        continue
    if coverage([f.course for f in seg]) < 200:
        continue
    v = [(f.speed / 3.6 * math.sin(math.radians(f.course)), f.speed / 3.6 * math.cos(math.radians(f.course))) for f in seg]
    r = fit(v)
    if not r:
        continue
    a, b, rad, res = r
    ws = math.hypot(a, b) * 3.6
    if not (14 <= rad * 3.6 <= 72) or res > 0.2 * rad or ws > 72:
        continue
    out.append((fx[i].t, ws, math.degrees(math.atan2(a, b)) % 360, rad * 3.6, res))

print("accepted estimates:", len(out), "of", len(fx) - W)
# summarise per circling episode (gap > 60 s starts a new one)
eps, cur = [], []
for o in out:
    if cur and o[0] - cur[-1][0] > 60:
        eps.append(cur); cur = []
    cur.append(o)
if cur: eps.append(cur)
print("episodes:", len(eps))
for e in eps:
    ws = sorted(o[1] for o in e); dr = [o[2] for o in e]
    # circular mean direction (wind blows TOWARD)
    mx = sum(math.sin(math.radians(d)) for d in dr); my = sum(math.cos(math.radians(d)) for d in dr)
    md = math.degrees(math.atan2(mx, my)) % 360
    t = e[0][0]
    print(f"{t//3600:02d}:{(t//60)%60:02d}  n={len(e):3d}  wind {ws[len(ws)//2]:4.1f} km/h (p10-p90 {ws[len(ws)//10]:.0f}-{ws[-1-len(ws)//10]:.0f})  toward {md:3.0f} deg  airspeed~{sorted(o[3] for o in e)[len(e)//2]:.0f}")
