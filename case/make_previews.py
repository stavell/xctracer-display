"""Regenerates the preview PNGs: python make_previews.py   (runs fitcheck first, because it needs the stand-in parts)"""
import tempfile, os
import trimesh
import fitcheck as f
import render
from case import *

tmp = tempfile.mkdtemp()
def mesh(shape, name):
    p = os.path.join(tmp, name)
    cq.exporters.export(shape, p, tolerance=0.02, angularTolerance=0.2)
    return trimesh.load(p)

SH, LID, CAP = (200, 203, 210), (140, 152, 170), (235, 120, 40)
shell_m, lid_m, caps_m = mesh(shell, "s.stl"), mesh(lid_asm, "l.stl"), mesh(caps_asm, "c.stl")
for view, name in (("front", "preview_front.png"), ("back", "preview_back.png"), ("isoback", "preview_iso_back.png"),
                   ("iso", "preview_iso_front.png")):
    render.render([shell_m, lid_m, caps_m], [SH, LID, CAP], view, 1000, name)

# open shell with stand-in parts: display PCB, battery, Feather, switches, piezo
PCB, BAT, FE, SW = (40, 120, 70), (70, 110, 200), (200, 60, 60), (240, 200, 60)
parts = [(f.pcb, PCB), (f.batt, BAT), (f.fe.union(f.fe_comp), FE), (f.sws, SW), (f.piezo, (230, 230, 230))]
meshes = [shell_m, caps_m] + [mesh(p, f"p{i}.stl") for i, (p, c) in enumerate(parts)]
cols = [SH, CAP] + [c for p, c in parts]
render.render(meshes, cols, "back", 1000, "preview_open.png")
render.render(meshes, cols, "isoback", 1000, "preview_open_iso.png")

# cutaways: slice through the D-pad column (x), look at the cut face
def cutaway(keep_box, view, name):
    shapes = [(shell.union(lid_asm), SH), (caps_asm, CAP)] + parts
    ms, cs = [], []
    for i, (s, c) in enumerate(shapes):
        r = s.cut(keep_box)
        if r.val().Volume() < 1e-3: continue
        ms.append(mesh(r, f"cut{i}.stl")); cs.append(c)
    render.render(ms, cs, view, 1000, name)
cutaway(cq.Workplane("XY").box(300, 300, 300).translate((dp_cx + 150, 0, 0)), "side", "preview_cutaway.png")     # keep x < D-pad column, look from +X
