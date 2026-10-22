"""Printability check of the exported STL files (run case.py first): python printcheck.py [part ...]

Bambu A1, PETG, 0.4 nozzle, 0.2 layers, NO supports. Every part is meshed in its print orientation (the STL files already are):
  shell + test plate + seam test: front face down, deck: ceiling face down, lid: outer face down, caps: head down.

Overhangs: every face that looks downward and is flatter than 45 deg from the horizontal (steeper than 45 deg from vertical, tolerance
46 deg for the tessellation), is not on the bed (z > 0.05), is collected into regions (connected faces). For each region the sample
points on its faces are compared with the part's cross-section one layer (0.15 mm) below: the overhang is the largest horizontal
distance from a point to the material below it. PASS if every region overhangs <= 0.4 mm (one line width, it leans on the perimeter below), OR it is a bridge: every unsupported point lies
on a straight chord (x, y or a diagonal) of the unsupported area whose both ends rest on material, and the chord is <= 10 mm.

Flexing features: the strain table from case.py (limit across the layers 1.0 %, in the layer plane 2.0 %).
Exits 1 on any failure."""
import sys, os
import numpy as np
import trimesh
import shapely
from functools import reduce
from shapely.geometry import Polygon, Point, LineString, MultiPoint
from shapely.ops import unary_union

OVERHANG_MAX, BRIDGE_MAX, ANGLE_MAX = 0.4, 10.0, 46.0
TOL = 0.02       # mm, mm, deg from vertical
BED_Z, LAYER_BELOW = 0.05, 0.15
PARTS = {"shell": "case_shell.stl", "lid": "case_lid.stl", "deck": "case_deck.stl", "caps": "case_buttons.stl",
         "testplate": "case_testplate.stl", "seamtest": "case_seamtest_shell.stl"}

class Slicer:
    def __init__(self, mesh):
        self.m, self.cache = mesh, {}
    def section(self, z):
        z = round(z / 0.05) * 0.05
        if z not in self.cache:
            sec = self.m.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
            if sec is None:
                self.cache[z] = Polygon()
            else:
                polys = [Polygon(p[:, :2]).buffer(0) for p in sec.discrete if len(p) > 3]
                self.cache[z] = reduce(lambda a, b: a.symmetric_difference(b), polys, Polygon())   # even-odd: holes are holes
        return self.cache[z]

def sample_triangles(tris, pitch=0.3, cap=40, seed=1):
    rng = np.random.default_rng(seed)
    pts = [tris.reshape(-1, 3)]
    for t in tris:
        area = 0.5 * np.linalg.norm(np.cross(t[1] - t[0], t[2] - t[0]))
        n = min(cap, int(area / (pitch * pitch)))
        if n:
            r = rng.random((n, 2)); flip = r.sum(1) > 1; r[flip] = 1 - r[flip]
            pts.append(t[0] + r[:, :1] * (t[1] - t[0]) + r[:, 1:] * (t[2] - t[0]))
    return np.concatenate(pts)

def chord(unsup, support, p, theta):
    """chord of the unsupported area through p along theta: (length, both ends rest on material)"""
    d = np.array([np.cos(theta), np.sin(theta)])
    line = LineString([p - 80 * d, p + 80 * d])
    seg = line.intersection(unsup)
    parts = [seg] if seg.geom_type == "LineString" else [g for g in getattr(seg, "geoms", []) if g.geom_type == "LineString" and not g.is_empty]
    parts = [g for g in parts if not g.is_empty]
    if not parts:
        return np.inf, False
    g = min(parts, key=lambda s: s.distance(Point(p)))
    a, b = (np.array(c) for c in (g.coords[0], g.coords[-1]))
    ok = support.distance(Point(a)) < 0.35 and support.distance(Point(b)) < 0.35
    return g.length, ok

def bridge_span(unsup, support, pts):
    """largest over the points of the shortest both-ends-supported chord (inf when some point has none)"""
    worst = 0.0
    for p in pts:
        best = np.inf
        for theta in (0, np.pi / 2, np.pi / 4, 3 * np.pi / 4):
            L, ok = chord(unsup, support, p[:2], theta)
            if ok: best = min(best, L)
        worst = max(worst, best)
    return worst

def check_part(name, path, verbose=True):
    m = trimesh.load(path)
    sl = Slicer(m)
    tri = m.triangles; nrm = m.face_normals
    flag = (nrm[:, 2] < -np.sin(np.radians(ANGLE_MAX))) & (tri[:, :, 2].max(1) > BED_Z) & (m.area_faces > 1e-4)
    # faces entirely on the bed plane are not overhangs
    flag &= ~(tri[:, :, 2].max(1) <= BED_Z)
    idx = np.flatnonzero(flag)
    out = []
    if len(idx) == 0:
        return m, out
    # connected regions of flagged faces
    adj = m.face_adjacency
    sel = np.zeros(len(m.faces), bool); sel[idx] = True
    keep = sel[adj[:, 0]] & sel[adj[:, 1]]
    import scipy.sparse as sp, scipy.sparse.csgraph as cg
    n = len(m.faces)
    g = sp.coo_matrix((np.ones(keep.sum()), (adj[keep, 0], adj[keep, 1])), shape=(n, n))
    _, lab = cg.connected_components(g, directed=False)
    for l in np.unique(lab[idx]):
        faces = idx[lab[idx] == l]
        tris = tri[faces]
        pts = sample_triangles(tris)
        # support: cross-section one layer below each point
        zs = np.round((pts[:, 2] - LAYER_BELOW) / 0.1) * 0.1
        dist = np.zeros(len(pts))
        for z in np.unique(zs):
            mask = zs == z
            if z < BED_Z:                       # first layer: sits on the bed
                continue
            sup = sl.section(z)
            if sup.is_empty:
                dist[mask] = 1e3
            else:
                dist[mask] = shapely.distance(sup, shapely.points(pts[mask][:, :2]))
        ov = float(dist.max())
        area = float(m.area_faces[faces].sum())
        ang = float(np.degrees(np.arcsin(np.clip(-nrm[faces, 2], -1, 1))).max())   # from vertical: 90 = flat roof
        span = None
        ok = ov <= OVERHANG_MAX + TOL
        if not ok:
            z0 = float(pts[:, 2].min())
            support = sl.section(z0 - LAYER_BELOW)
            foot = unary_union([Polygon(t[:, :2]).buffer(0.01) for t in tris if Polygon(t[:, :2]).area > 1e-9])
            unsup = foot.difference(support.buffer(0.02))
            far = np.argsort(-dist)[:80]
            span = bridge_span(unsup, support, pts[far])
            ok = span <= BRIDGE_MAX
        c = pts.mean(0)
        inlay = bool(pts[:, 2].min() > 0.35 and pts[:, 2].max() < 0.55 and area > 1.0)     # roof of an inlay pocket: filled by the 2nd colour
        if inlay:
            ok = True
        out.append(dict(part=name, centre=c, z0=float(pts[:, 2].min()), z1=float(pts[:, 2].max()), area=area, angle=ang, overhang=ov, bridge=span, ok=ok, inlay=inlay))
    return m, out

def main(argv):
    names = argv or list(PARTS)
    here = os.path.dirname(os.path.abspath(__file__))
    bad = 0
    print("=== overhangs (faces steeper than 45 deg from vertical, off the bed) ===")
    for nm in names:
        m, regs = check_part(nm, os.path.join(here, PARTS[nm]))
        print(f"{nm:10s} {PARTS[nm]:26s} bbox {np.round(m.bounds[1] - m.bounds[0], 1)}  watertight {m.is_watertight}  flagged regions {len(regs)}")
        for r in sorted(regs, key=lambda r: -r["overhang"]):
            c = r["centre"]
            verdict = "ok " if r["ok"] else "FAIL"
            br = "inlay pocket (filled)" if r["inlay"] else f"bridge {r['bridge']:5.1f} mm" if r["bridge"] is not None else "               "
            print(f"   {verdict} at ({c[0]:7.1f},{c[1]:7.1f},{c[2]:6.1f})  z {r['z0']:5.2f}..{r['z1']:5.2f}  area {r['area']:7.2f} mm2  "
                  f"angle {r['angle']:4.1f}  overhang {r['overhang']:5.2f} mm  {br}")
            bad += not r["ok"]
    print("=== flexing features (case.py FLEX) ===")
    sys.path.insert(0, here)
    from case import FLEX
    for f in FLEX:
        flag = f["strain"] <= f["limit"]
        bad += not flag
        print(f"   {'ok ' if flag else 'FAIL'} {f['part']:6s} {f['feature']:92s} {f['layer']:14s} strain {f['strain']:5.2f} % (limit {f['limit']} %)")
    print("RESULT:", "FAIL" if bad else "PASS")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
