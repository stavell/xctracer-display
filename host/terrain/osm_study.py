"""Size of compact vector layers: towns/cities and roads (motorway+trunk+primary; secondary separately).
Encoding (estimate = the real bytes of the proposed format, see README): coordinates in 10 m units on the DEM projection
(x = (lon-22E)*cos(42.7)*111195/10, y = (45N-lat)*111195/10), 1024-unit (10.24 km) blocks, polylines Douglas-Peucker
simplified, vertices zigzag-varint delta coded, 3-byte block index.
usage: osm_study.py bg.pkl"""
import pickle, sys, json
import numpy as np
from shapely.geometry import LineString
import tlib

d = pickle.load(open(sys.argv[1], "rb"))
U = 10.0
BLK = 1024
roi = tlib.roi_mask(500, 10)
dy5, dx5, _, _ = tlib.grid_geom(500)


def in_roi(lat, lon):
    r = np.clip(((tlib.LAT_N - lat) / dy5).astype(int), 0, roi.shape[0] - 1)
    c = np.clip(((lon - tlib.LON_W) / dx5).astype(int), 0, roi.shape[1] - 1)
    return roi[r, c]


def xy_m(lon, lat):
    return (np.asarray(lon) - tlib.LON_W) * tlib.COS0 * tlib.M_PER_DEG, (tlib.LAT_N - np.asarray(lat)) * tlib.M_PER_DEG


def varint(n):
    n = int(n)
    b = 1
    while n >= 128:
        n >>= 7; b += 1
    return b


def zz(n):
    n = int(n)
    return (n << 1) if n >= 0 else ((-n << 1) - 1)


def vsize(n):
    return varint(zz(n))


def road_layer_size(ways, tol, with_class=True):
    blocks = {}   # (by,bx) -> list of polylines (list of (x,y) in units)
    nverts_in = nverts_out = 0
    for w in ways:
        lon = np.array([p[0] for p in w["pts"]]); lat = np.array([p[1] for p in w["pts"]])
        nverts_in += len(lon)
        x, y = xy_m(lon, lat)
        ok = in_roi(lat, lon)
        # split into runs inside the region
        i = 0
        n = len(x)
        while i < n:
            if not ok[i]:
                i += 1; continue
            j = i
            while j < n and ok[j]:
                j += 1
            if j - i >= 2:
                ls = LineString(np.c_[x[i:j], y[i:j]])
                s = ls.simplify(tol, preserve_topology=False) if tol > 0 else ls
                q = np.rint(np.array(s.coords) / U).astype(int)
                q = q[np.r_[True, (np.diff(q, axis=0) != 0).any(axis=1)]]   # drop duplicate points
                if len(q) >= 2:
                    nverts_out += len(q)
                    # cut at block changes; the crossing segment belongs to the piece of its first vertex
                    cur = [q[0]]
                    bk = (q[0][1] // BLK, q[0][0] // BLK)
                    for p in q[1:]:
                        cur.append(p)
                        nb = (p[1] // BLK, p[0] // BLK)
                        if nb != bk:
                            blocks.setdefault(bk, []).append(cur)
                            cur = [p]; bk = nb
                    if len(cur) >= 2:
                        blocks.setdefault(bk, []).append(cur)
            i = j
    total = 0
    for (by, bx), pls in blocks.items():
        total += varint(len(pls))
        for pl in pls:
            total += varint(len(pl)) + (1 if with_class else 0)
            ox, oy = bx * BLK, by * BLK
            total += vsize(pl[0][0] - ox) + vsize(pl[0][1] - oy)
            for a, b in zip(pl[:-1], pl[1:]):
                total += vsize(b[0] - a[0]) + vsize(b[1] - a[1])
    allb = list(blocks)
    bys = [b[0] for b in allb]; bxs = [b[1] for b in allb]
    index = 3 * ((max(bys) - min(bys) + 1) * (max(bxs) - min(bxs) + 1) + 1)
    npl = sum(len(v) for v in blocks.values())
    return dict(data=total, index=index, blocks=len(blocks), polylines=npl, verts_in=nverts_in, verts_out=nverts_out)


from osm_lib import translit, merge_ways

res = {}
roads = d["roads"]
main = [r for r in roads if r["cls"] in ("motorway", "trunk", "primary")]
sec = [r for r in roads if r["cls"] == "secondary"]
print("ways:", len(main), "main,", len(sec), "secondary")
if "--merge" in sys.argv:
    main, sec = merge_ways(main), merge_ways(sec)
    print("after merging end-to-end:", len(main), "main,", len(sec), "secondary")
for tol in (25, 50, 100):
    for nm, ws in (("main", main), ("secondary", sec)):
        r = road_layer_size(ws, tol)
        res[f"roads_{nm}_{tol}"] = r
        print(f"roads {nm:9s} DP {tol:3d} m: data {r['data']/1e3:7.1f} kB + index {r['index']/1e3:.1f} kB = {(r['data']+r['index'])/1e3:7.1f} kB "
              f"({r['polylines']} polylines in {r['blocks']} blocks, vertices {r['verts_in']} -> {r['verts_out']})")
    # per class split for main
    for c in ("motorway", "trunk", "primary"):
        r = road_layer_size([w for w in main if w["cls"] == c], tol)
        print(f"     {c:9s} {(r['data']+r['index'])/1e3:7.1f} kB")

# places
pl = [p for p in d["places"] if p["name"] and in_roi(np.array([p["lat"]]), np.array([p["lon"]]))[0]]
cities = [p for p in pl if p["kind"] == "city"]
print(f"places in region (10 km buffer): {len(pl)} ({len(cities)} city, {len(pl)-len(cities)} town); with population tag: {sum(1 for p in pl if p['pop'])}")
sz_utf8 = sz_1b = sz_lat = 0
for p in pl:
    cyr = p["name"]
    lat = p["name_en"] if p["name_en"] else translit(cyr)
    fixed = 2 + 2 + 1 + 1      # x u16, y u16, class/pop byte, name length byte
    sz_utf8 += fixed + len(cyr.encode("utf-8"))
    sz_1b += fixed + len(cyr)
    sz_lat += fixed + len(lat.encode("utf-8"))
print(f"places bytes (no index, linear list): Cyrillic UTF-8 {sz_utf8}, Cyrillic 1-byte code {sz_1b}, Latin {sz_lat}; "
      f"both UTF-8+Latin {sz_utf8 + sz_lat - 6*len(pl)}")
print("avg name chars", np.mean([len(p["name"]) for p in pl]), "latin", np.mean([len(translit(p["name"])) for p in pl]))
print("largest:", [(p["name"], translit(p["name"]), p["pop"]) for p in sorted(pl, key=lambda p: -p["pop"])[:6]])
res["places"] = dict(n=len(pl), cyr_utf8=sz_utf8, cyr_1byte=sz_1b, latin=sz_lat)
json.dump(res, open(f"{tlib.DATA}/osm_sizes.json", "w"), indent=1, default=int)
