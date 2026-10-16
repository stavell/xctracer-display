"""Finest grid that fits the budget, per buffer / quant / tile / max layer (best codec for each). Total includes indexes."""
import json
import tlib

BUDGET = 1_900_000
d = json.load(open(f"{tlib.DATA}/sizes.json"))
GR = (90, 100, 125, 150, 175, 200, 225, 250, 300, 500)
CN = ("delta+rice", "med+rice", "med+adaptive")
print("kB totals (mean [+ max] + index) for the best codec; * = fits 1.9 MB")
for buf in (0, 10, 20):
    for mx in (None, 10, 5):
        for T in (32, 64):
            for q in (1, 2, 4):
                row = []
                fit = None
                for S in GR:
                    idx = d[f"{S}|{T}|buf{buf}|meta"]["index_bytes"]
                    best = None
                    for cn in CN:
                        t = d[f"{S}|{T}|{cn}|q{q}|buf{buf}|mean"] + idx
                        if mx:
                            t += d[f"{S}|{T}|{cn}|q{q}|buf{buf}|max{mx}"] + idx
                        if best is None or t < best[0]:
                            best = (t, cn)
                    row.append(f"{S}:{best[0]/1e3:.0f}{'*' if best[0] <= BUDGET else ''}")
                    if best[0] <= BUDGET and fit is None:
                        fit = (S, best[1])
                print(f"buf{buf:2d} max={mx or '-':>3} T{T} q{q}: finest fit {fit}   |", " ".join(row))
