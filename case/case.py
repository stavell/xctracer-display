import cadquery as cq

# ============================================================
# PARAMETERS - Sharp 2.7" memory display (portrait) + Feather nRF52840 Sense (4516) + 55x40x10 LiPo
# Rev 3: D-pad + A/B (6 tact switches) below the screen, all electronics in the SHELL, plain lid.
#
# Print orientation (Bambu A1, PETG, no supports):
#   SHELL  front face down on the bed (z = 0 is the bed).
#   LID    outer face down on the bed.
#   CAPS   head down on the bed (case_buttons.stl, 6 identical caps).
#
# Case frame: Z = 0 at the front face, +Z toward the back. X and Y are as seen from the BACK (right-handed):
#   +Y = top of the device in portrait (display side), -Y = bottom (button band).
#   Seen from the FRONT, case +X is the user's LEFT: the D-pad is at +X, A/B at -X.
# No screws, bolts or inserts anywhere: every joint is a printed snap.
# ============================================================
# Display PCB (source: Adafruit Eagle .brd). Eagle frame = front view, long side (63.5) turned vertical.
pcb_w = 63.5          # mm - PCB long side (runs along case Y)
pcb_h = 55.88         # mm - PCB short side (runs along case X)
pcb_t = 1.6
glass_t = 2.4         # mm - ASSUMED glass module height above PCB front (measure!)
pcb_clear = 0.4       # mm - total clearance around PCB

# Battery 55x40x10, lying with 55 along X, 40 along Y (behind the display)
batt_x, batt_y, batt_z = 55.0, 40.0, 10.0
batt_slack = 0.3
feather_w = 22.86
feather_l = 50.8
comp_zone = 3.5       # mm - back-of-display component / solder clearance

# Shell structure
wall = 3.0
lip_t = 1.2           # bezel lip in front of the glass
corner_r = 5.0
front_chamfer = 1.5
lid_t = 2.0
skirt_h = 7.0
skirt_t = 1.2         # skirt = snap-tab cantilever thickness
fit = 0.25            # lid-to-shell sliding clearance

# ---- buttons: 6 tact switches, separate printed caps ----
dpad_pitch = 9.0      # D-pad centre-to-centre (up/down/left/right from the centre)
ab_dx, ab_dy = 10.0, 3.5   # B is lower-left of A (seen from the front): distance = 10.6
n_cap_layout = 6
plate_t = 2.6         # front plate thickness in the button band
cb_d = 1.6            # cap counterbore depth
cb_dia = 7.6
cap_head_d, cap_head_h = 7.0, 1.4
cap_protrude = 0.15   # head stands this far proud of the front face at rest
cap_stem_d, cap_hole_d = 4.2, 4.7
cap_flange_d, cap_flange_t, cap_flange_ch = 6.4, 0.8, 0.6
sw_size, sw_body_h, sw_plunger = 6.0, 3.5, 1.5    # ASSUMED 6x6 tact: body 3.5 + plunger 1.5 (verify!)
sw_leg_clear = 3.5    # mm - room behind the switch body for legs + wire solder
sw_clear = 0.3
cup_o = 8.6           # cup outer size
chamber_d = 6.9       # flange chamber diameter
plunger_hole_d = 4.0
ring_t = 1.0
lip_p, lip_len, lip_w = 0.35, 0.9, 3.0   # switch retention wedge

# ---- Feather (components face the lid), carried by 4 posts rising from the front plate ----
fe_holes = [(2.54, 2.54), (2.54, 20.32), (48.26, 2.54), (48.26, 20.32)]   # (u along length from USB end, v across)
fe_reset = (11.1, 6.22)
fe_user = (29.93, 5.85)
fe_usb_v = 11.49
fe_pcb_t = 1.6
fe_comp_h = 6.0       # tallest part above the PCB top (JST)
fe_sw_h = 1.8         # tact switch height above PCB top
fe_usb_zc = 1.35      # micro-B centre above PCB top
fe_end_gap = 0.3      # USB end to wall
post_d = 4.4
peg_d, peg_head_d, peg_head_h, peg_slot = 2.4, 3.0, 0.7, 0.6   # short split pegs (hole 2.5)
usb_hole_w, usb_hole_h, usb_ch = 11.5, 7.0, 2.5

# Display window
glass_w, glass_h = 63.5, 41.2
active_w, active_h = 58.8, 35.3
win_margin = 0.3
ffc_w, ffc_y0, ffc_y1 = 18.0, -19.7, -24.4        # Eagle frame (y along the short side)
peg_xy = (29.21, 25.4)
disp_ear = 6.4

# Lid snap tabs (cantilever = skirt wall, free length = skirt_h)
tab_w, tab_slit = 10.0, 0.8
hook_h, hook_depth = 1.2, 0.5      # hook height / depth (flat retaining face + 50 deg lead-in)
groove_extra = 0.1

# Battery rails and stops
led_w, stop_t, stop_h = 2.5, 1.2, 5.0

# Strap / cord tunnel through the bottom corner
strap_slot_w, strap_slot_h = 3.2, 10.4
strap_tip = 2.7       # solid bridge between the slot and the outer corner
strap_z0 = 1.8

# Buzzer
piezo_d = 12.0
piezo_cup_wall, piezo_cup_h = 1.0, 1.0
snd_slots, snd_pitch, snd_w = 5, 2.0, 1.0

# Lid access holes
tube_od = 4.6
reset_hole_d, user_hole_d = 1.8, 3.4

# ============================================================
# DERIVED
# ============================================================
sw_pocket = sw_size + sw_clear
disp_w = pcb_h + pcb_clear                      # display pocket along X
disp_l = pcb_w + pcb_clear                      # display pocket along Y
band_margin = 0.2
band_h = 2 * dpad_pitch + cup_o + 2 * band_margin
inner_w = disp_w
inner_h = disp_l + band_h
outer_w = inner_w + 2 * wall
outer_h = inner_h + 2 * wall
z_pcb_front = lip_t + glass_t
front_z = z_pcb_front + pcb_t                   # PCB back plane
cap_head_bottom = -cap_protrude + cap_head_h    # head underside at rest
cap_stem_len = plate_t - cap_head_bottom
head_gap = cb_d - cap_head_bottom               # cap travel before it bottoms out
z_plunger_tip = plate_t + cap_flange_t + 0.1
z_bf = z_plunger_tip + sw_plunger               # switch body front face
z_bb = z_bf + sw_body_h                         # switch body back face
fe_floor_z = z_bb + sw_leg_clear                # Feather PCB underside
fe_top_z = fe_floor_z + fe_pcb_t                # Feather component face
shell_depth = fe_top_z + fe_comp_h + 1.0
total_depth = shell_depth + lid_t
seam_z = shell_depth
skirt_z0 = seam_z - skirt_h
led_top = front_z + comp_zone
assert led_top + batt_z + 0.5 <= shell_depth

y_bot = -inner_h / 2                            # inner bottom wall face
y_band_top = y_bot + band_h
band_cy = y_bot + band_h / 2
disp_cy = y_band_top + disp_l / 2

# ============================================================
# HELPERS
# ============================================================
def blk(cx, cy, z0, z1, w, h, r=0.0):
    b = cq.Workplane("XY").workplane(offset=z0).center(cx, cy).rect(w, h).extrude(z1 - z0)
    return b.edges("|Z").fillet(r) if r else b

def cyl(cx, cy, z0, z1, d):
    return cq.Workplane("XY").workplane(offset=z0).center(cx, cy).circle(d / 2).extrude(z1 - z0)

# Button centres (case coords). D-pad at +X (user's left), A/B at -X.
xL = inner_w / 2 - cup_o / 2 - 0.2
dp_cx = xL - dpad_pitch
xA = -inner_w / 2 + cup_o / 2 + 0.2
buttons = {
    "left":  (xL, band_cy),
    "right": (dp_cx - dpad_pitch, band_cy),
    "up":    (dp_cx, band_cy + dpad_pitch),
    "down":  (dp_cx, band_cy - dpad_pitch),
    "A":     (xA, band_cy + ab_dy / 2 + 0.5),
    "B":     (xA + ab_dx, band_cy - ab_dy / 2 + 0.5),
}

# Feather: USB end at +X wall, components up, rigid rotation of the Eagle top view (CW 90 deg).
fe_cx = inner_w / 2 - fe_end_gap - feather_l / 2
fe_cy = band_cy
def fe_xy(u, v):
    lx, ly = feather_w / 2 - v, feather_l / 2 - u
    return fe_cx + ly, fe_cy - lx

# ============================================================
# SHELL
# ============================================================
outer = (cq.Workplane("XY").box(outer_w, outer_h, shell_depth, centered=(True, True, False))
         .edges("|Z").fillet(corner_r)
         .faces("<Z").edges().chamfer(front_chamfer))
cav_r = max(0.1, corner_r - wall)
cav_disp = (cq.Workplane("XY").workplane(offset=front_z)
            .center(0, y_band_top + (inner_h / 2 - y_band_top) / 2)
            .rect(inner_w, inner_h / 2 - y_band_top).extrude(shell_depth).edges("|Z and >Y").fillet(cav_r))
cav_band = (cq.Workplane("XY").workplane(offset=plate_t).center(0, band_cy)
            .rect(inner_w, band_h).extrude(shell_depth).edges("|Z and <Y").fillet(cav_r))
cavity = cav_disp.union(cav_band)
rebate = (cq.Workplane("XY").workplane(offset=shell_depth - skirt_h)
          .box(outer_w - 2 * (wall - skirt_t), outer_h - 2 * (wall - skirt_t), skirt_h + 1, centered=(True, True, False))
          .edges("|Z").fillet(max(0.1, corner_r - (wall - skirt_t))))
shell = outer.cut(cavity).cut(rebate)

# --- display window, glass pocket, FFC relief, PCB pocket (display portrait, ear toward case -X) ---
dwin_x = -(pcb_h / 2 - disp_ear - 1.8 - active_h / 2)     # active-area centre, case X
gl_x = -(pcb_h / 2 - disp_ear - glass_h / 2)
shell = shell.cut(blk(dwin_x, disp_cy, -1, z_pcb_front + 0.1, active_h + 2 * win_margin, active_w + 2 * win_margin, 0.5))
shell = shell.cut(blk(gl_x, disp_cy, lip_t, z_pcb_front + 0.1, glass_h + pcb_clear, pcb_w + pcb_clear))
shell = shell.cut(blk(-(ffc_y0 + ffc_y1) / 2, disp_cy, lip_t, z_pcb_front + 0.1, ffc_y0 - ffc_y1, ffc_w))
shell = shell.cut(blk(0, disp_cy, z_pcb_front, front_z + 0.1, disp_w, disp_l, 1.0))

def split_peg(x, y, z_base, z_top):
    """upright split peg: stem from z_base to z_top, head above"""
    stem = cyl(x, y, z_base, z_top, peg_d)
    head = (cq.Workplane("XY").workplane(offset=z_top).center(x, y).circle(peg_head_d / 2)
            .workplane(offset=peg_head_h).circle(peg_d / 2 - 0.3).loft())
    slot = blk(x, y, z_top - 1.0, z_top + peg_head_h + 0.1, peg_head_d + 1, peg_slot)
    return stem.union(head).cut(slot)

for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = sx * peg_xy[1], disp_cy + sy * peg_xy[0]
        shell = shell.union(split_peg(x, y, z_pcb_front - 0.1, front_z))

# --- buttons: hole + counterbore in the plate, cup with flange chamber, plunger ring, switch pocket ---
cup_top = z_bb + 1.0
for name, (bx, by) in buttons.items():
    shell = shell.union(blk(bx, by, plate_t - 0.1, cup_top, cup_o, cup_o))
    shell = shell.cut(cyl(bx, by, -1, cb_d, cb_dia))
    shell = shell.cut(cyl(bx, by, cb_d - 0.1, z_bf - ring_t + 0.0, cap_hole_d))
    shell = shell.cut(cyl(bx, by, plate_t, z_bf - ring_t, chamber_d))
    shell = shell.cut(cyl(bx, by, z_bf - ring_t - 0.1, z_bf + 0.1, plunger_hole_d))
    shell = shell.cut(blk(bx, by, z_bf, cup_top + 1, sw_pocket, sw_pocket))
    for sgn in (-1, 1):                       # retention wedges, flat underside just behind the body
        xw = bx + sgn * sw_pocket / 2
        za = z_bb + 0.1
        pts = [(xw + sgn * 0.1, za), (xw - sgn * lip_p, za), (xw + sgn * 0.1, za + lip_len)]
        w = cq.Workplane("XZ").polyline(pts).close().extrude(lip_w).translate((0, by + lip_w / 2, 0))
        shell = shell.union(w)

# --- Feather posts + split pegs (components face the lid) ---
for (u, v) in fe_holes:
    x, y = fe_xy(u, v)
    shell = shell.union(cyl(x, y, plate_t - 0.1, fe_floor_z, post_d))
    shell = shell.union(split_peg(x, y, fe_floor_z - 0.1, fe_top_z))

# --- battery rails + stops ---
b_y0 = y_band_top + 0.5
b_y1 = b_y0 + batt_y + batt_slack
for sgn in (-1, 1):
    xw = sgn * (inner_w / 2 + 0.2)
    L = (b_y1 + stop_t) - (b_y0 - stop_t)
    pts = [(xw, front_z), (xw, led_top), (xw - sgn * (led_w + 0.2), led_top)]
    rail = cq.Workplane("XZ").polyline(pts).close().extrude(L).translate((0, b_y1 + stop_t, 0))
    shell = shell.union(rail)
    xs = sgn * (inner_w / 2 - led_w / 2 + 0.1)
    for yy in (b_y0 - stop_t / 2, b_y1 + stop_t / 2):
        shell = shell.union(blk(xs, yy, led_top - 0.1, led_top + stop_h, led_w + 0.2, stop_t))

# --- strap / cord tunnel: solid corner lug + 45 degree rectangular slot (bottom, -X corner) ---
cxm, cym = -outer_w / 2, -outer_h / 2
sq2 = 2 ** 0.5
tip_d = 2.07 + strap_tip + strap_slot_w / 2                  # diagonal distance corner -> slot centre line
fill = (tip_d + strap_slot_w / 2 + 1.8) * sq2                # leg length of the solid triangle
tri = (cq.Workplane("XY").workplane(offset=plate_t - 0.1)
       .polyline([(cxm, cym), (cxm + fill, cym), (cxm, cym + fill)]).close().extrude(skirt_z0 - plate_t + 0.1))
shell = shell.union(tri.intersect(cav_band))
slot = (cq.Workplane("XY").box(40, strap_slot_w, strap_slot_h, centered=(True, True, False))
        .translate((0, 0, strap_z0)).rotate((0, 0, 0), (0, 0, 1), -45)
        .translate((cxm + tip_d / sq2, cym + tip_d / sq2, 0)))
shell = shell.cut(slot)

# --- USB opening in the +X wall at the Feather connector (flat 45 deg top = no wide bridge) ---
usb_x = outer_w / 2 - wall / 2
usb_y = fe_xy(0, fe_usb_v)[1]
usb_z = fe_top_z + fe_usb_zc
usb_cut = (cq.Workplane("XY").box(wall + 2, usb_hole_w, usb_hole_h)
           .edges("|X and >Z").chamfer(usb_ch).translate((usb_x, usb_y, usb_z)))
shell = shell.cut(usb_cut)

# --- buzzer: 12 mm disc glued to the inner face of the bottom wall, slots through the wall ---
buz_x, buz_z = -2.0, plate_t + 0.3 + piezo_d / 2
ring = (cq.Workplane("XZ").workplane(offset=-(y_bot + piezo_cup_h)).center(buz_x, buz_z)
        .circle(piezo_d / 2 + 0.2 + piezo_cup_wall).circle(piezo_d / 2 + 0.2).extrude(piezo_cup_h + 0.2))
ring = ring.intersect(blk(buz_x, y_bot, plate_t - 0.1, skirt_z0 - 0.2, 40, 6))
shell = shell.union(ring)
for i in range(snd_slots):
    xo = buz_x + (i - (snd_slots - 1) / 2) * snd_pitch
    shell = shell.cut(blk(xo, y_bot - wall / 2, buz_z - 4.0, buz_z + 3.5, snd_w, wall + 1.5))

# --- lid snap grooves + pry notches ---
sk_o_w, sk_o_h = outer_w - 2 * (wall - skirt_t) - 2 * fit, outer_h - 2 * (wall - skirt_t) - 2 * fit
tabs = [("-Y", -20), ("-Y", 20), ("+Y", -18), ("+Y", 18),
        ("-X", 32), ("-X", 10), ("-X", -10), ("+X", 32), ("+X", 10), ("+X", -14)]
hook_z0 = skirt_z0 + 0.3
def wedge(side, c, depth, z0, h):
    pts = [(0, z0), (0, z0 + h), (depth, z0 + h / 2)]
    prof = cq.Workplane("XZ").polyline(pts).close().extrude(tab_w / 2, both=True)
    if side == "+X": return prof.translate((sk_o_w / 2, c, 0))
    if side == "-X": return prof.rotate((0, 0, 0), (0, 0, 1), 180).translate((-sk_o_w / 2, c, 0))
    if side == "+Y": return prof.rotate((0, 0, 0), (0, 0, 1), 90).translate((c, sk_o_h / 2, 0))
    return prof.rotate((0, 0, 0), (0, 0, 1), -90).translate((c, -sk_o_h / 2, 0))
for side, c in tabs:
    shell = shell.cut(wedge(side, c, hook_depth + groove_extra, hook_z0, hook_h + 0.2))
def pry(lid_or_shell):
    s = lid_or_shell
    s = s.cut(blk(0, outer_h / 2 - 0.5, seam_z - 1.0, seam_z + 1, 14, 2.0))
    return s.cut(blk(-outer_w / 2 + 0.5, -28, seam_z - 1.0, seam_z + 1, 2.0, 14))
shell = pry(shell)

# ============================================================
# LID (plain plate + skirt, built in the assembly frame, flipped for printing)
# ============================================================
lid_plate = (cq.Workplane("XY").box(outer_w, outer_h, lid_t, centered=(True, True, False))
             .edges("|Z").fillet(corner_r).faces("<Z").edges().chamfer(front_chamfer))
skirt_out = (cq.Workplane("XY").workplane(offset=lid_t)
             .box(sk_o_w, sk_o_h, skirt_h, centered=(True, True, False))
             .edges("|Z").fillet(max(0.1, corner_r - (wall - skirt_t) - fit)))
skirt_in = (cq.Workplane("XY").workplane(offset=lid_t)
            .box(sk_o_w - 2 * skirt_t, sk_o_h - 2 * skirt_t, skirt_h + 1, centered=(True, True, False))
            .edges("|Z").fillet(0.5))
lid = lid_plate.union(skirt_out.cut(skirt_in))
lid_asm = lid.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, total_depth))

# access tubes + holes over the Feather reset / user switches (not in the flat velcro area)
sw_top_z = fe_top_z + fe_sw_h
access = []
for (u, v), d in ((fe_reset, reset_hole_d), (fe_user, user_hole_d)):
    x, y = fe_xy(u, v)
    access.append((x, y, d))
    lid_asm = lid_asm.union(cyl(x, y, sw_top_z + 0.8, seam_z + 0.1, tube_od))
for x, y, d in access:
    lid_asm = lid_asm.cut(cyl(x, y, sw_top_z, total_depth + 1, d))

# notches in the skirt: USB plug, piezo disc
lid_asm = lid_asm.cut(usb_cut)
lid_asm = lid_asm.cut(blk(buz_x, y_bot - 1.5, skirt_z0 - 0.1, buz_z + piezo_d / 2 + 0.5, piezo_d + 6, 6))

for side, c in tabs:
    if side in ("+X", "-X"):
        xs = (1 if side == "+X" else -1) * (sk_o_w / 2 - skirt_t / 2)
        for d in (-1, 1):
            lid_asm = lid_asm.cut(blk(xs, c + d * (tab_w / 2 + tab_slit / 2), skirt_z0 - 0.1, seam_z + 0.05, skirt_t + 0.2, tab_slit))
    else:
        ys = (1 if side == "+Y" else -1) * (sk_o_h / 2 - skirt_t / 2)
        for d in (-1, 1):
            lid_asm = lid_asm.cut(blk(c + d * (tab_w / 2 + tab_slit / 2), ys, skirt_z0 - 0.1, seam_z + 0.05, tab_slit, skirt_t + 0.2))
    lid_asm = lid_asm.union(wedge(side, c, hook_depth, hook_z0, hook_h))
lid_asm = pry(lid_asm)
lid = lid_asm.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, total_depth))

# ============================================================
# BUTTON CAPS (head down on the bed; one cap, six copies)
# ============================================================
def make_cap():
    head = cq.Workplane("XY").circle(cap_head_d / 2).extrude(cap_head_h).faces("<Z").chamfer(0.4)
    stem = cyl(0, 0, cap_head_h - 0.01, cap_head_h + cap_stem_len, cap_stem_d)
    fl = (cq.Workplane("XY").workplane(offset=cap_head_h + cap_stem_len - 0.01).circle(cap_flange_d / 2)
          .extrude(cap_flange_t + 0.01).faces("<Z").chamfer(cap_flange_ch))
    return head.union(stem).union(fl.union(cyl(0, 0, cap_head_h + cap_stem_len - 0.02, cap_head_h + cap_stem_len + 0.1, cap_stem_d)))
cap = make_cap()
caps_print = None
for i in range(n_cap_layout):
    c = cap.translate(((i - (n_cap_layout - 1) / 2) * 10.0, 0, 0))
    caps_print = c if caps_print is None else caps_print.union(c)
caps_asm = None
for name, (bx, by) in buttons.items():
    c = cap.translate((bx, by, -cap_protrude))
    caps_asm = c if caps_asm is None else caps_asm.union(c)

# ============================================================
# EXPORT
# ============================================================
if __name__ == "__main__":
    cq.exporters.export(shell, "case_shell.stl", tolerance=0.01, angularTolerance=0.1)
    cq.exporters.export(lid, "case_lid.stl", tolerance=0.01, angularTolerance=0.1)
    cq.exporters.export(caps_print, "case_buttons.stl", tolerance=0.01, angularTolerance=0.1)
    cq.exporters.export(shell.union(lid_asm).union(caps_asm), "case_assembly.stl", tolerance=0.01, angularTolerance=0.1)
    cq.exporters.export(shell, "case_shell.step"); cq.exporters.export(lid, "case_lid.step")
    cq.exporters.export(caps_print, "case_buttons.step")
    print(f"Outer {outer_w:.1f} x {outer_h:.1f} x {total_depth:.1f} mm (shell {shell_depth:.1f}); band_h {band_h:.1f}")
    print(f"PCB back z={front_z:.1f}; switch body z={z_bf:.1f}-{z_bb:.1f}; Feather PCB z={fe_floor_z:.1f}-{fe_top_z:.1f}; "
          f"USB y={usb_y:.2f} z={usb_z:.2f}; cap head_gap {head_gap:.2f}")
    for k, v in buttons.items(): print(f"  {k:5s} ({v[0]:7.2f}, {v[1]:7.2f})")
