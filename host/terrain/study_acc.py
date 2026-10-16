"""Accuracy of each stored grid vs the native 90 m DEM (bilinear vs bilinear), over Bulgaria and along IGC tracks."""
import glob, json
import numpy as np
import tlib

rng = np.random.default_rng(1)
# region sample: random points in Bulgaria (0 km buffer), uniform in area
roi = tlib.roi_mask(90, 0)
rr, cc = np.nonzero(roi)
pick = rng.choice(len(rr), 600000, replace=False)
dy, dx, _, _ = tlib.grid_geom(90)
rlat = tlib.LAT_N - (rr[pick] + rng.random(len(pick))) * dy
rlon = tlib.LON_W + (cc[pick] + rng.random(len(pick))) * dx
# tracks
tl, tn, names = [], [], []
for f in sorted(glob.glob("/Users/stavel/Downloads/*.igc")):
    g = tlib.read_igc(f)
    tl.append(g["lat"]); tn.append(g["lon"]); names.append(f)
tlat = np.concatenate(tl); tlon = np.concatenate(tn)
print("tracks", names, len(tlat), "points")

mos = tlib.mosaic()
sets = {"region": (rlat, rlon), "tracks": (tlat, tlon)}
truth = {k: tlib.sample_grid(mos, 0, *v) for k, v in sets.items()}
slope = {k: tlib.native_slope_deg(*v) for k, v in sets.items()}
for k in sets:
    print(k, "mean slope %.1f deg, frac>20deg %.3f, frac>30 %.3f" % (slope[k].mean(), (slope[k] > 20).mean(), (slope[k] > 30).mean()))


def stats(e):
    a = np.abs(e)
    return dict(n=int(len(e)), bias=float(e.mean()), rms=float(np.sqrt((e ** 2).mean())), p95=float(np.percentile(a, 95)),
                p99=float(np.percentile(a, 99)), max=float(a.max()))


res = {}
for S in [90, 100, 125, 150, 175, 200, 225, 250, 300, 500]:
    if S == 0:
        continue
    g = tlib.resample(S, "average")
    mx = tlib.resample(S, "max")
    for q in (1, 2, 4):
        rec = np.round(g / q) * q
        for k, (la, lo) in sets.items():
            e = tlib.sample_grid(rec, S, la, lo) - truth[k]
            for cls, m in (("all", slice(None)), ("flat", slope[k] < 20), ("mtn", slope[k] >= 20)):
                res[f"{S}|q{q}|{k}|{cls}"] = stats(e[m])
    # max layer: bound violated?  (mean quantised q=2 + ceil offset qx), sampled bilinear
    for qx in (5, 10):
        rec = np.round(g / 2) * 2
        off = np.clip(np.ceil((mx - rec) / qx), 0, None) * qx
        bound = rec + off
        for k, (la, lo) in sets.items():
            b = tlib.sample_grid(bound, S, la, lo)
            d = b - truth[k]
            res[f"{S}|bound{qx}|{k}"] = dict(frac_below=float((d < 0).mean()), min=float(d.min()), mean_margin=float(d.mean()),
                                              p99_below=float(np.percentile(d, 1)))
    print(S, flush=True)
json.dump(res, open(f"{tlib.DATA}/acc.json", "w"), indent=0)
