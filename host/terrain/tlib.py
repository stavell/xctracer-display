"""Shared helpers for the terrain study: mosaic, grids, border mask, IGC reader, bilinear sampling.

Grid model (what the device would use): regular lat/lon grid, north-up, origin (LAT0, LON0) = (45.0 N, 22.0 E),
lat step dy = S/111195 deg, lon step dx = dy/cos(42.7 deg), so cells are ~S x S metres at 42.7 N.
Index on the device: row = (LAT0 - lat)/dy, col = (lon - LON0)/dx -- two multiplies, no trig. Cell value =
mean (or max) over the cell; samples sit at cell centres.
"""
import glob, json, math, os
import numpy as np
import rasterio
from rasterio.transform import Affine
from rasterio.warp import reproject, Resampling

DATA = "/private/tmp/claude-501/-Users-stavel-dev-display/da365ad1-40f2-499d-917a-3a6befca5ade/scratchpad/terrain"
LAT0, LON0 = 45.0, 22.0
LAT_S, LON_W = 41.0, 22.0
LAT_N, LON_E = 45.0, 29.0
COS0 = math.cos(math.radians(42.7))
M_PER_DEG = 111195.0


def mosaic():
    p = f"{DATA}/mosaic.npy"
    if os.path.exists(p):
        return np.load(p, mmap_mode="r")
    m = np.zeros((4800, 8400), np.float32)
    for la in range(41, 45):
        for lo in range(22, 29):
            with rasterio.open(f"{DATA}/raw/Copernicus_DSM_COG_30_N{la:02d}_00_E{lo:03d}_00_DEM.tif") as ds:
                a = ds.read(1)
            assert a.shape == (1200, 1200), a.shape
            r = (44 - la) * 1200
            c = (lo - 22) * 1200
            m[r:r + 1200, c:c + 1200] = a
    np.save(p, m)
    return m


NATIVE_T = Affine(1 / 1200, 0, LON_W, 0, -1 / 1200, LAT_N)


def grid_geom(S):
    dy = round(S / M_PER_DEG * 1e7) / 1e7  # integer 1e-7 deg steps so the device can use int32 maths
    dx = round(dy / COS0 * 1e7) / 1e7
    nr = int(round((LAT_N - LAT_S) / dy))
    nc = int(round((LON_E - LON_W) / dx))
    return dy, dx, nr, nc


def resample(S, mode="average"):
    """Resample the native mosaic onto the S-metre grid. mode: average | max."""
    p = f"{DATA}/grid_{S}_{mode}.npy"
    if os.path.exists(p):
        return np.load(p)
    dy, dx, nr, nc = grid_geom(S)
    dst = np.zeros((nr, nc), np.float32)
    t = Affine(dx, 0, LON_W, 0, -dy, LAT_N)
    rs = Resampling.average if mode == "average" else Resampling.max
    reproject(np.asarray(mosaic()), dst, src_transform=NATIVE_T, src_crs="EPSG:4326",
              dst_transform=t, dst_crs="EPSG:4326", resampling=rs)
    np.save(p, dst)
    return dst


def border_polys():
    d = json.load(open(f"{DATA}/raw/border.json"))
    from shapely.geometry import shape
    for f in d["features"]:
        if f["properties"]["name"] == "Bulgaria":
            return shape(f["geometry"])
    raise RuntimeError("no Bulgaria")


def roi_mask(S, buffer_km):
    """Boolean cell mask (grid S) of cells whose centre is within buffer_km of Bulgaria's land border."""
    p = f"{DATA}/roi_{S}_{buffer_km}.npy"
    if os.path.exists(p):
        return np.load(p)
    from rasterio.features import rasterize
    from shapely.ops import transform
    poly = border_polys()
    dy, dx, nr, nc = grid_geom(S)
    if buffer_km:
        # buffer in a local metric frame: x = dlon*cos0, y = dlat (degrees), 1 deg = 111.195 km
        poly = transform(lambda x, y, z=None: (np.asarray(x) * COS0, np.asarray(y)), poly)
        poly = poly.buffer(buffer_km / 111.195)
        poly = transform(lambda x, y, z=None: (np.asarray(x) / COS0, np.asarray(y)), poly)
    t = Affine(dx, 0, LON_W, 0, -dy, LAT_N)
    m = rasterize([(poly, 1)], out_shape=(nr, nc), transform=t, fill=0, dtype=np.uint8, all_touched=True)
    m = m.astype(bool)
    np.save(p, m)
    return m


# ---------- IGC ----------
def read_igc(path):
    """Returns dict with t (s), lat, lon, palt, galt arrays (decimal degrees, metres)."""
    ext = {}
    rows = []
    for ln in open(path, errors="ignore"):
        if ln.startswith("I"):
            n = int(ln[1:3])
            for i in range(n):
                s = ln[3 + 7 * i:10 + 7 * i]
                ext[s[4:7]] = (int(s[0:2]) - 1, int(s[2:4]))
        if not ln.startswith("B"):
            continue
        t = int(ln[1:3]) * 3600 + int(ln[3:5]) * 60 + int(ln[5:7])
        la = int(ln[7:9]) + (int(ln[9:11]) + int(ln[11:14]) / 1000) / 60
        lo = int(ln[15:18]) + (int(ln[18:20]) + int(ln[20:23]) / 1000) / 60
        if "LAD" in ext:
            a, b = ext["LAD"]; la += int(ln[a:b + 1]) / 1e5 / 60
        if "LOD" in ext:
            a, b = ext["LOD"]; lo += int(ln[a:b + 1]) / 1e5 / 60
        rows.append((t, la, lo, int(ln[25:30]), int(ln[30:35])))
    a = np.array(rows, float)
    return dict(t=a[:, 0], lat=a[:, 1], lon=a[:, 2], palt=a[:, 3], galt=a[:, 4])


def bilinear(arr, row_f, col_f):
    """Bilinear sample at fractional cell-centre coordinates (row_f, col_f in cell-centre units)."""
    nr, nc = arr.shape
    r = np.clip(row_f, 0, nr - 1.001)
    c = np.clip(col_f, 0, nc - 1.001)
    r0 = r.astype(int); c0 = c.astype(int)
    fr = r - r0; fc = c - c0
    a = np.asarray(arr)
    return (a[r0, c0] * (1 - fr) * (1 - fc) + a[r0, c0 + 1] * (1 - fr) * fc +
            a[r0 + 1, c0] * fr * (1 - fc) + a[r0 + 1, c0 + 1] * fr * fc)


def sample_grid(arr, S, lat, lon):
    """Sample a grid of cell size S (S=0: native mosaic) at lat/lon, bilinear between cell centres."""
    if S == 0:
        dy = dx = 1 / 1200
    else:
        dy, dx, _, _ = grid_geom(S)
    row = (LAT_N - lat) / dy - 0.5
    col = (lon - LON_W) / dx - 0.5
    return bilinear(arr, row, col)


def native_slope_deg(lat, lon):
    """Slope (deg) of the native DEM at points (central differences over +-1 native cell)."""
    m = mosaic()
    h = lambda la, lo: sample_grid(m, 0, la, lo)
    e = 1 / 1200
    dzdx = (h(lat, lon + e) - h(lat, lon - e)) / (2 * e * M_PER_DEG * math.cos(math.radians(42.7)))
    dzdy = (h(lat + e, lon) - h(lat - e, lon)) / (2 * e * M_PER_DEG)
    return np.degrees(np.arctan(np.hypot(dzdx, dzdy)))
