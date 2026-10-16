"""Minimum feature width of the inlay (needs shapely: pip install shapely). python inlay_check.py [min_width=0.6]"""
import sys
import trimesh
from shapely.ops import unary_union

w_min = float(sys.argv[1]) if len(sys.argv) > 1 else 0.6
m = trimesh.load("case_shell_inlay.stl")
sec = m.section(plane_origin=[0, 0, 0.2], plane_normal=[0, 0, 1])
polys = sec.to_planar()[0].polygons_full
shape = unary_union(polys)
print(f"inlay islands: {len(polys)}; area {shape.area:.1f} mm2")
# a feature thinner than w disappears when eroded by w/2 and grown back
opened = shape.buffer(-w_min / 2 + 1e-3).buffer(w_min / 2 - 1e-3)
lost = shape.difference(opened)
print(f"area lost by opening with {w_min} mm: {lost.area:.3f} mm2 ({100 * lost.area / shape.area:.2f} %)")
for w in (0.5, 0.6, 0.7, 0.8, 0.9, 1.0):
    o = shape.buffer(-w / 2 + 1e-3).buffer(w / 2 - 1e-3)
    print(f"  features thinner than {w:.1f} mm: {shape.difference(o).area:.3f} mm2")
