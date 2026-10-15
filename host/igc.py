"""IGC B-record reader and XCTRC line builder (replay support).
Only B records are used; header lines (pilot, glider) are ignored."""
import math
import re
from dataclasses import dataclass

R_EARTH = 6371000.0


@dataclass
class Fix:
    t: int            # seconds since midnight UTC
    lat: float
    lon: float
    palt: float       # pressure altitude, m
    galt: float       # GPS altitude, m
    vario: float = 0.0   # m/s, derived
    speed: float = 0.0   # km/h, derived
    course: float = 0.0  # degrees, derived


def read_igc(path):
    date = (16, 8, 26)
    ext = {}  # name -> (start, end) 1-based inclusive
    fixes = []
    for line in open(path, errors="replace"):
        line = line.rstrip("\r\n")
        if line.startswith("HFDTE"):
            m = re.search(r"(\d\d)(\d\d)(\d\d)", line)
            if m:
                date = tuple(int(x) for x in m.groups())
        elif line.startswith("I") and len(line) > 3:
            n = int(line[1:3])
            for i in range(n):
                s = line[3 + 7 * i: 10 + 7 * i]
                ext[s[4:]] = (int(s[0:2]), int(s[2:4]))
        elif line.startswith("B") and len(line) >= 35 and line[24] == "A":
            t = int(line[1:3]) * 3600 + int(line[3:5]) * 60 + int(line[5:7])
            lat = int(line[7:9]) + (int(line[9:14]) / 1000.0) / 60.0
            if "LAD" in ext:
                a, b = ext["LAD"]
                lat += int(line[a - 1:b]) / 1e4 / 60.0
            if line[14] == "S":
                lat = -lat
            lon = int(line[15:18]) + (int(line[18:23]) / 1000.0) / 60.0
            if "LOD" in ext:
                a, b = ext["LOD"]
                lon += int(line[a - 1:b]) / 1e4 / 60.0
            if line[23] == "W":
                lon = -lon
            fixes.append(Fix(t, lat, lon, float(line[25:30]), float(line[30:35])))
    derive(fixes)
    return date, fixes


def _bearing(a, b):
    p1, p2 = math.radians(a.lat), math.radians(b.lat)
    dl = math.radians(b.lon - a.lon)
    y = math.sin(dl) * math.cos(p2)
    x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return (math.degrees(math.atan2(y, x)) + 360) % 360


def _dist(a, b):
    p1, p2 = math.radians(a.lat), math.radians(b.lat)
    dp, dl = p2 - p1, math.radians(b.lon - a.lon)
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * R_EARTH * math.asin(math.sqrt(h))


def derive(fx):
    n = len(fx)
    for i, f in enumerate(fx):
        # vario: least-squares slope of pressure altitude over +-2 s (integer metres are noisy)
        lo, hi = max(0, i - 2), min(n - 1, i + 2)
        pts = [(fx[j].t - f.t, fx[j].palt) for j in range(lo, hi + 1)]
        mt = sum(p[0] for p in pts) / len(pts)
        ma = sum(p[1] for p in pts) / len(pts)
        den = sum((p[0] - mt) ** 2 for p in pts)
        f.vario = sum((p[0] - mt) * (p[1] - ma) for p in pts) / den if den else 0.0
        # speed and course from positions +-1 s
        a, b = fx[max(0, i - 1)], fx[min(n - 1, i + 1)]
        dt = b.t - a.t
        if dt > 0:
            f.speed = _dist(a, b) / dt * 3.6
            if f.speed > 2:
                f.course = _bearing(a, b)
            elif i:
                f.course = fx[i - 1].course


def xctrc_line(date, f, batt=85):
    y, m, d = 2000 + date[2], date[1], date[0]
    hh, mm, ss = f.t // 3600, (f.t // 60) % 60, f.t % 60
    pressure = 1013.25 * (1 - f.palt / 44330.0) ** 5.255
    body = (f"XCTRC,{y},{m},{d},{hh},{mm},{ss},0,{f.lat:.6f},{f.lon:.6f},{f.palt:.2f},"
            f"{f.speed:.2f},{f.course:.1f},{f.vario:.2f},,,,{pressure:.2f},{batt}")
    cs = 0
    for ch in body:
        cs ^= ord(ch)
    return f"${body}*{cs:02x}"
