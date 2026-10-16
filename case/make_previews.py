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

SH, LID, CAP, INL, DECK = (205, 207, 212), (140, 152, 170), (235, 120, 40), (25, 95, 150), (120, 170, 130)
shell_m, lid_m, caps_m = mesh(shell, "s.stl"), mesh(lid_asm, "l.stl"), mesh(caps_asm, "c.stl")
inlay_m = mesh(inlay, "i.stl")
for view, name in (("front", "preview_front.png"), ("back", "preview_back.png"), ("isoback", "preview_iso_back.png"),
                   ("iso", "preview_iso_front.png")):
    render.render([shell_m, lid_m, caps_m, inlay_m], [SH, LID, CAP, INL], view, 1000, name)

# open shell with stand-in parts: display PCB, battery, Feather, switches, piezo, deck
PCB, BAT, FE, SW = (40, 120, 70), (70, 110, 200), (200, 60, 60), (240, 200, 60)
parts = [(f.pcb, PCB), (f.batt, BAT), (f.fe.union(f.fe_comp), FE), (f.sws, SW), (f.piezo, (230, 230, 230)), (deck, DECK)]
meshes = [shell_m, caps_m, inlay_m] + [mesh(p, f"p{i}.stl") for i, (p, c) in enumerate(parts)]
cols = [SH, CAP, INL] + [c for p, c in parts]
render.render(meshes, cols, "back", 1000, "preview_open.png")
render.render(meshes, cols, "isoback", 1000, "preview_open_iso.png")

# cutaways through one cap, zoomed on the control band: keep x < plane, look from +X (front of the case is on the right)
def cutaway(x_plane, name, extra=()):
    win = cq.Workplane("XY").box(300, 2 * 19, 300).translate((0, band_cy, 0))             # only the control band
    keep = win.cut(cq.Workplane("XY").box(300, 300, 300).translate((x_plane + 150, 0, 0)))
    shapes = [(shell.union(deck), SH), (caps_asm, CAP), (f.sws, (240, 200, 60))] + list(extra)
    ms, cs = [], []
    for i, (s, c) in enumerate(shapes):
        r = s.intersect(keep)
        if r.val().Volume() < 1e-3: continue
        ms.append(mesh(r, f"cut{i}.stl")); cs.append(c)
    render.render(ms, cs, "side", 1000, name)
cutaway(dp_cx, "preview_cutaway.png")
cutaway(buttons["A"][0], "preview_cutaway_ab.png")
