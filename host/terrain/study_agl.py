"""AGL sanity check on the IGC files: altitude at the stationary start/end of each flight minus terrain.
Terrain from: native 90 m bilinear, the proposed stored grid (200 m, 4 m quant) and the 500 m max layer."""
import glob
import numpy as np
import tlib

mos = tlib.mosaic()
g200 = np.round(tlib.resample(200) / 4) * 4
mx500 = np.ceil(tlib.resample(500, "max") / 10) * 10
dy5, dx5, _, _ = tlib.grid_geom(500)
for f in sorted(glob.glob("/Users/stavel/Downloads/*.igc")):
    g = tlib.read_igc(f)
    t, la, lo = g["t"], g["lat"], g["lon"]
    # ground speed from positions, smoothed over 5 samples
    d = np.hypot(np.diff(la) * 111195, np.diff(lo) * 111195 * np.cos(np.radians(la[:-1]))) / np.maximum(np.diff(t), 1)
    sp = np.convolve(d, np.ones(5) / 5, "same") * 3.6
    sp = np.append(sp, sp[-1])
    print(f.split("/")[-1], f"{len(t)} fixes, {(t[-1]-t[0])/60:.0f} min; max alt GNSS {g['galt'].max():.0f} m, baro {g['palt'].max():.0f} m")
    for name, sl in (("takeoff (first 60 s)", slice(0, 60)), ("landing (last 60 s)", slice(-60, None))):
        m = sp[sl] < 5
        idx = np.arange(len(t))[sl][m]
        if len(idx) < 5:
            print(f"  {name}: only {len(idx)} fixes below 5 km/h; skipped"); continue
        la_, lo_ = la[idx], lo[idx]
        tn = tlib.sample_grid(mos, 0, la_, lo_)
        ts = tlib.sample_grid(g200, 200, la_, lo_)
        r = np.clip(((tlib.LAT_N - la_) / dy5).astype(int), 0, mx500.shape[0] - 1)
        c = np.clip(((lo_ - tlib.LON_W) / dx5).astype(int), 0, mx500.shape[1] - 1)
        tx = mx500[r, c]
        ga, pa = np.median(g["galt"][idx]), np.median(g["palt"][idx])
        print(f"  {name}: {len(idx)} fixes <5 km/h at {la_.mean():.4f},{lo_.mean():.4f}: GNSS alt {ga:.0f}, baro alt {pa:.0f}; "
              f"terrain native {tn.mean():.0f} / stored200 {ts.mean():.0f} / max500 {tx.mean():.0f}; "
              f"AGL GNSS {ga-tn.mean():+.0f} (stored {ga-ts.mean():+.0f}), AGL baro(ISA, no QNH) {pa-tn.mean():+.0f}")
    # whole flight: minimum of (GNSS alt - stored terrain) vs native; how often terrain exceeds aircraft
    tn = tlib.sample_grid(mos, 0, la, lo); ts = tlib.sample_grid(g200, 200, la, lo)
    agl_n, agl_s = g["galt"] - tn, g["galt"] - ts
    print(f"  whole flight AGL(GNSS-native): min {agl_n.min():.0f} p5 {np.percentile(agl_n,5):.0f} median {np.median(agl_n):.0f}; "
          f"with stored 200 m: min {agl_s.min():.0f}; |stored - native| along track max {np.abs(ts-tn).max():.1f} m")
