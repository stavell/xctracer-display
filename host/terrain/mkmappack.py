"""Build the map pack (DEM mean + MAX layer, towns, main roads, attribution) for the device's QSPI flash.

Format: FORMAT.md. Reader: firmware/src/platform/terrain/. Usage: README.md ("Building a map pack").

  mkmappack.py OUT.pack [--bbox LAT_S LAT_N LON_W LON_E] [--no-roads] [--secondary] [--ref OUT.ref.txt]

Needs the study data (tlib.DATA: grid_*_*.npy, roi_*.npy, osm/bg.pkl) and the venv with numpy/numba/shapely.
--bbox restricts every section to a rectangle (test fixtures); --ref writes the reference values the C++ tests
compare against (independent Python decode of the quantised grids and the pre-encoding polylines).
"""
import argparse, math, os, pickle, struct, sys, zlib
import numpy as np
import tlib, codec
from osm_lib import translit, merge_ways

VERSION = 1
HDR = 192
NSEC_MAX = 8
SEC_MEAN, SEC_MAX, SEC_TOWNS, SEC_ROADS, SEC_STRINGS = 1, 2, 3, 4, 5
TILE = 32
UNIT_M = 10                        # vector coordinate unit, metres
BLK = 1024                         # vector block, units (10.24 km)
MAXPTS = 120                       # polyline vertices per record (reader buffer)
MAX_BLOCK_BYTES = 2048             # reader buffer for one road block
MAX_LATIN, MAX_CYR = 47, 95        # name bytes (reader buffers are +1 for the NUL)
ROAD_CLASS = {"motorway": 0, "trunk": 1, "primary": 2, "secondary": 3}
ATTRIBUTION = [
    "Contains modified Copernicus data (DEM GLO-90) (c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH "
    "2014-2018, provided under COPERNICUS by the European Union and ESA; all rights reserved.",
    "© OpenStreetMap contributors",
    "(c) OpenStreetMap contributors (ODbL)",
]


def crc32(b):
    return zlib.crc32(bytes(b)) & 0xFFFFFFFF


# ---------------------------------------------------------------- DEM layers
def layer_cells_mask(S, bbox):
    """Cell-centre mask of grid S inside bbox (all True without bbox)."""
    dy, dx, nr, nc = tlib.grid_geom(S)
    if not bbox:
        return np.ones((nr, nc), bool)
    s, n, w, e = bbox
    lat = tlib.LAT_N - (np.arange(nr) + 0.5) * dy
    lon = tlib.LON_W + (np.arange(nc) + 0.5) * dx
    return ((lat >= s) & (lat <= n))[:, None] & ((lon >= w) & (lon <= e))[None, :]


def build_layer(S, kind, q, buffer_km, bbox):
    g = tlib.resample(S, "average" if kind == 0 else "max")
    v = np.round(g / q) if kind == 0 else np.ceil(g / q)      # max layer rounds UP: never under-states
    v = np.clip(v, -32000, 32000).astype(np.int16)
    roi = tlib.roi_mask(S, buffer_km) & layer_cells_mask(S, bbox)
    tm = codec.tile_mask(roi, TILE)
    ys, xs = np.nonzero(tm)
    ty0, ty1, tx0, tx1 = ys.min(), ys.max(), xs.min(), xs.max()
    vp = codec.pad_to_tiles(v, TILE)
    vc = np.ascontiguousarray(vp[ty0 * TILE:(ty1 + 1) * TILE, tx0 * TILE:(tx1 + 1) * TILE])
    tmc = np.ascontiguousarray(tm[ty0:ty1 + 1, tx0:tx1 + 1])
    blob, offs = codec.layer_encode(vc, TILE, 2, tmc)          # codec 2 = MED + Rice(k), modes 0/1/3
    dy, dx, nr, nc = tlib.grid_geom(S)
    L = dict(S=S, kind=kind, q=q, vc=vc, tm=tmc, blob=blob.tobytes(), offs=offs, dy_e7=round(dy * 1e7),
             dx_e7=round(dx * 1e7), lat0_e7=round((tlib.LAT_N - ty0 * TILE * dy) * 1e7),
             lon0_e7=round((tlib.LON_W + tx0 * TILE * dx) * 1e7), rows=vc.shape[0], cols=vc.shape[1],
             ty=tmc.shape[0], tx=tmc.shape[1])
    return L


def layer_section(L):
    """desc(48) + u24 index + u32 tile CRCs + data; all offsets relative to the section start."""
    n = L["ty"] * L["tx"]
    idx = b"".join(int(o).to_bytes(3, "little") for o in L["offs"])
    assert L["offs"][-1] < (1 << 24)
    crcs = bytearray()
    for i in range(n):
        a, b = int(L["offs"][i]), int(L["offs"][i + 1])
        crcs += struct.pack("<I", crc32(L["blob"][a:b]) if b > a else 0)
    idx_off = 48
    crc_off = idx_off + len(idx)
    data_off = crc_off + len(crcs)
    desc = struct.pack("<iiiiHHHHBBBBIII8x", L["lat0_e7"], L["lon0_e7"], L["dy_e7"], L["dx_e7"], L["rows"], L["cols"],
                       L["ty"], L["tx"], L["q"], int(math.log2(TILE)), L["kind"], 3, idx_off, crc_off, data_off)
    assert len(desc) == 48
    return desc + idx + bytes(crcs) + L["blob"]


def ref_cell(L, r, c):
    """Quantised value or None (outside / tile not stored)."""
    if r < 0 or c < 0 or r >= L["rows"] or c >= L["cols"]:
        return None
    return int(L["vc"][r, c]) if L["tm"][r // TILE, c // TILE] else None


def ref_mean(L, lat_e7, lon_e7):
    """Bilinear in metres, float64, same cell geometry as the C++ reader; None when it must refuse."""
    rf = (L["lat0_e7"] - lat_e7) / L["dy_e7"] - 0.5
    cf = (lon_e7 - L["lon0_e7"]) / L["dx_e7"] - 0.5
    r, c = math.floor(rf), math.floor(cf)
    fr, fc = rf - r, cf - c
    v = [ref_cell(L, r, c), ref_cell(L, r, c + 1), ref_cell(L, r + 1, c), ref_cell(L, r + 1, c + 1)]
    if any(x is None for x in v):
        return None
    a, b, d, e = v
    return L["q"] * ((a * (1 - fc) + b * fc) * (1 - fr) + (d * (1 - fc) + e * fc) * fr)


def ref_cellmax(L, lat_e7, lon_e7):
    r = math.floor((L["lat0_e7"] - lat_e7) / L["dy_e7"])
    c = math.floor((lon_e7 - L["lon0_e7"]) / L["dx_e7"])
    v = ref_cell(L, r, c)
    return None if v is None else L["q"] * v


# ---------------------------------------------------------------- vectors
def to_units(lon, lat):
    """DEM projection in 10 m units: x east of 22E, y south of 45N."""
    x = (np.asarray(lon) - tlib.LON_W) * tlib.COS0 * tlib.M_PER_DEG / UNIT_M
    y = (tlib.LAT_N - np.asarray(lat)) * tlib.M_PER_DEG / UNIT_M
    return x, y


def varint(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return bytes(out)


def zvarint(n):
    n = int(n)
    return varint((n << 1) if n >= 0 else ((-n << 1) - 1))


def road_polylines(ways, tol, inside):
    """Merged ways -> simplified polylines in units, cut at region/bbox exits, then at block changes and MAXPTS."""
    from shapely.geometry import LineString
    pls = []
    for w in ways:
        lon = np.array([p[0] for p in w["pts"]]); lat = np.array([p[1] for p in w["pts"]])
        x, y = to_units(lon, lat)
        ok = inside(lat, lon)
        i, n = 0, len(x)
        while i < n:
            if not ok[i]:
                i += 1
                continue
            j = i
            while j < n and ok[j]:
                j += 1
            if j - i >= 2:
                ls = LineString(np.c_[x[i:j] * UNIT_M, y[i:j] * UNIT_M])
                s = ls.simplify(tol, preserve_topology=False)
                q = np.rint(np.array(s.coords) / UNIT_M).astype(int)
                q = q[np.r_[True, (np.diff(q, axis=0) != 0).any(axis=1)]]
                if len(q) >= 2:
                    pls.append((ROAD_CLASS[w["cls"]], q))
            i = j
    out = []
    for cls, q in pls:
        cur = [q[0]]
        bk = (q[0][1] // BLK, q[0][0] // BLK)
        pieces = []
        for p in q[1:]:
            cur.append(p)
            nb = (p[1] // BLK, p[0] // BLK)
            if nb != bk or len(cur) >= MAXPTS:       # the crossing segment stays with the block of its first vertex
                pieces.append((bk, cur))
                cur = [p]
                bk = nb
        if len(cur) >= 2:
            pieces.append((bk, cur))
        for bk, c in pieces:
            out.append((bk, cls, [(int(a), int(b)) for a, b in c]))
    return out


def roads_section(pls):
    blocks = {}
    for bk, cls, pts in pls:
        blocks.setdefault(bk, []).append((cls, pts))
    bys = [b[0] for b in blocks]; bxs = [b[1] for b in blocks]
    by0, bx0 = min(bys), min(bxs)
    nby, nbx = max(bys) - by0 + 1, max(bxs) - bx0 + 1
    assert max(bxs) * BLK < 65536 and max(bys) * BLK < 65536
    data = bytearray()
    offs = [0]
    biggest = 0
    for by in range(by0, by0 + nby):
        for bx in range(bx0, bx0 + nbx):
            lst = blocks.get((by, bx))
            if lst:
                b = bytearray(varint(len(lst)))
                for cls, pts in lst:
                    b += varint(len(pts)) + bytes([cls])
                    ox, oy = bx * BLK, by * BLK
                    b += zvarint(pts[0][0] - ox) + zvarint(pts[0][1] - oy)
                    for a, c in zip(pts[:-1], pts[1:]):
                        b += zvarint(c[0] - a[0]) + zvarint(c[1] - a[1])
                assert len(b) <= MAX_BLOCK_BYTES, len(b)
                biggest = max(biggest, len(b))
                data += b
            offs.append(len(data))
    idx = b"".join(o.to_bytes(3, "little") for o in offs)
    hdr_len = 24
    npoly = sum(len(v) for v in blocks.values())
    hdr = struct.pack("<HHHHHHIII", BLK, bx0, by0, nbx, nby, MAXPTS, hdr_len, hdr_len + len(idx), npoly)
    assert len(hdr) == hdr_len
    return hdr + idx + bytes(data), dict(blocks=len(blocks), polylines=npoly, biggest=biggest, index=len(idx),
                                         data=len(data))


POP_EDGES = [5000, 10000, 20000, 50000, 100000, 250000]    # bucket 1..7 = 1 + edges <= pop; 0 = unknown


def pop_bucket(pop):
    return 0 if pop <= 0 else 1 + sum(1 for e in POP_EDGES if pop >= e)


def towns_section(places):
    places = sorted(places, key=lambda p: (-p["pop"], p["name"]))
    recs = bytearray()
    names = bytearray()
    out = []
    for p in places:
        cyr = p["name"].encode("utf-8")
        lat = (p["name_en"] or translit(p["name"])).encode("utf-8")
        assert len(lat) <= MAX_LATIN and len(cyr) <= MAX_CYR, p
        x, y = to_units(p["lon"], p["lat"])
        x, y = int(round(float(x))), int(round(float(y)))
        cls = pop_bucket(p["pop"]) | (8 if p["kind"] == "city" else 0)
        recs += struct.pack("<HHBBH", x, y, cls, 0, len(names))
        names += bytes([len(lat), len(cyr)]) + lat + cyr
        out.append(dict(x=x, y=y, cls=cls, lat=lat.decode(), cyr=p["name"], pop=p["pop"]))
    names_off = 4 + len(recs)
    assert names_off + len(names) < 65536
    return struct.pack("<HH", len(places), names_off) + bytes(recs) + bytes(names), out


def strings_section():
    b = bytes([len(ATTRIBUTION)])
    for s in ATTRIBUTION:
        b += s.encode("utf-8") + b"\0"
    return b


# ---------------------------------------------------------------- file
def build(out, bbox, buffer_km, tol, secondary, roads_on, ref):
    mean = build_layer(200, 0, 4, buffer_km, bbox)
    mx = build_layer(500, 1, 10, buffer_km, bbox)
    secs = [(SEC_MEAN, layer_section(mean)), (SEC_MAX, layer_section(mx))]
    d = pickle.load(open(f"{tlib.DATA}/osm/bg.pkl", "rb"))
    roi5 = tlib.roi_mask(500, buffer_km)
    dy5, dx5, _, _ = tlib.grid_geom(500)

    def inside(lat, lon):
        r = np.clip(((tlib.LAT_N - lat) / dy5).astype(int), 0, roi5.shape[0] - 1)
        c = np.clip(((lon - tlib.LON_W) / dx5).astype(int), 0, roi5.shape[1] - 1)
        ok = roi5[r, c]
        if bbox:
            ok = ok & (lat >= bbox[0]) & (lat <= bbox[1]) & (lon >= bbox[2]) & (lon <= bbox[3])
        return ok

    pl = [p for p in d["places"] if p["name"] and inside(np.array([p["lat"]]), np.array([p["lon"]]))[0]]
    tsec, tinfo = towns_section(pl)
    secs.append((SEC_TOWNS, tsec))
    pls, rinfo = [], None
    if roads_on:
        classes = ("motorway", "trunk", "primary") + (("secondary",) if secondary else ())
        ways = merge_ways([r for r in d["roads"] if r["cls"] in classes])
        pls = road_polylines(ways, tol, inside)
        rsec, rinfo = roads_section(pls)
        secs.append((SEC_ROADS, rsec))
    secs.append((SEC_STRINGS, strings_section()))

    pos = HDR
    table = []
    body = bytearray()
    for sid, b in secs:
        table.append(struct.pack("<HHIII", sid, 0, pos, len(b), crc32(b)))
        body += b
        pos += len(b)
    nl, ml = mean, mean
    # coverage = the mean layer's stored grid
    lat_n, lon_w = mean["lat0_e7"], mean["lon0_e7"]
    lat_s = lat_n - mean["rows"] * mean["dy_e7"]
    lon_e = lon_w + mean["cols"] * mean["dx_e7"]
    cos0_f32 = struct.unpack("<I", struct.pack("<f", tlib.COS0))[0]
    h = bytearray(HDR)
    struct.pack_into("<4sHHII", h, 0, b"BGMP", VERSION, HDR, pos, 0)
    struct.pack_into("<iiii", h, 16, lat_s, lat_n, lon_w, lon_e)
    struct.pack_into("<iiIIHH", h, 32, round(tlib.LAT_N * 1e7), round(tlib.LON_W * 1e7), int(tlib.M_PER_DEG), cos0_f32,
                     UNIT_M, len(secs))
    for i, t in enumerate(table):
        h[64 + 16 * i:64 + 16 * i + 16] = t
    struct.pack_into("<I", h, 52, crc32(h))                    # header CRC (file CRC and itself still zero)
    blob = bytearray(h) + body
    struct.pack_into("<I", blob, 12, crc32(blob))              # file CRC with its own field zero
    open(out, "wb").write(blob)

    print(f"{out}: {len(blob)} bytes ({len(blob) / 1048576 * 100 / 2:.1f} % of 2 MiB), header {HDR}")
    names = {SEC_MEAN: "DEM mean 200 m", SEC_MAX: "DEM max 500 m", SEC_TOWNS: "towns", SEC_ROADS: "roads",
             SEC_STRINGS: "strings"}
    for sid, b in secs:
        print(f"  section {sid} {names[sid]:15s} {len(b):9d} B")
    for nm, L in (("mean", mean), ("max", mx)):
        n = L["ty"] * L["tx"]
        print(f"  {nm}: grid {L['rows']}x{L['cols']}, tiles {L['ty']}x{L['tx']} ({int(L['tm'].sum())} coded), "
              f"index {3 * (n + 1)} B, tile CRCs {4 * n} B, data {len(L['blob'])} B")
    print(f"  towns: {len(tinfo)}")
    if rinfo:
        print(f"  roads: {rinfo}")
    if ref:
        write_ref(ref, mean, mx, tinfo, pls)
    return blob


def write_ref(path, mean, mx, tinfo, pls):
    rng = np.random.default_rng(7)
    lines = []
    for _ in range(400):
        la = mean["lat0_e7"] - rng.uniform(-0.05, 1.05) * mean["rows"] * mean["dy_e7"]
        lo = mean["lon0_e7"] + rng.uniform(-0.05, 1.05) * mean["cols"] * mean["dx_e7"]
        la, lo = int(la), int(lo)
        m = ref_mean(mean, la, lo)
        c = ref_cellmax(mx, la, lo)
        lines.append(f"E {la} {lo} {'nan' if m is None else f'{m:.5f}'} {'nan' if c is None else f'{c:.1f}'}")
    for t in tinfo:
        lines.append(f"T {t['x']} {t['y']} {t['cls']} {t['pop']} {t['lat']}|{t['cyr']}")
    for bk, cls, pts in pls:
        lines.append("P %d %d %s" % (cls, len(pts), " ".join(f"{a} {b}" for a, b in pts)))
    open(path, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--bbox", nargs=4, type=float, metavar=("LAT_S", "LAT_N", "LON_W", "LON_E"))
    ap.add_argument("--buffer", type=int, default=10, help="km around the border (only used for the 10 km study ROI)")
    ap.add_argument("--dp", type=float, default=50, help="Douglas-Peucker tolerance for roads, metres")
    ap.add_argument("--secondary", action="store_true", help="also store secondary roads (class 3)")
    ap.add_argument("--no-roads", action="store_true")
    ap.add_argument("--ref", help="write reference values for the C++ tests")
    a = ap.parse_args()
    build(a.out, a.bbox, a.buffer, a.dp, a.secondary, not a.no_roads, a.ref)
