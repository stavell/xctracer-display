"""240x400 1-bit map mock-ups: dithered hillshade (from the stored 200 m grid) + main roads + town names.
usage: osm_render.py bg.pkl OUTDIR"""
import pickle, struct, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from shapely.geometry import LineString
import tlib
from osm_lib import translit, merge_ways

d = pickle.load(open(sys.argv[1], "rb"))
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
roads = merge_ways([r for r in d["roads"] if r["cls"] in ("motorway", "trunk", "primary")])
places = d["places"]

binf = f"{tlib.DATA}/bg_dem.bin"
ref = np.load(binf + ".ref.npz")
f = open(binf, "rb").read()
lat0e7, lon0e7, dye7, dxe7 = struct.unpack_from("<iiii", f, 16)
Q = f[16 + 24]
H = ref["v"].astype(np.float32) * Q
lat0, lon0, dy, dx = lat0e7 / 1e7, lon0e7 / 1e7, dye7 / 1e7, dxe7 / 1e7
BAYER = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38],
                  [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
                  [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]) / 64.0
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"


def shade(h, cell_m, zf=2.0):
    gy, gx = np.gradient(h, cell_m)
    ex, ny = gx * zf, -gy * zf
    az, alt = np.radians(315), np.radians(40)
    lx, ly, lz = np.sin(az) * np.cos(alt), np.cos(az) * np.cos(alt), np.sin(alt)
    return np.clip((-ex * lx - ny * ly + lz) / np.sqrt(ex * ex + ny * ny + 1), 0, 1)


def render(lat, lon, mpp, latin, name, W=240, Hh=400):
    ys = (np.arange(Hh) - Hh / 2 + 0.5) * mpp / tlib.M_PER_DEG
    xs = (np.arange(W) - W / 2 + 0.5) * mpp / tlib.M_PER_DEG / tlib.COS0
    LA = lat - ys[:, None] + 0 * xs[None, :]
    LO = lon + xs[None, :] + 0 * ys[:, None]
    h = tlib.bilinear(H, (lat0 - LA) / dy - 0.5, (LO - lon0) / dx - 0.5)
    s = np.clip(0.93 + (shade(h, mpp) - 0.643) * 1.1, 0, 1)
    t = np.tile(BAYER, (Hh // 8 + 1, W // 8 + 1))[:Hh, :W]
    im = Image.fromarray(((s > t) * 255).astype(np.uint8), "L")
    dr = ImageDraw.Draw(im)

    def px(lo_, la_):
        return ((np.asarray(lo_) - lon) * tlib.M_PER_DEG * tlib.COS0 / mpp + W / 2,
                (lat - np.asarray(la_)) * tlib.M_PER_DEG / mpp + Hh / 2)

    # roads: white casing under black line; width by class
    wid = {"motorway": 3, "trunk": 2, "primary": 1}
    for casing in (True, False):
        for r in roads:
            pts = r["pts"]
            lo_a = np.array([p[0] for p in pts]); la_a = np.array([p[1] for p in pts])
            if lo_a.max() < lon - 0.5 or lo_a.min() > lon + 0.5 or la_a.max() < lat - 0.5 or la_a.min() > lat + 0.5:
                continue
            x, y = px(lo_a, la_a)
            if x.max() < -5 or x.min() > W + 5 or y.max() < -5 or y.min() > Hh + 5:
                continue
            ls = LineString(np.c_[x, y]).simplify(0.7)      # simplify to pixel scale like the device would
            xy = [tuple(c) for c in ls.coords]
            w = wid[r["cls"]]
            dr.line(xy, fill=255 if casing else 0, width=w + 2 if casing else w)
    # places
    for p in sorted(places, key=lambda p: p["pop"]):
        x, y = px(p["lon"], p["lat"])
        if not (8 <= x < W - 8 and 8 <= y < Hh - 8):
            continue
        big = p["kind"] == "city" or p["pop"] > 50000
        r_ = 4 if big else 2
        dr.ellipse([x - r_ - 1, y - r_ - 1, x + r_ + 1, y + r_ + 1], fill=255)
        dr.ellipse([x - r_, y - r_, x + r_, y + r_], fill=0 if big else 255, outline=0)
        txt = translit(p["name"]) if latin else p["name"]
        font = ImageFont.truetype(FONT, 14 if big else 11)
        font.set_variation_by_axes if False else None
        tx, ty = x + r_ + 3, y - 7
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                dr.text((tx + ox, ty + oy), txt, fill=255, font=font, fontmode="1")
        dr.text((tx, ty), txt, fill=0, font=font, fontmode="1")
    return im.convert("1")


views = [("sofia_100mpp", 42.69, 23.32, 100.0), ("sofia_200mpp", 42.60, 23.40, 200.0), ("plovdiv_300mpp", 42.15, 24.75, 300.0),
         ("shumen_200mpp", 43.30, 26.80, 200.0)]
for nm, la, lo, mpp in views:
    a = render(la, lo, mpp, False, nm)
    b = render(la, lo, mpp, True, nm)
    sheet = Image.new("L", (240 * 2 + 10, 400), 128)
    sheet.paste(a.convert("L"), (0, 0)); sheet.paste(b.convert("L"), (250, 0))
    sheet.save(f"{out}/map_{nm}.png")
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(f"{out}/map_{nm}_x2.png")
print("done")
