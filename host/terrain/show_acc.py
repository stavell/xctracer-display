"""Print accuracy table from acc.json."""
import json
import tlib

d = json.load(open(f"{tlib.DATA}/acc.json"))
for k in ("region", "tracks"):
    print(f"== {k}: error vs native bilinear, metres. columns: rms p95 p99 max | flat(<20deg) rms p99 | mtn(>=20deg) rms p99 | bias")
    for S in (90, 100, 125, 150, 175, 200, 225, 250, 300, 500):
        for q in (1, 2, 4):
            a = d[f"{S}|q{q}|{k}|all"]; f = d[f"{S}|q{q}|{k}|flat"]; m = d[f"{S}|q{q}|{k}|mtn"]
            print(f"S={S:3d} q{q}: {a['rms']:5.1f} {a['p95']:5.1f} {a['p99']:5.1f} {a['max']:6.1f} | {f['rms']:5.1f} {f['p99']:5.1f} | {m['rms']:5.1f} {m['p99']:5.1f} | {a['bias']:+.2f}")
print("== MAX-layer bound (q2 mean + ceil offset): fraction of points where bound < native, worst shortfall m, mean margin m")
for S in (90, 100, 125, 150, 175, 200, 225, 250, 300, 500):
    for qx in (5, 10):
        r = d[f"{S}|bound{qx}|region"]; t = d[f"{S}|bound{qx}|tracks"]
        print(f"S={S} off{qx}: region below {r['frac_below']*100:.2f}% worst {r['min']:.0f} margin {r['mean_margin']:.0f} | tracks below {t['frac_below']*100:.2f}% worst {t['min']:.0f}")
