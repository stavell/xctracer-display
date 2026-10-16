"""Hillshade renders: whole Bulgaria (grey + 1-bit) and zoomed 240x400 device-sized crops.
usage: render.py OUTDIR   (needs bg_dem.bin.ref.npz from mkterrain.py, native mosaic)"""
import sys, os, struct
import numpy as np
from PIL import Image
import tlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
binf = f"{tlib.DATA}/bg_dem.bin"
ref = np.load(binf + ".ref.npz")
f = open(binf, "rb").read()
lat0e7, lon0e7, dye7, dxe7 = struct.unpack_from("<iiii", f, 16)
Q = f[16 + 24]
S = 200
v = ref["v"].astype(np.float32) * Q
tm = np.kron(ref["tm"], np.ones((32, 32), bool))
H = np.where(tm, v, np.nan)
lat0, lon0, dy, dx = lat0e7 / 1e7, lon0e7 / 1e7, dye7 / 1e7, dxe7 / 1e7

BAYER = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38],
                  [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
                  [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]) / 64.0


def hillshade(h, cell_m, az=315.0, alt=40.0, zf=2.0):
    """Lambert shade 0..1 with vertical exaggeration zf (flatland = sin(alt))."""
    gy, gx = np.gradient(h, cell_m)       # rows grow southwards: north slope = -gy
    ex, ny = gx * zf, -gy * zf             # dz/d(east), dz/d(north)
    azr, altr = np.radians(az), np.radians(alt)
    lx, ly, lz = np.sin(azr) * np.cos(altr), np.cos(azr) * np.cos(altr), np.sin(altr)
    s = (-ex * lx - ny * ly + lz) / np.sqrt(ex * ex + ny * ny + 1.0)
    return np.clip(s, 0, 1)


def to_gray(s):
    return (np.nan_to_num(s, nan=1.0) * 255).astype(np.uint8)


def to_1bit(s):
    s = np.nan_to_num(s, nan=1.0)
    # push flat ground (sin(40deg)=0.64) towards white so only relief is dotted: much calmer than raw Bayer
    s = np.clip(0.93 + (s - 0.643) * 1.1, 0, 1)
    t =np.tile(BAYER, (s.shape[0] // 8 + 1, s.shape[1] // 8 + 1))[:s.shape[0], :s.shape[1]]
    return ((s > t) * 255).astype(np.uint8)


# 1) whole country at the stored resolution
sh = hillshade(np.nan_to_num(H, nan=0.0), S)
sh[~tm] = np.nan
Image.fromarray(to_gray(sh)).save(f"{out}/bulgaria_200m_gray.png")
Image.fromarray(to_1bit(sh)).save(f"{out}/bulgaria_200m_1bit.png")
print("whole country", sh.shape)


def crop(arr_fn, lat, lon, w=240, h=400, mpp=100.0):
    """Sample a function height(lat, lon) on a w x h pixel grid, mpp metres per pixel, centred on lat/lon."""
    ys = (np.arange(h) - h / 2) * mpp / tlib.M_PER_DEG
    xs = (np.arange(w) - w / 2) * mpp / tlib.M_PER_DEG / tlib.COS0
    LA = lat - ys[:, None] + 0 * xs[None, :]
    LO = lon + xs[None, :] + 0 * ys[:, None]
    return arr_fn(LA, LO)


def stored(LA, LO, Hs=np.nan_to_num(H, nan=0.0)):
    return tlib.bilinear(Hs, (lat0 - LA) / dy - 0.5, (LO - lon0) / dx - 0.5)


mos = tlib.mosaic()


def native(LA, LO):
    return tlib.sample_grid(mos, 0, LA, LO)


# track overlay
tr = tlib.read_igc("/Users/stavel/Downloads/2026-08-16-XCT-SVE-09.igc")
clat, clon = (tr["lat"].min() + tr["lat"].max()) / 2, (tr["lon"].min() + tr["lon"].max()) / 2
print("flight area centre", clat, clon, "extent km:", (tr["lat"].max() - tr["lat"].min()) * 111.2,
      (tr["lon"].max() - tr["lon"].min()) * 111.2 * tlib.COS0)


def panel(fn, lat, lon, mpp, track=True, zf=2.0, onebit=True):
    h = crop(fn, lat, lon, mpp=mpp)
    s = hillshade(h, mpp, zf=zf)
    img = to_1bit(s) if onebit else to_gray(s)
    im = Image.fromarray(img).convert("L")
    if track:
        a = np.array(im.convert("RGB"))
        px = ((tr["lon"] - lon) * tlib.M_PER_DEG * tlib.COS0 / mpp + 120).astype(int)
        py = (200 - (tr["lat"] - lat) * tlib.M_PER_DEG / mpp).astype(int)
        ok = (px >= 0) & (px < 240) & (py >= 0) & (py < 400)
        a[py[ok], px[ok]] = (255, 0, 0)
        return Image.fromarray(a)
    return im.convert("RGB")


def sheet(name, lat, lon, mpp, zf=2.0):
    ps = [panel(stored, lat, lon, mpp, False, zf, True), panel(stored, lat, lon, mpp, True, zf, False),
          panel(native, lat, lon, mpp, False, zf, False)]
    W = Image.new("RGB", (240 * 3 + 20, 400), (80, 80, 80))
    for i, p in enumerate(ps):
        W.paste(p, (i * 250, 0))
    W.save(f"{out}/{name}.png")


# 240x400 device crops: [stored 1-bit | stored grey + track | native 90 m grey]
sheet("zoom_flight_100mpp", clat, clon, 100.0)       # 24 x 40 km window
sheet("zoom_flight_200mpp", clat, clon, 200.0)       # 48 x 80 km window: 1 px = 1 cell
sheet("zoom_flight_50mpp", clat, clon, 50.0)         # 12 x 20 km (oversampled, shows interpolation artefacts)
# mountain area: Rila / Musala (42.18 N, 23.585 E)
sheet("zoom_rila_100mpp", 42.20, 23.55, 100.0)
# 1-bit only, enlarged x3 for viewing
p = panel(stored, clat, clon, 100.0, False, 2.0, True).resize((720, 1200), Image.NEAREST)
p.save(f"{out}/zoom_flight_100mpp_1bit_x3.png")
