import cadquery as cq

# ============================================================
# PARAMETERS - Sharp 2.7" memory display + Feather nRF52840 (4516) + 55x40x10 LiPo
# PHASE 2: features (window, pegs, buttons, USB, snap tabs).
# Print orientation: SHELL front face down on bed (chamfered front edge, no supports);
#                    LID outer face down on bed.
# Coordinates: X right, Y up (top = display header side), Z = 0 at front face, +Z toward back.
# ============================================================
# Display PCB (source: Adafruit Eagle .brd, Adafruit-Sharp-Memory-Display-PCBs)
pcb_w = 63.5          # mm - PCB width (2.5")
pcb_h = 55.88         # mm - PCB height (2.2")
pcb_t = 1.6           # mm - PCB thickness (standard)
glass_t = 2.4         # mm - ASSUMED glass module height above PCB front (measure!)
pcb_clear = 0.4       # mm - total clearance around PCB

# Battery 55x40x10 (user), mounted rotated: 40 across X, 55 along Y
batt_x, batt_y, batt_z = 40.0, 55.0, 10.0
feather_w = 22.86     # mm - Feather width (Adafruit Eagle .brd: 50.8 x 22.86)
feather_l = 50.8      # mm
comp_zone = 3.5       # mm - back-of-display component / solder clearance
depth_slack = 0.5     # mm

# Button column to the right of the display
button_col_w = 13.0   # mm

# Shell structure
wall = 3.0            # mm - wall (outer skin 1.6 + 1.4 rebate for lid skirt)
lip_t = 1.2           # mm - bezel lip in front of the glass (display recess depth)
corner_r = 5.0        # mm - vertical corner radius
front_chamfer = 1.5   # mm - front edge chamfer (print-friendly, soft feel)
lid_t = 2.0           # mm - lid plate thickness
skirt_h = 7.0         # mm - lid skirt/rebate depth
skirt_t = 1.4         # mm - lid skirt thickness
fit = 0.25            # mm - lid-to-shell clearance

# ============================================================
# DERIVED
# ============================================================
inner_w = pcb_w + pcb_clear + button_col_w
inner_h = pcb_h + pcb_clear
outer_w = inner_w + 2 * wall
outer_h = inner_h + 2 * wall
front_z = lip_t + glass_t + pcb_t                    # PCB back plane
shell_depth = front_z + comp_zone + batt_z + depth_slack
total_depth = shell_depth + lid_t

# ============================================================
# MODEL
# ============================================================
# --- Shell (front down on bed) ---
outer = (cq.Workplane("XY").box(outer_w, outer_h, shell_depth, centered=(True, True, False))
         .edges("|Z").fillet(corner_r)
         .faces("<Z").edges().chamfer(front_chamfer))
cavity = (cq.Workplane("XY").workplane(offset=front_z)
          .box(inner_w, inner_h, shell_depth, centered=(True, True, False))
          .edges("|Z").fillet(max(0.1, corner_r - wall)))
rebate = (cq.Workplane("XY").workplane(offset=shell_depth - skirt_h)
          .box(outer_w - 2 * (wall - skirt_t), outer_h - 2 * (wall - skirt_t), skirt_h + 1,
               centered=(True, True, False))
          .edges("|Z").fillet(max(0.1, corner_r - (wall - skirt_t))))
shell = outer.cut(cavity).cut(rebate)

# --- Lid (outer face down on bed) ---
lid_plate = (cq.Workplane("XY").box(outer_w, outer_h, lid_t, centered=(True, True, False))
             .edges("|Z").fillet(corner_r)
             .faces("<Z").edges().chamfer(front_chamfer))
sk_o_w, sk_o_h = outer_w - 2 * (wall - skirt_t) - 2 * fit, outer_h - 2 * (wall - skirt_t) - 2 * fit
skirt_out = (cq.Workplane("XY").workplane(offset=lid_t)
             .box(sk_o_w, sk_o_h, skirt_h, centered=(True, True, False))
             .edges("|Z").fillet(max(0.1, corner_r - (wall - skirt_t) - fit)))
skirt_in = (cq.Workplane("XY").workplane(offset=lid_t)
            .box(sk_o_w - 2 * skirt_t, sk_o_h - 2 * skirt_t, skirt_h + 1, centered=(True, True, False))
            .edges("|Z").fillet(0.5))
lid = lid_plate.union(skirt_out.cut(skirt_in))


# ============================================================
# PHASE 2 PARAMETERS
# ============================================================
# Display glass/active area (read from Adafruit product photos, +/-0.3 mm)
glass_w, glass_h = 63.5, 41.2          # mm - glass module outline
glass_cy = pcb_h/2 - 6.4 - glass_h/2   # glass centre Y relative to PCB centre (header ear 6.4 mm)
active_w, active_h = 58.8, 35.3        # mm - active area (Adafruit spec)
active_cy = pcb_h/2 - 6.4 - 1.8 - active_h/2
win_margin = 0.3                       # mm - window margin per side
ffc_w, ffc_y0, ffc_y1 = 18.0, -19.7, -24.4   # FFC connector relief (rel. PCB centre)
peg_xy = (29.21, 25.4)                 # mm - PCB hole spacing/2 (58.42 x 50.8)
peg_d, peg_head_d, peg_head_h, peg_slot = 2.3, 2.9, 0.7, 0.6

# Buttons (4 flexure tongues on the front, 6x6 tact switches behind)
n_buttons = 4
btn_pitch = 13.4
tongue_w, tongue_l, tongue_t, slot_w = 7.0, 11.0, 1.2, 0.6
btn_press_from_hinge = 8.5
nub_d, nub_h = 3.6, 1.4
sw_size, sw_body_h, sw_plunger = 6.0, 3.5, 1.5    # 6x6 tact: body 3.5 + plunger 1.5
sw_clear, ring_t, ring_hole = 0.3, 1.0, 4.0
cup_wall = 1.2
dish_d, dish_depth = 6.5, 0.4

# Feather (components face the lid; Eagle .brd coordinates)
fe_right_x = 25.43                     # mm - case X of Feather long edge v=0
fe_top_y = 27.4                        # mm - case Y of Feather USB end
fe_holes = [(2.54, 2.54), (2.54, 20.32), (48.26, 2.54), (48.26, 20.32)]   # (u along length, v across)
fe_reset = (11.1, 6.22)                # (u, v) SW1
fe_user = (29.93, 5.85)                # (u, v) SW2
fe_pcb_t = 1.6
fe_top_h = 6.5                         # mm - Feather component face to lid inner plane (post length)
fe_usb_uv_v = 11.49
fe_usb_zc = 1.35                       # mm - micro-B centre, from Feather component face toward lid
fe_sw_h = 1.8                          # mm - tact switch height above Feather component face
usb_hole_w, usb_hole_h = 11.5, 7.0     # mm - plug overmold clearance
lid_btn_t = 1.0                        # mm - lid flexure tongue thickness
lid_tongue_l, lid_tongue_w = 11.0, 7.0

# Snap tabs on lid skirt
tab_w, tab_slit = 10.0, 0.8
hook_h, hook_base = 0.6, 1.2
groove_extra = 0.1

# ============================================================
# HELPERS
# ============================================================
def blk(cx, cy, z0, z1, w, h, r=0.0):
    b = cq.Workplane("XY").workplane(offset=z0).center(cx, cy).rect(w, h).extrude(z1 - z0)
    return b.edges("|Z").fillet(r) if r else b

def cyl(cx, cy, z0, z1, d):
    return cq.Workplane("XY").workplane(offset=z0).center(cx, cy).circle(d / 2).extrude(z1 - z0)

px = -inner_w/2 + (pcb_w + pcb_clear)/2          # PCB centre X in case coords
bx = inner_w/2 - button_col_w/2                  # button column centre X
z_pcb_front = lip_t + glass_t
seam_z = shell_depth                              # lid inner plane (assembly frame)
skirt_z0 = seam_z - skirt_h

# ============================================================
# SHELL FEATURES
# ============================================================
# window + glass pocket + FFC relief + PCB pocket
shell = shell.cut(blk(px, active_cy, -1, z_pcb_front + 0.1, active_w + 2*win_margin, active_h + 2*win_margin, 0.5))
shell = shell.cut(blk(px, glass_cy, lip_t, z_pcb_front + 0.1, pcb_w + pcb_clear, glass_h + pcb_clear))
shell = shell.cut(blk(px, (ffc_y0 + ffc_y1)/2, lip_t, z_pcb_front + 0.1, ffc_w, ffc_y0 - ffc_y1))
shell = shell.cut(blk(px, 0, z_pcb_front, front_z + 0.1, pcb_w + pcb_clear, pcb_h + pcb_clear, 1.0))

# split pegs through the display PCB mounting holes (snap fit, no screws)
for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = px + sx*peg_xy[0], sy*peg_xy[1]
        stem = cyl(x, y, z_pcb_front - 0.1, front_z + 0.0, peg_d)
        head = (cq.Workplane("XY").workplane(offset=front_z).center(x, y).circle(peg_head_d/2)
                .workplane(offset=peg_head_h).circle(peg_d/2 - 0.3).loft())
        slot = blk(x, y, z_pcb_front + 0.8, front_z + peg_head_h + 0.1, peg_head_d + 1, peg_slot)
        shell = shell.union(stem).union(head.cut(slot)).cut(slot.intersect(cyl(x, y, 0, 99, peg_head_d + 0.2)))

# buttons: flexure tongues + switch cups
ring_z0 = tongue_t + nub_h + 0.1 + sw_plunger - ring_t     # ring front face
z_bf = ring_z0 + ring_t                                    # switch body front face
sw_pocket = sw_size + sw_clear
cup_o = sw_pocket + 2*cup_wall
cave_z1 = ring_z0
btn_pts = []
for i in range(n_buttons):
    yc = (i - (n_buttons - 1)/2) * btn_pitch
    yh = yc - tongue_l/2 + 0.3
    ysw = yh + btn_press_from_hinge
    btn_pts.append(ysw)
    shell = shell.union(blk(bx, ysw, front_z - 0.1, z_bf + sw_body_h, cup_o, cup_o))
    sl_y0, sl_y1 = yh, yh + tongue_l + slot_w
    # U-slot through the tongue plate + cave behind it
    shell = shell.cut(blk(bx - tongue_w/2 - slot_w/2, (sl_y0+sl_y1)/2, -1, tongue_t, slot_w, sl_y1 - sl_y0))
    shell = shell.cut(blk(bx + tongue_w/2 + slot_w/2, (sl_y0+sl_y1)/2, -1, tongue_t, slot_w, sl_y1 - sl_y0))
    shell = shell.cut(blk(bx, yh + tongue_l + slot_w/2, -1, tongue_t, tongue_w + 2*slot_w, slot_w))
    shell = shell.cut(blk(bx, (sl_y0+sl_y1)/2, tongue_t, cave_z1, tongue_w + 2*slot_w, sl_y1 - sl_y0))
    shell = shell.union(cyl(bx, ysw, tongue_t - 0.1, tongue_t + nub_h, nub_d))
    shell = shell.cut(cyl(bx, ysw, cave_z1 - 0.1, z_bf + 0.1, ring_hole))              # plunger hole
    shell = shell.cut(blk(bx, ysw, z_bf, z_bf + sw_body_h + 1, sw_pocket, sw_pocket))   # switch pocket
    shell = shell.cut(cq.Workplane('XY').workplane(offset=-1).center(bx, ysw).polygon(48, dish_d).extrude(dish_depth + 1))                              # finger dish

# USB opening (top wall), centred on the Feather connector
fe_x = lambda v: fe_right_x - v
fe_y = lambda u: fe_top_y - u
fe_face_z = seam_z - fe_top_h                      # Feather component face (case Z)
usb_x, usb_z = fe_x(fe_usb_uv_v), fe_face_z + fe_usb_zc
usb_cut = (cq.Workplane("XY").box(usb_hole_w, wall + 2, usb_hole_h)
           .edges("|Y").fillet(1.5).translate((usb_x, inner_h/2 + wall/2, usb_z)))
shell = shell.cut(usb_cut)

# lid snap-tab grooves + pry notches (shell side)
skirt_outer_off = (sk_o_w/2, sk_o_h/2)             # skirt outer faces
tabs = [("-Y", -22), ("-Y", 22), ("+Y", -22), ("+Y", 30), ("-X", 0), ("+X", 0)]
hook_z0 = skirt_z0 + 0.3
def wedge(side, c, depth, z0, h):
    """triangular prism: base on skirt face, apex `depth` outward; length tab_w along wall."""
    pts = [(0, z0), (0, z0 + h), (depth, z0 + h/2)]
    prof = cq.Workplane("XZ").polyline(pts).close().extrude(tab_w/2, both=True)   # extrudes along Y, centred
    # profile local x = outward direction (+X) ; extrusion axis Y = along-wall for +/-X sides
    if side == "+X": return prof.translate((sk_o_w/2, c, 0))
    if side == "-X": return prof.rotate((0, 0, 0), (0, 0, 1), 180).translate((-sk_o_w/2, c, 0))
    if side == "+Y": return prof.rotate((0, 0, 0), (0, 0, 1), 90).translate((c, sk_o_h/2, 0))
    return prof.rotate((0, 0, 0), (0, 0, 1), -90).translate((c, -sk_o_h/2, 0))
for side, c in tabs:
    shell = shell.cut(wedge(side, c, hook_h + groove_extra, hook_z0, hook_base + 0.2).translate((0, 0, 0)))
shell = shell.cut(blk(0, -outer_h/2 + 0.5, seam_z - 1.0, seam_z + 1, 14, 2.0))      # pry notch bottom
shell = shell.cut(blk(-25, outer_h/2 - 0.5, seam_z - 1.0, seam_z + 1, 14, 2.0))     # pry notch top

# ============================================================
# LID FEATURES (built in assembly frame, flipped for printing)
# ============================================================
lid_asm = lid.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, total_depth))

# Feather posts + split pegs (components face the lid)
for (u, v) in fe_holes:
    x, y = fe_x(v), fe_y(u)
    post = cyl(x, y, fe_face_z, seam_z + 0.1, 5.0)
    stem = cyl(x, y, fe_face_z - fe_pcb_t - peg_head_h, fe_face_z + 0.1, peg_d)
    head = (cq.Workplane("XY").workplane(offset=fe_face_z - fe_pcb_t - peg_head_h).center(x, y)
            .circle(peg_d/2 - 0.3).workplane(offset=peg_head_h).circle(peg_head_d/2).loft())
    slot = blk(x, y, fe_face_z - fe_pcb_t - peg_head_h - 0.1, fe_face_z - 0.5, peg_head_d + 1, peg_slot)
    lid_asm = lid_asm.union(post).union(stem.union(head).cut(slot))

# switch back-stop pins over the 4 front-button switches
for ysw in btn_pts:
    lid_asm = lid_asm.union(cyl(bx, ysw, z_bf + sw_body_h + 0.1, seam_z + 0.1, 3.0))

# lid flexure tongues pressing the Feather's reset + user switches
for (u, v) in (fe_reset, fe_user):
    x, y = fe_x(v), fe_y(u)
    yh = y - btn_press_from_hinge
    z_out = total_depth                                       # outer face
    z_tin = z_out - lid_btn_t                                 # tongue inner face
    y0, y1 = yh, yh + lid_tongue_l + slot_w
    lid_asm = lid_asm.cut(blk(x - lid_tongue_w/2 - slot_w/2, (y0+y1)/2, z_tin - 1.1 - 0, z_out + 1, slot_w, y1 - y0))
    lid_asm = lid_asm.cut(blk(x + lid_tongue_w/2 + slot_w/2, (y0+y1)/2, seam_z - 1, z_out + 1, slot_w, y1 - y0))
    lid_asm = lid_asm.cut(blk(x, yh + lid_tongue_l + slot_w/2, seam_z - 1, z_out + 1, lid_tongue_w + 2*slot_w, slot_w))
    lid_asm = lid_asm.cut(blk(x, (y0+y1)/2, seam_z - 1, z_tin, lid_tongue_w + 2*slot_w, y1 - y0))
    nub_bottom = fe_face_z + fe_sw_h + 0.3
    lid_asm = lid_asm.union(cyl(x, y, nub_bottom, z_tin + 0.1, 3.0))
    lid_asm = lid_asm.cut(cq.Workplane('XY').workplane(offset=z_out - dish_depth).center(x, y).polygon(36, dish_d - 1.3).extrude(dish_depth + 1))

# USB cutout in lid skirt + snap tabs
lid_asm = lid_asm.cut(usb_cut)
for side, c in tabs:
    # slits free a vertical arm; bump on outer face near the free end
    if side in ("+X", "-X"):
        sgn = 1 if side == "+X" else -1
        xs = sgn * (sk_o_w/2 - skirt_t/2)
        for d in (-1, 1):
            lid_asm = lid_asm.cut(blk(xs, c + d*(tab_w/2 + tab_slit/2), skirt_z0 - 0.1, seam_z + 0.05, skirt_t + 0.2, tab_slit))
    else:
        sgn = 1 if side == "+Y" else -1
        ys = sgn * (sk_o_h/2 - skirt_t/2)
        for d in (-1, 1):
            lid_asm = lid_asm.cut(blk(c + d*(tab_w/2 + tab_slit/2), ys, skirt_z0 - 0.1, seam_z + 0.05, tab_slit, skirt_t + 0.2))
    lid_asm = lid_asm.union(wedge(side, c, hook_h, hook_z0, hook_base))
lid_asm = lid_asm.cut(blk(0, -outer_h/2 + 0.5, seam_z - 1.0, seam_z + 1, 14, 2.0))
lid_asm = lid_asm.cut(blk(-25, outer_h/2 - 0.5, seam_z - 1.0, seam_z + 1, 14, 2.0))

lid = lid_asm.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, total_depth))   # back to print orientation

# ============================================================
# EXPORT
# ============================================================
cq.exporters.export(shell, "case_shell.stl", tolerance=0.01, angularTolerance=0.1)
cq.exporters.export(lid, "case_lid.stl", tolerance=0.01, angularTolerance=0.1)
cq.exporters.export(shell.union(lid_asm), "case_assembly.stl", tolerance=0.01, angularTolerance=0.1)
cq.exporters.export(shell, "case_shell.step"); cq.exporters.export(lid, "case_lid.step")
print(f"Outer {outer_w:.1f} x {outer_h:.1f} x {total_depth:.1f} mm; PCB back z={front_z:.1f}; "
      f"Feather face z={fe_face_z:.1f}; USB centre x={usb_x:.1f} z={usb_z:.1f}")
