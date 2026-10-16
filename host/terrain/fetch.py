"""Download Copernicus GLO-90 tiles (AWS open data). Usage: fetch.py DATADIR"""
import sys, os, urllib.request
from concurrent.futures import ThreadPoolExecutor

d = sys.argv[1]
os.makedirs(d, exist_ok=True)


def get(t):
    la, lo = t
    n = f"Copernicus_DSM_COG_30_N{la:02d}_00_E{lo:03d}_00_DEM"
    p = f"{d}/{n}.tif"
    if os.path.exists(p):
        return n, "have"
    try:
        urllib.request.urlretrieve(f"https://copernicus-dem-90m.s3.amazonaws.com/{n}/{n}.tif", p)
        return n, "ok"
    except Exception as e:
        return n, f"FAIL {e}"


with ThreadPoolExecutor(8) as ex:
    for r in ex.map(get, [(a, b) for a in range(41, 45) for b in range(22, 29)]):
        print(*r)
