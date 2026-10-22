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

# palette "Forest": body cream, panel forest green, caps orange with cream legends, lid wordmark forest green
BODY, PANEL, CAP, GLASS = (238, 228, 205), (36, 86, 62), (232, 112, 40), (176, 180, 186)
DECK = (120, 170, 130)
shell_m, lid_m, caps_m = mesh(shell, "s.stl"), mesh(lid_asm, "l.stl"), mesh(caps_asm, "c.stl")
inlay_m, lid_inlay_m, caps_inlay_m = mesh(inlay, "i.stl"), mesh(lid_inlay_asm, "li.stl"), mesh(caps_inlay_asm, "ci.stl")
glass_m = mesh(f.glass, "g.stl")
two = [shell_m, lid_m, caps_m, inlay_m, lid_inlay_m, caps_inlay_m, glass_m]
two_c = [BODY, BODY, CAP, PANEL, PANEL, BODY, GLASS]
for view, name in (("front", "preview_front.png"), ("back", "preview_back.png"), ("isoback", "preview_iso_back.png"),
                   ("iso", "preview_iso_front.png")):
    render.render(two, two_c, view, 1000, name)
# close-up of the control band and the panel's lower edge (front view: x is mirrored)
render.render(two, two_c, "front", 1000, "preview_front_closeup.png", zoom=2.3, focus=(0, band_cy + 12))
# the lid from inside: plate, rib grid (full height / low), skirt, tabs, tongue hooks
render.render([lid_m, lid_inlay_m], [BODY, PANEL], "front", 1000, "preview_lid_inner.png")

# open shell with stand-in parts: display PCB, battery, Feather, switches, piezo, deck; lid off, back view
PCB, BAT, FE, SW = (40, 120, 70), (70, 110, 200), (200, 60, 60), (240, 200, 60)
parts = [(f.pcb, PCB), (f.batt, BAT), (f.fe.union(f.fe_comp), FE), (f.sws, SW), (f.piezo, (230, 230, 230)), (deck, DECK)]
meshes = [shell_m, caps_m, inlay_m, caps_inlay_m] + [mesh(p, f"p{i}.stl") for i, (p, c) in enumerate(parts)]
cols = [BODY, CAP, PANEL, BODY] + [c for p, c in parts]
render.render(meshes, cols, "back", 1000, "preview_open.png")
render.render(meshes, cols, "isoback", 1000, "preview_open_iso.png")

# cutaways: keep x < plane, look from +X (front of the case is on the right); y window around the feature
def cutaway(x_plane, y0, half, name, shapes, zoom=1.0, focus=None):
    win = cq.Workplane("XY").box(300, 2 * half, 300).translate((0, y0, 0))
    keep = win.cut(cq.Workplane("XY").box(300, 300, 300).translate((x_plane + 150, 0, 0)))
    ms, cs = [], []
    for i, (s, c) in enumerate(shapes):
        r = s.intersect(keep)
        if r.val().Volume() < 1e-3: continue
        ms.append(mesh(r, f"cut{i}.stl")); cs.append(c)
    render.render(ms, cs, "side", 1000, name, zoom=zoom, focus=focus)       # view coordinates of "side": (-z, y)
cutaway(dp_cx, band_cy, 19, "preview_cutaway.png", [(shell.union(deck), BODY), (caps_asm, CAP), (f.sws, SW)])
cutaway(buttons["A"][0], band_cy, 19, "preview_cutaway_ab.png", [(shell.union(deck), BODY), (caps_asm, CAP), (f.sws, SW)])
# lid tongue and ledge (top end), through the left tongue
cutaway(tg_xs[1], outer_h / 2 - 12, 14, "preview_section_tongue.png",
        [(shell, BODY), (lid_asm, PANEL), (f.piezo, (230, 230, 230))], zoom=1.0)

# heat stake: through the axis of a display pin: pin, PCB, the melted head (dome), the head envelope and the iron access cylinder
nm_, px, py, zs_, zt_ = disp_pins[2]
sph_r = (head_d ** 2 / 4 + head_h ** 2) / (2 * head_h)
dome = (cq.Workplane("XY").sphere(sph_r).translate((px, py, zt_ + head_h - sph_r)).intersect(blk(px, py, zt_, zt_ + head_h + 1, head_d + 1, head_d + 1)))
env_ = cyl(px, py, zt_, zt_ + env_h, env_d)
iron_ = cyl(px, py, zt_ + env_h, zt_ + env_h + iron_h, iron_d)
cutaway(px, py, 12, "preview_section_stake.png",
        [(shell, BODY), (f.pcb, PCB), (f.glass, GLASS), (dome, CAP), (iron_.union(env_), (225, 225, 120))], zoom=2.6, focus=(-(zt_ + 2.0), py))
# the same pin before staking, next to the cradle wall
cutaway(px, py, 12, "preview_section_stake_before.png", [(shell, BODY), (f.pcb, PCB), (f.glass, GLASS)], zoom=2.6, focus=(-(zt_ + 2.0), py))

# battery cradle: through a corner block pair (x = 26): the rounded cell, the blocks hugging its rounded long edges, 45 deg undersides
cutaway(26.0, (cp_y0 + cp_y1) / 2, 24, "preview_section_cradle.png",
        [(shell, BODY), (f.batt_at(f._x_tape), BAT), (f.pcb, PCB), (f.backcomp, (150, 150, 150))], zoom=1.5, focus=(-14.0, (cp_y0 + cp_y1) / 2))
# along X: the end wall, the shelf with the tape, the cell against it (section at the cell centre line)
render_cut = cq.Workplane("XY").box(300, 300, 300).translate((0, 150 + (cp_y0 + cp_y1) / 2, 0))
ms, cs = [], []
for i, (s_, c_) in enumerate([(shell, BODY), (f.batt_at(f._x_tape), BAT), (f.pcb, PCB)]):
    r_ = s_.cut(render_cut)
    ms.append(mesh(r_, f"cx{i}.stl")); cs.append(c_)
render.render(ms, cs, "back", 1000, "preview_section_cradle_x.png", zoom=1.0, focus=(0, 0))

# snap finger: through the bottom finger (x = 0): finger, hook, groove, rim relief, root fillet
cutaway(0.0, -outer_h / 2 + 8, 9, "preview_section_tab.png", [(shell, BODY), (lid_asm, PANEL)], zoom=1.9, focus=(-(seam_z - 4.5), -outer_h / 2 + 4.5))

# strap corner: from the back with the lid off, and from outside
strap_sec = mesh(shell.intersect(blk(0, 0, -1, strap_z0 + strap_slot_h / 2, outer_w + 2, outer_h + 2)), "strap_sec.stl")
render.render([strap_sec], [BODY], "back", 1000, "preview_strap_corner.png", zoom=3.0, focus=(cxm + 14, cym + 14))
render.render([shell_m, inlay_m], [BODY, PANEL], "isoback", 1000, "preview_strap_outside.png", zoom=3.0, focus=(cxm + 6, cym + 12))
