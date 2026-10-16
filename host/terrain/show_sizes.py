"""Print the size table from sizes.json. Usage: show_sizes.py [buffer_km]"""
import json, sys
import tlib
buf = sys.argv[1] if len(sys.argv) > 1 else "10"
d = json.load(open(f"{tlib.DATA}/sizes.json"))
print(f"buffer {buf} km; sizes in kB (1 kB=1000 B), index excluded; max columns use q2 mean quant")
for S in (90, 100, 125, 150, 175, 200, 225, 250, 300, 500):
    for T in (32, 64):
        m = d[f"{S}|{T}|buf{buf}|meta"]
        print(f"S={S} T={T} roi cells {m['cells_roi']} tiles {m['tiles']} index {m['index_bytes']} B")
        for cn in ("raw16", "delta+rice", "med+rice", "med+adaptive"):
            row = []
            for q in (1, 2, 4):
                k = f"{S}|{T}|{cn}|q{q}|buf{buf}|mean"
                if k in d:
                    row.append(f"q{q}:{d[k]/1e3:.0f}")
            mx = [f"max{x}:{d.get(f'{S}|{T}|{cn}|q2|buf{buf}|max{x}', 0)/1e3:.0f}" for x in (5, 10)]
            print("   ", f"{cn:13s}", *row, *mx)
