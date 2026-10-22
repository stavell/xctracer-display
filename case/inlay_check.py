"""Minimum feature width of the three inlays (needs shapely + trimesh). python inlay_check.py [min_width=0.6]
Each inlay is sliced at mid height; a feature thinner than w disappears when the shape is eroded by w/2 and grown back.
Sharp letter corners always lose a few hundredths of a mm2 (the disc does not reach into the tip); a stroke thinner
than w loses a strip. Exits 1 when one lost piece is larger than 0.1 mm2."""
import sys
import trimesh
from functools import reduce
from shapely.geometry import Polygon

w_min = float(sys.argv[1]) if len(sys.argv) > 1 else 0.6
bad = 0
for name in ("case_shell_inlay.stl", "case_lid_inlay.stl", "case_buttons_inlay.stl"):
    m = trimesh.load(name)
    if not m.is_watertight:
        print(f"{name}: NOT WATERTIGHT"); bad += 1
    sec = m.section(plane_origin=[0, 0, 0.2], plane_normal=[0, 0, 1])
    polys = [Polygon(p[:, :2]).buffer(0) for p in sec.discrete]     # outlines and holes: even-odd fill
    shape = reduce(lambda a, b: a.symmetric_difference(b), polys)
    print(f"{name}: islands {len(polys)}; area {shape.area:.1f} mm2; thickness {m.bounds[1][2] - m.bounds[0][2]:.2f} mm")
    for w in (0.5, 0.6, 0.7, 0.8, 1.0):
        o = shape.buffer(-w / 2 + 1e-3).buffer(w / 2 - 1e-3)
        d = shape.difference(o)
        pieces = getattr(d, "geoms", [d])
        big = max((p.area for p in pieces), default=0.0)
        print(f"  thinner than {w:.1f} mm: {d.area:.3f} mm2 in {len(pieces)} pieces, largest {big:.3f}")
        if abs(w - w_min) < 1e-9 and big > 0.1:
            bad += 1; print("   <-- TOO THIN")
print("RESULT:", "FAIL" if bad else "PASS")
sys.exit(1 if bad else 0)
