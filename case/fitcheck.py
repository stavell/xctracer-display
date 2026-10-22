"""Interference + assembly check of case.py against stand-in parts (run: python fitcheck.py).
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
pcb = blk(pcb_dx, disp_cy, z_pcb_front, front_z, disp_w - pcb_clear, disp_l - pcb_clear)
for sx in (-1, 1):
    for sy in (-1, 1):
        pcb = pcb.cut(cyl(pcb_dx + sx * peg_xy[1], disp_cy + sy * peg_xy[0], 0, 20, 2.5))
glass = blk(gl_x, disp_cy, lip_t + 0.1, z_pcb_front, glass_h, glass_w)
backcomp = blk(pcb_dx, disp_cy, front_z, front_z + comp_zone, 40, 50)
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
    s_ = blk(bx, by, z_bf, z_bb, sw_size, sw_leg_w).union(cyl(bx, by, z_pt, z_bf, 3.5))
    sws = s_ if sws is None else sws.union(s_)
# PS1240-type piezo: body axis along Y, sound face on the top wall
piezo = (cq.Workplane("XZ").center(buz_x, buz_z).circle(piezo_d / 2).extrude(-piezo_h).translate((0, y_top - piezo_h, 0)))

case = shell.union(deck).union(lid_asm)
print(f"Outer {outer_w:.1f} x {outer_h:.1f} x {total_depth:.1f} mm")
bad = 0
def chk(name, a, b, tol=0.5):     # face-contact slivers (peg heads, flanges) are < 0.5 mm3
    global bad
    v = vol(a.intersect(b))
    flag = "" if v < tol else "   <-- INTERFERENCE"
    bad += v >= tol
    print(f"{name:40s} {v:8.2f} mm3{flag}")
for name, comp in (("display pcb", pcb), ("glass", glass), ("display back comps", backcomp), ("battery", batt),
                   ("feather", fe), ("feather comps", fe_comp), ("switches", sws), ("piezo", piezo)):
    chk(f"{name} vs shell+deck+lid", case, comp)
chk("button caps vs shell+deck+lid", case, caps_asm)
chk("deck vs shell", deck, shell)
chk("battery vs feather+comps", batt, fe.union(fe_comp))
chk("feather vs display back", fe, backcomp)
chk("switches vs feather", sws, fe.union(fe_comp))
chk("caps vs switches", caps_asm, sws)
chk("piezo vs feather", piezo, fe.union(fe_comp))
chk("piezo vs display back / battery", piezo, backcomp.union(batt))
chk("inlay inside shell material", inlay, shell_plain.cut(shell_plain.cut(inlay)), tol=1e9) if False else None
v_in = vol(inlay)
print(f"inlay volume {v_in:.1f} mm3; part inside solid shell material {vol(inlay.intersect(shell_plain)):.1f} mm3")
if abs(vol(inlay.intersect(shell_plain)) - v_in) > 0.5:
    print("   <-- INLAY OVERLAPS AN OPENING"); bad += 1

# ---------------- cap assembly check (caps go in from the inside) ----------------
print("--- cap assembly (cap inserted from inside, head first through the opening) ---")
def opening_prism(kind, grow=0.0):
    """opening in cap coordinates (cap front tip at z = 0; plate back at z = plate_th)"""
    if kind == "dp":
        return cross(dp_h + op_clr + grow, dp_w + op_clr + grow, -1, 40, dp_r + op_clr + grow)
    return cyl(0, 0, -1, 40, ab_d + 2 * (op_clr + grow))
for kind, cap, th, hd in (("dp", dp_cap, dp_th, "D-pad"), ("ab", ab_cap["A"], ab_th, "A"), ("ab", ab_cap["B"], ab_th, "B")):
    head = cap.intersect(blk(0, 0, -1, th - 0.01, 60, 60))                    # everything in front of the flange
    not_passing = vol(head.cut(opening_prism(kind)))
    snug = vol(head.cut(opening_prism(kind, -(op_clr - 0.28))))               # must still be inside the opening - (0.3 - 0.28)
    # retention: flange cross-section well above the chamfer must contain the opening grown by (fl_ov - 0.01)
    sl = blk(0, 0, th + 0.7, th + 0.8, 80, 80)
    flange = cap.intersect(sl)
    need = opening_prism(kind, fl_ov - 0.01).intersect(sl)
    missing = vol(need.cut(flange))
    ok = not_passing < 0.01 and snug < 0.01 and missing < 0.01
    bad += not ok
    print(f"{hd:6s} head passes through opening: {'yes' if not_passing < 0.01 else 'NO'}; clearance >= 0.28: "
          f"{'yes' if snug < 0.01 else 'NO'}; flange overlap >= {fl_ov - 0.01:.2f} mm all round: {'yes' if missing < 0.01 else 'NO'}"
          f"   {'' if ok else '<-- FAIL'}")
print(f"head height above the front face: D-pad {dp_proud} mm, A/B {ab_proud} mm; flange thickness {fl_t}")

# ---------------- plan-view clearances of the flanges (they swing in the gap between plate and deck) ----------------
def flange_solid(k):
    if k == "dp":
        return cross(dp_h + fl_off + 0.3, dp_w + fl_off + 0.3, plate_t, plate_t + fl_t + 0.5, dp_r + fl_off + 0.3, dp_cx, band_cy)
    bx, by = buttons[k]
    return cyl(bx, by, plate_t, plate_t + fl_t + 0.5, ab_d + 2 * fl_off + 0.6)
shell_stuff = shell.intersect(blk(0, band_cy, plate_t + 0.01, plate_t + fl_t + 0.5, outer_w, band_h))
for k in ("dp", "A", "B"):
    chk(f"flange swing space {k} vs shell/posts", flange_solid(k), shell_stuff)
chk("flange D-pad vs A/B", flange_solid("dp"), flange_solid("A").union(flange_solid("B")))

# ---------------- rocking test: tilt the D-pad until the nub meets the deck ----------------
print("--- D-pad rocking ---")
import math as _m
ng = z_deck - (plate_t + fl_t + nub_h)
ext = dp_fl_half - fl_off                                 # inner edge of the flange/plate contact = opening edge
edge = dp_h + op_clr                                      # contact line distance from the centre
theta = ng / edge
stub_travel = theta * (dp_pitch + edge)
print(f"nub gap {ng:.2f} mm -> tilt {_m.degrees(theta):.2f} deg, stub travel at the limit {stub_travel:.2f} mm "
      f"(plunger gap {plunger_gap} + travel {sw_travel} -> {plunger_gap + sw_travel:.2f}); centre press travel {ng:.2f} mm")
fire = plunger_gap + sw_travel
if not (ng < fire <= stub_travel):
    bad += 1
    print(f"  <-- ROCKING: need centre travel {ng:.2f} < {fire:.2f} <= arm travel {stub_travel:.2f}")
for ang, nm in ((0, "up"), (180, "down"), (90, "left"), (270, "right")):
    # axis through the opposite contact line, perpendicular to the pressed arm
    capr = dp_cap.translate((0, 0, -dp_proud)).rotate((0, 0, 0), (0, 0, 1), 0)
    ax = (1, 0, 0) if nm in ("up", "down") else (0, 1, 0)
    sgn = {"up": 1, "down": -1, "left": 1, "right": -1}[nm]
    px, py = (0, -sgn * edge) if nm in ("up", "down") else (-sgn * edge, 0)
    rot = _m.degrees(theta) * (1 if nm in ("up", "right") else -1)
    r = capr.translate((0, 0, 0)).rotate((px, py, plate_t), (px + ax[0], py + ax[1], plate_t + ax[2]), rot)
    r = r.translate((dp_cx, band_cy, 0))
    v = vol(case.intersect(r))
    flag = "" if v < 0.5 else "   <-- INTERFERENCE"
    bad += v >= 0.5
    print(f"  rock {nm:5s}: cap vs shell/deck {v:6.2f} mm3{flag}")

# ---------------- misc ----------------
print(f"plunger gap to stub {plunger_gap} mm; deck ring {z_bf - z_deck:.1f} mm; flange-to-deck {z_deck - plate_t - fl_t:.1f} mm")
print(f"snap tab strain {1.5 * skirt_t * hook_depth / skirt_h**2 * 100:.2f} %  (L {skirt_h} t {skirt_t}, L/t {skirt_h/skirt_t:.1f}), tabs {len(tabs)}")
print(f"flat lid: {outer_w - 2*front_chamfer:.1f} x {outer_h - 2*front_chamfer:.1f}, no holes (wordmark inlay in the bottom {lid_wm_y + outer_h/2 + lid_cap_h/2:.1f} mm)")
print(f"bezel (case edge to window): left {outer_w/2 - win_w/2:.2f} mm, right {outer_w/2 - win_w/2:.2f} mm; "
      f"window centre x = 0, D-pad centre x = {dp_cx:.2f}, A/B pair centre x = {pair_cx:.2f}")
pn_gap = pn_bot - (band_cy + dp_h + op_clr)
print(f"panel bottom to the D-pad opening: {pn_gap:.2f} mm; to A: {pn_bot - (buttons['A'][1] + ab_d / 2 + op_clr):.2f} mm")
if pn_gap < 3.0:
    bad += 1
    print("   <-- PANEL TOO CLOSE TO THE D-PAD")
print("RESULT:", "FAIL" if bad else "PASS")
if __name__ == "__main__":
    sys.exit(1 if bad else 0)
