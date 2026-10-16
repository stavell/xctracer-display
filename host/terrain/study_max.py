"""Alternative conservative layer: a COARSE absolute max grid (cell max, rounded UP to q metres) instead of a per-cell offset.
Lookup = nearest-cell value (the cell containing the point). Reports size (T=32, MED+Rice, buffer 10 km) and how much it
exceeds the native terrain at region/track samples (margin) and how often it is below it (violation)."""
import glob
import numpy as np
import tlib, codec

rng = np.random.default_rng(2)
roi90 = tlib.roi_mask(90, 0)
rr, cc = np.nonzero(roi90)
pick = rng.choice(len(rr), 300000, replace=False)
dy, dx, _, _ = tlib.grid_geom(90)
rl = tlib.LAT_N - (rr[pick] + rng.random(len(pick))) * dy
ro = tlib.LON_W + (cc[pick] + rng.random(len(pick))) * dx
tr = [tlib.read_igc(f) for f in sorted(glob.glob("/Users/stavel/Downloads/*.igc"))]
tl = np.concatenate([g["lat"] for g in tr]); to = np.concatenate([g["lon"] for g in tr])
mos = tlib.mosaic()
T = 32
for S in (300, 500, 750, 1000):
    mx = tlib.resample(S, "max")
    roi = tlib.roi_mask(S, 10) if S in (300, 500) else None
    if roi is None:
        # compute the 10 km ROI at this size directly
        roi = tlib.roi_mask(S, 10)
    tm = codec.tile_mask(roi, T)
    dyS, dxS, _, _ = tlib.grid_geom(S)
    for q in (10, 20):
        v = codec.pad_to_tiles(np.ceil(mx / q).astype(np.int16), T)
        sz = int(codec.layer_tile_sizes(v, T, 2, tm).sum()) + 3 * (tm.size + 1)
        rec = np.ceil(mx / q) * q
        out = []
        for name, la, lo in (("region", rl, ro), ("tracks", tl, to)):
            r = np.clip(((tlib.LAT_N - la) / dyS).astype(int), 0, rec.shape[0] - 1)
            c = np.clip(((lo - tlib.LON_W) / dxS).astype(int), 0, rec.shape[1] - 1)
            d = rec[r, c] - tlib.sample_grid(mos, 0, la, lo)
            out.append(f"{name}: below {100*(d<0).mean():.2f}% (worst {d.min():.0f}) margin mean {d.mean():.0f} p95 {np.percentile(d,95):.0f}")
        print(f"S={S} q={q}: {sz/1e3:.0f} kB | " + " | ".join(out), flush=True)
