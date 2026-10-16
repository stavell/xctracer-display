"""Interference check of case.py against stand-in parts (run: python fitcheck.py).
Stand-in parts are reused by make_previews.py."""
import sys
import cadquery as cq
from case import *

def vol(s):
    try:
        return s.val().Volume()
    except Exception:
        return 0.0

# --- stand-ins ---
pcb = blk(0, disp_cy, z_pcb_front, front_z, disp_w - pcb_clear, disp_l - pcb_clear)
for sx in (-1, 1):
    for sy in (-1, 1):
        pcb = pcb.cut(cyl(sx * peg_xy[1], disp_cy + sy * peg_xy[0], 0, 20, 2.5))
glass = blk(gl_x, disp_cy, lip_t + 0.1, z_pcb_front, glass_h, glass_w)
backcomp = blk(0, disp_cy, front_z, front_z + comp_zone, 40, 50)
batt = blk(0, b_y0 + batt_y / 2, led_top, led_top + batt_z, batt_x, batt_y)
fe = blk(fe_cx, fe_cy, fe_floor_z, fe_top_z, feather_l, feather_w)
for (u, v) in fe_holes:
    fe = fe.cut(cyl(*fe_xy(u, v), 0, 40, 2.5))
def fe_part(u, v, w, l, h):          # (w along case X, l along case Y)
    x, y = fe_xy(u, v)
    return blk(x, y, fe_top_z, fe_top_z + h, w, l)
fe_comp = (fe_part(0, fe_usb_v, 6.0, 8.0, 2.8)
           .union(fe_part(10.8, 18, 6.0, 8.0, 6.0))          # JST
           .union(fe_part(*fe_reset, 2.8, 4.6, fe_sw_h))
           .union(fe_part(*fe_user, 2.8, 4.6, fe_sw_h)))
sws = None
for name, (bx, by) in buttons.items():
    s_ = blk(bx, by, z_bf, z_bb, sw_size, sw_size).union(cyl(bx, by, z_plunger_tip, z_bf, 3.5))
    sws = s_ if sws is None else sws.union(s_)
piezo = (cq.Workplane("XZ").workplane(offset=-(y_bot + 0.9)).center(buz_x, buz_z).circle(piezo_d / 2).extrude(0.8))
batt_wire = None

case = shell.union(lid_asm)
print(f"Outer {outer_w:.1f} x {outer_h:.1f} x {total_depth:.1f} mm")
bad = 0
def chk(name, a, b, tol=0.5):     # face-contact slivers (peg heads, flanges) are < 0.5 mm3
    global bad
    v = vol(a.intersect(b))
    flag = "" if v < tol else "   <-- INTERFERENCE"
    bad += v >= tol
    print(f"{name:34s} {v:8.2f} mm3{flag}")
for name, comp in (("display pcb", pcb), ("glass", glass), ("display back comps", backcomp), ("battery", batt),
                   ("feather", fe), ("feather comps", fe_comp), ("switches", sws), ("piezo disc", piezo)):
    chk(f"{name} vs shell+lid", case, comp)
chk("button caps vs shell+lid", case, caps_asm)
chk("battery vs feather+comps", batt, fe.union(fe_comp))
chk("feather vs display back", fe, backcomp)
chk("switches vs feather", sws, fe.union(fe_comp))
chk("caps vs switches", caps_asm, sws)
chk("piezo vs feather", piezo, fe)
# access holes: pin path from outside to the switch must be clear
for (u, v), d in ((fe_reset, reset_hole_d), (fe_user, user_hole_d)):
    x, y = fe_xy(u, v)
    pin = cyl(x, y, sw_top_z, total_depth + 1, d * 0.9)
    chk(f"access pin path ({d} mm)", case, pin)
# cap travel: head gap and plunger preload
print(f"cap head_gap {head_gap:.2f} mm (switch needs ~0.25), plunger gap to flange {z_plunger_tip - plate_t - cap_flange_t:.2f} mm")
# strain of the snap tabs: eps = 1.5 t d / L^2
print(f"snap tab strain {1.5 * skirt_t * hook_depth / skirt_h**2 * 100:.2f} %  (L {skirt_h} t {skirt_t}, L/t {skirt_h/skirt_t:.1f}), tabs {len(tabs)}")
# flat lid area: lid face above the access holes
ys = [fe_xy(*fe_reset)[1], fe_xy(*fe_user)[1]]
y_free0 = max(ys) + 4
print(f"flat lid: full {outer_w - 2*front_chamfer:.1f} x {outer_h - 2*front_chamfer:.1f}; hole-free above y={y_free0:.1f}: "
      f"{outer_w - 2*front_chamfer:.1f} x {outer_h/2 - front_chamfer - y_free0:.1f} mm")
print("RESULT:", "FAIL" if bad else "PASS")
