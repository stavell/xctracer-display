"""Independent reference wind from an IGC log, compared with the device estimator (circle fit, EKF, shown wind()).

  python host/wind_ref.py flight.igc [--starts 0,1500,4000,4185] [--emu firmware/emu/out/emu] [--table] [--csv]

Reference (pure Python, shares no code with the firmware):
  A. a *geometric* least-squares circle of the ground-velocity vectors (Gauss-Newton from the algebraic fit,
     robust: points more than 2.5 MAD from the circle are dropped and the fit repeated) for every window of
     36 s in which the glider turned the same way by at least 300 degrees. Strict gates: radius (airspeed)
     25..50 km/h, residual <= 6 % of the radius, >= 300 degrees of heading covered, at least 80 % of the
     points kept, centre inside 60 km/h.
  B. the thermal drift: the centroid of the track over one whole turn moves with the air; the slope of that
     centroid over a circling episode (>= 100 s) is the drift. Only a sanity check on A (thermals drift at
     80..100 % of the wind), shown in the table.
The reference wind at a second is the median of the accepted A windows centred within +-45 s, valid when at
least 3 agree within 2.5 km/h of their median. "Soft" reference: that value held for up to 300 s after.

The device side is firmware/emu/out/emu windtrace (per-second CSV) run on the same flight started at each
offset, so the start offset (EKF seeding, first fits) is part of the test. Error = length of the vector
difference in km/h. "confident wrong" = seconds in which the shown wind is valid, not marked old (age <= 120 s)
and more than 8 km/h from the soft reference.
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from igc import read_igc, xctrc_line  # noqa: E402

WIN = 36


def vel(f):
    c = math.radians(f.course)
    v = f.speed / 3.6
    return v * math.sin(c), v * math.cos(c)


def kasa(pts):
    n = len(pts)
    sx = sy = sxx = syy = sxy = sz = sxz = syz = 0.0
    for x, y in pts:
        z = x * x + y * y
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y
        sz += z; sxz += x * z; syz += y * z
    A = [[sxx, sxy, sx], [sxy, syy, sy], [sx, sy, n]]
    B = [sxz, syz, sz]
    return solve3(A, B)


def solve3(A, B):
    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    D = det(A)
    if abs(D) < 1e-9:
        return None
    sol = []
    for k in range(3):
        M = [r[:] for r in A]
        for i in range(3):
            M[i][k] = B[i]
        sol.append(det(M) / D)
    a, b = sol[0] / 2, sol[1] / 2
    r2 = sol[2] + a * a + b * b
    return (a, b, math.sqrt(r2)) if r2 > 0 else None


def geo_fit(pts, iters=12):
    """Geometric circle fit (minimise sum (|p-c| - r)^2) by Gauss-Newton from the algebraic start."""
    s = kasa(pts)
    if not s:
        return None
    a, b, r = s
    for _ in range(iters):
        jtj = [[0.0] * 3 for _ in range(3)]
        jtr = [0.0] * 3
        for x, y in pts:
            dx, dy = x - a, y - b
            d = math.hypot(dx, dy)
            if d < 1e-9:
                continue
            j = (-dx / d, -dy / d, -1.0)
            e = d - r
            for i in range(3):
                jtr[i] += j[i] * e
                for k in range(3):
                    jtj[i][k] += j[i] * j[k]
        step = solve3_plain(jtj, jtr)
        if not step:
            break
        a -= step[0]; b -= step[1]; r -= step[2]
        if abs(step[0]) + abs(step[1]) + abs(step[2]) < 1e-6:
            break
    return a, b, abs(r)


def solve3_plain(A, B):
    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    D = det(A)
    if abs(D) < 1e-12:
        return None
    out = []
    for k in range(3):
        M = [r[:] for r in A]
        for i in range(3):
            M[i][k] = B[i]
        out.append(det(M) / D)
    return out


def wrap(a):
    return (a + 180) % 360 - 180


def coverage(courses):
    a = sorted(c % 360 for c in courses)
    return 360 - max((a[(i + 1) % len(a)] - a[i]) % 360 for i in range(len(a)))


def circle_windows(fx):
    """Accepted reference windows: list of (t_centre, east, north, airspeed_ms) from the IGC at 1 Hz."""
    out = []
    n = len(fx)
    for i in range(0, n - WIN):
        seg = fx[i:i + WIN]
        if seg[-1].t - seg[0].t != WIN - 1:
            continue                                      # gap in the log
        if min(f.speed for f in seg) < 8:
            continue
        turn = sum(wrap(seg[k].course - seg[k - 1].course) for k in range(1, WIN))
        absturn = sum(abs(wrap(seg[k].course - seg[k - 1].course)) for k in range(1, WIN))
        if abs(turn) < 300 or abs(turn) < 0.85 * absturn:
            continue                                      # not one steady turn
        if coverage([f.course for f in seg]) < 300:
            continue
        pts = [vel(f) for f in seg]
        keep = pts
        fit = None
        for _ in range(3):
            fit = geo_fit(keep)
            if not fit:
                break
            res = [abs(math.hypot(x - fit[0], y - fit[1]) - fit[2]) for x, y in pts]
            mad = sorted(res)[len(res) // 2] + 1e-3
            keep2 = [p for p, e in zip(pts, res) if e <= 2.5 * 1.4826 * mad + 0.05]
            if len(keep2) < 0.8 * WIN:
                fit = None
                break
            if len(keep2) == len(keep):
                break
            keep = keep2
        if not fit:
            continue
        a, b, r = fit
        rms = math.sqrt(sum((math.hypot(x - a, y - b) - r) ** 2 for x, y in keep) / len(keep))
        if not (25 / 3.6 <= r <= 50 / 3.6) or rms > 0.06 * r or math.hypot(a, b) > 60 / 3.6:
            continue
        out.append((seg[WIN // 2].t, a, b, r))
    return out


def reference(fx):
    """dict t -> (east, north, hard) ; hard=True inside the +-45 s support, False for the 300 s hold."""
    wins = circle_windows(fx)
    ref = {}
    last = None
    for f in fx:
        t = f.t
        near = [w for w in wins if abs(w[0] - t) <= 45]
        ok = None
        if len(near) >= 3:
            me = sorted(w[1] for w in near)[len(near) // 2]
            mn = sorted(w[2] for w in near)[len(near) // 2]
            good = [w for w in near if math.hypot(w[1] - me, w[2] - mn) * 3.6 <= 2.5]
            if len(good) >= 3:
                ok = (sorted(w[1] for w in good)[len(good) // 2], sorted(w[2] for w in good)[len(good) // 2])
        if ok:
            ref[t] = (ok[0], ok[1], True)
            last = (t, ok)
        elif last and t - last[0] <= 300:
            ref[t] = (last[1][0], last[1][1], False)
    return ref, wins


def drift_episodes(fx):
    """B: wind from thermal drift. Per circling episode (steady turning >= 100 s), centroid of the track over a
    sliding 36 s window vs time, least-squares slope."""
    eps, cur = [], []
    for i in range(1, len(fx)):
        turning = abs(wrap(fx[i].course - fx[i - 1].course)) >= 4 and fx[i].speed > 8
        if turning:
            cur.append(i)
        else:
            if len(cur) > 100 and fx[cur[-1]].t - fx[cur[0]].t < len(cur) + 10:
                eps.append(cur)
            cur = []
    if len(cur) > 100:
        eps.append(cur)
    res = []
    kx = 111320.0 * math.cos(math.radians(fx[0].lat))
    for ep in eps:
        ts, xs, ys = [], [], []
        for j in range(ep[0], ep[-1] - WIN):
            seg = fx[j:j + WIN]
            ts.append(seg[WIN // 2].t)
            xs.append(sum((f.lon - fx[0].lon) * kx for f in seg) / WIN)
            ys.append(sum((f.lat - fx[0].lat) * 111320.0 for f in seg) / WIN)
        if len(ts) < 40:
            continue
        mt = sum(ts) / len(ts)
        den = sum((t - mt) ** 2 for t in ts)
        sx = sum((t - mt) * x for t, x in zip(ts, xs)) / den
        sy = sum((t - mt) * y for t, y in zip(ts, ys)) / den
        res.append((ts[0], ts[-1], sx, sy))
    return res


def run_emu(emu, date, fx, t0, t1, tag):
    lines = [xctrc_line(date, f) for f in fx if t0 <= f.t - fx[0].t < t1]
    with tempfile.NamedTemporaryFile("w", suffix=".lines", delete=False) as fh:
        fh.write("\n".join(lines) + "\n")
        name = fh.name
    try:
        out = subprocess.run([emu, "windtrace", name], capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(name)
    rows = out.strip().split("\n")
    hdr = rows[0].split(",")
    data = []
    for r in rows[1:]:
        v = r.split(",")
        data.append({h: float(x) for h, x in zip(hdr, v)})
    return data


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p / 100 * len(v)))] if v else float("nan")


def compare(data, ref, tmin):
    """Per estimator error statistics against the reference."""
    res = {}
    names = {"circle fit (smoothed)": ("cw_ok", "cw_e", "cw_n"), "EKF": None, "shown wind()": ("w_ok", "w_e", "w_n")}
    for name, keys in names.items():
        hard, soft = [], []
        wrong = shown = 0
        for d in data:
            t = int(d["t"])
            if t < tmin or t not in ref:
                continue
            if keys:
                if not d[keys[0]]:
                    continue
                e, n = d[keys[1]], d[keys[2]]
            else:
                if not d["ekf_conf"]:
                    continue
                e, n = d["ekf_e"], d["ekf_n"]
            re_, rn, isHard = ref[t]
            err = math.hypot(e - re_, n - rn) * 3.6
            (hard if isHard else soft).append(err)
            if name == "shown wind()":
                shown += 1
                if d["w_age"] <= 120 and err > 8:
                    wrong += 1
        res[name] = (hard, soft, shown, wrong)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("igc")
    ap.add_argument("--starts", default="0,1500,4000,4185")
    ap.add_argument("--emu", default=os.path.join(os.path.dirname(__file__), "..", "firmware", "emu", "out", "emu"))
    ap.add_argument("--table", action="store_true", help="print the reference windows' episodes and drift check")
    ap.add_argument("--csv", help="write per-second reference + device rows of the first start to this file")
    a = ap.parse_args()
    date, fx = read_igc(a.igc)
    ref, wins = reference(fx)
    hard = [t for t, v in ref.items() if v[2]]
    print(f"{os.path.basename(a.igc)}: {len(fx)} fixes, {len(wins)} accepted reference windows, "
          f"reference valid (hard) {len(hard)} s, with 300 s hold {len(ref)} s")
    if a.table:
        drifts = drift_episodes(fx)
        for t0, t1, sx, sy in drifts:
            near = [ref[t] for t in range(t0, t1) if t in ref and ref[t][2]]
            if not near:
                continue
            me = sorted(v[0] for v in near)[len(near) // 2]
            mn = sorted(v[1] for v in near)[len(near) // 2]
            print(f"  episode {t0 - fx[0].t:5d}..{t1 - fx[0].t:5d} s: circle-reference {math.hypot(me, mn) * 3.6:5.1f} km/h to "
                  f"{math.degrees(math.atan2(me, mn)) % 360:3.0f} | thermal drift {math.hypot(sx, sy) * 3.6:5.1f} km/h to "
                  f"{math.degrees(math.atan2(sx, sy)) % 360:3.0f}")
    print(f"{'start':>6} {'estimator':<22} {'n':>5} {'med':>5} {'p90':>5} {'max':>6} | {'soft n':>6} {'soft p90':>8} {'soft max':>8}"
          f" | shown {'n':>5} confident-wrong")
    for st in [int(x) for x in a.starts.split(",")]:
        data = run_emu(a.emu, date, fx, st, 10 ** 9, st)
        tmin = fx[0].t + st
        res = compare(data, ref, tmin)
        for name, (h, s, shown, wrong) in res.items():
            extra = f" | shown {shown:5d} {wrong:5d}" if name == "shown wind()" else ""
            print(f"{st:6d} {name:<22} {len(h):5d} {pct(h, 50):5.1f} {pct(h, 90):5.1f} {max(h) if h else float('nan'):6.1f} |"
                  f" {len(s):6d} {pct(s, 90):8.1f} {max(s) if s else float('nan'):8.1f}{extra}")
        if a.csv and st == int(a.starts.split(",")[0]):
            with open(a.csv, "w") as fh:
                fh.write("t,ref_e,ref_n,ref_hard,w_e,w_n,w_ok,ekf_e,ekf_n,ekf_conf,cw_e,cw_n,cw_ok\n")
                for d in data:
                    t = int(d["t"])
                    r = ref.get(t, (float("nan"), float("nan"), 0))
                    fh.write(f"{t},{r[0]:.3f},{r[1]:.3f},{int(r[2])},{d['w_e']},{d['w_n']},{int(d['w_ok'])},{d['ekf_e']},{d['ekf_n']},"
                             f"{int(d['ekf_conf'])},{d['cw_e']},{d['cw_n']},{int(d['cw_ok'])}\n")


main()
