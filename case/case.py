import math
import cadquery as cq
from shapely.geometry import box as sbox, LineString, Polygon as SPoly
from shapely.ops import unary_union

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
glass_t = 2.0         # mm - glass module height above PCB front (measured: PCB 1.6, PCB + glass 3.6)
pcb_clear = 0.8       # mm - total clearance around PCB (assembly tolerance)
window_t = 0.0        # mm - optional clear window sheet (acrylic / PETG) between the bezel lip and the glass; 0 = off

# Battery 55x35x13 (measured), lying with 55 along X, 35 along Y (behind the display)
batt_x, batt_y, batt_z = 55.0, 35.0, 13.0   # measured 2026-10-22
batt_slack = 1.0      # mm - battery length tolerance between its stops (cells vary, leads)
wire_gap = 6.0        # mm - wire channel between the button band and the battery: display, battery and piezo leads to the Feather
feather_w = 22.86
feather_l = 50.8
comp_zone = 3.5       # mm - back-of-display component / solder clearance

# Shell structure
wall = 3.0
lip_t = 1.2           # bezel lip in front of the glass
corner_r = 5.0
front_chamfer = 1.5
lid_t = 2.6           # stiffer than rev 4 (2.0): a bowed lid releases the side tabs
skirt_h = 7.0
skirt_t = 1.2         # skirt = snap-tab cantilever thickness
fit = 0.25            # lid-to-shell sliding clearance

# ---- controls: ONE rocker D-pad + A/B, six 6x6 tact switches, Game-Boy-style caps ----
# Cap = head (through the front-plate opening, stands proud) + flange behind the plate (>= 1.2 mm overlap all round).
# The caps are dropped in from the inside (shell face down); a separate switch DECK (case_deck.stl) is then snapped on
# over them: it carries the 6 switch cups and traps the flanges between plate and deck.
plate_t = 2.0         # front plate thickness in the control band
dp_len, dp_arm = 22.0, 7.5          # D-pad cross: across / arm width
dp_proud = 2.5        # D-pad top above the front face
dp_pitch = 8.0        # D-pad centre to each switch (pressing stub)
ab_d, ab_proud = 9.5, 2.0           # A/B caps: diameter, height above the front face
ab_pitch, ab_tilt = 14.0, 25.0      # centre distance, tilt of the A-B line (deg); B lower-left of A seen from the front
ab_shift = -1.0       # A/B pair centre offset from the band centre (keeps A clear of the Feather posts)
op_clr = 0.3          # opening = head outline + this clearance all round
fl_ov, fl_t = 1.2, 1.2              # flange overlap beyond the opening, flange thickness
head_ch = 0.6         # 45 deg chamfer on the cap top edge (prints face down)
nub_h, nub_r = 0.95, 2.0            # D-pad pivot nub (dome) on the flange back; 0.25 below the deck = centre-press travel
boss_h, boss_d = 0.8, 6.0           # A/B travel-limit boss on the flange back
sw_size, sw_body_h, sw_plunger = 6.0, 4.0, 0.2    # TS-061 6x6, measured: body 3.9-4.0, actuator 0.2 above it (4.2 free)
sw_travel = 0.2       # mm - measured: 4.2 free, 4.0 pressed (actuator goes flush with the body)
plunger_gap = 0.1
deck_ring = 1.2       # deck ceiling thickness between the flanges and the switch bodies
z_flange_back = plate_t + fl_t
z_deck = plate_t + 2.4              # deck underside (ceiling): flange back + 1.2
# Stubs reach through the deck's plunger holes: a short plunger (TS-061) needs them longer than the 0.8 minimum.
stub_h, stub_d = max(0.8, z_deck + deck_ring - sw_plunger - plunger_gap - z_flange_back), 3.4
z_pt = z_flange_back + stub_h + plunger_gap     # plunger tip rest position
sw_leg_clear = 4.5    # mm - room behind the switch body: legs 3.5 (drawing) + 1.0 for solder and wire
sw_clear = 0.3
cup_o = 8.6           # cup outer size (wedge sides, case X)
sw_leg_w = 6.5        # mm - switch across its legs (legs bent flush), case Y
plunger_hole_d = 4.0
lip_p, lip_len, lip_w = 0.35, 0.9, 3.0   # switch retention wedge
deck_fit = 0.3        # deck outline clearance to the walls
deck_peg_y = 10.6     # deck pins at x ~ 0, band_cy +- this
rail_w = 1.3          # deck rests on 4 corner rails on the side walls

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
tab_w, tab_slit = 14.0, 0.8     # wide tabs near the stiff corners
hook_h, hook_depth = 1.2, 0.5      # hook height / depth (flat retaining face + 50 deg lead-in)
groove_extra = 0.1

# Battery rails and hold-down hooks (rev 5 drop safety: the shell holds the cell, not the lid)
led_w, stop_t = 2.5, 1.2
bh_prot, bh_gap, bh_tip_t, bh_ramp = 1.5, 0.3, 0.4, 30.0   # hook overhang (flat underside), gap over the cell, tip thickness, lead-in angle
bh_x0, bh_x1 = 25.65, 29.9      # hook post x range: on the rail, clear of the lid skirt (inner face 30.2)
batt_pad = 1.0        # mm - foam pad on the cell top (stick it on the cell)
# Lid ribs (inner face): full height where there is room, low over the cell and the Feather, none over the wire channel and piezo
rib_w, rib_h, rib_low, rib_pitch, rib_inset = 1.2, 2.5, 1.4, 20.0, 2.5
# Rigid tongues at the top end of the lid (slide into a groove under a ledge in the top wall); the rest is snap tabs
tg_w, tg_prot, tg_h, tg_depth, tg_z = 15.0, 0.8, 1.2, 1.0, 3.3   # width, hook out of the skirt, hook height, groove depth, hook top below the seam

# Strap / cord tunnel through the bottom corner
strap_slot_w, strap_slot_h = 3.2, 10.4
strap_tip = 2.7       # solid bridge between the slot and the outer corner
strap_z0 = 1.8

# Buzzer
piezo_d, piezo_h, piezo_pin_pitch = 12.2, 6.5, 5.0   # PS1240-type housed passive piezo (TDK PS1240P02BT); sound hole in the top face
piezo_cup_h = 3.0     # cradle depth along the body axis (the rest is glue + leads + 100 ohm resistor)
buz_z = 15.0          # axis height: clear of the display back components below and of the lid plate above
snd_slots, snd_pitch, snd_w = 5, 2.0, 1.0

# Front panel (rev 5): one dark screen panel around the window, picture-mat margins measured window edge to panel edge.
# The side margin (11.4) is set by the display width; the case is made as long as the margins need.
panel_m = 2.0         # mm - panel edge inside the flat front face (top and sides)
panel_top = 7.0     # mat above the window (about 0.6 x the sides)
panel_bot = 11.0    # mat below the window (about the sides: optical weight)
ctrl_gap = 5.0        # mm - panel to the control openings, and openings to the flat bottom edge: deliberate and equal
panel_r_big = 11.0    # mm - the Game-Boy nod: bottom corner on the A/B side (front view, bottom right)
strip_wall = 3.0      # mm - solid wall left between the hollow strips and the display pocket

# ============================================================
# DERIVED
# ============================================================
sw_pocket = sw_size + sw_clear
sw_pocket_y = sw_leg_w + sw_clear              # the legs side
cup_oy = sw_pocket_y + (cup_o - sw_pocket)      # same wall thickness on the legs side
disp_w = pcb_h + pcb_clear                      # display pocket along X
disp_l = pcb_w + pcb_clear                      # display pocket along Y
dp_fl_half = dp_len / 2 + op_clr + fl_ov        # D-pad flange half length (12.5)
band_core = 2 * dp_fl_half + 1.0                # what the caps and the deck need
band_h = 2 * (ctrl_gap + dp_len / 2 + op_clr - front_chamfer)    # control band inner height: opening to the flat bottom edge = ctrl_gap
assert band_h >= band_core
# Window centred on the case axis: the display PCB is shifted +X by pcb_dx (its glass sits off-centre on the PCB).
win_x_pcb = -(pcb_h / 2 - disp_ear - 1.8 - active_h / 2)    # active-area centre on the PCB, case X (PCB centred)
pcb_dx = -win_x_pcb
inner_w = disp_w + 2 * abs(pcb_dx)
_win_half = (active_w + 2 * win_margin) / 2 + 0.6      # window half height on the front face (with its chamfer)
panel_gap = ctrl_gap + dp_len / 2 + op_clr + panel_bot - band_h / 2 + (_win_half - disp_l / 2)   # strip between band and display
top_extra = panel_top + 0.5 - (disp_l / 2 - _win_half)   # empty length above the display pocket
assert top_extra > 0
inner_h = disp_l + band_h + panel_gap + top_extra
led_w = (inner_w - (batt_x - 3.7)) / 2          # battery rail width: free gap between rails 51.3 mm
outer_w = inner_w + 2 * wall
outer_h = inner_h + 2 * wall
z_pcb_front = lip_t + window_t + glass_t
front_z = z_pcb_front + pcb_t                   # PCB back plane
z_bf = z_pt + sw_plunger                        # switch body front face
z_bb = z_bf + sw_body_h                         # switch body back face
fe_floor_z = z_bb + sw_leg_clear                # Feather PCB underside
fe_top_z = fe_floor_z + fe_pcb_t                # Feather component face
led_top = front_z + comp_zone
bh_z0 = led_top + batt_z + bh_gap               # hook underside
bh_h_post = bh_tip_t + bh_prot * math.tan(math.radians(bh_ramp))   # hook height at the post
bh_top = bh_z0 + bh_h_post
# Depth: the deeper of the Feather stack (button band) and the battery stack (display + 13 mm cell).
shell_depth = max(fe_top_z + fe_comp_h + 1.0, bh_top + 0.4 + rib_low)
total_depth = shell_depth + lid_t
seam_z = shell_depth
skirt_z0 = seam_z - skirt_h
cup_top = z_bb + 1.0
assert led_top + batt_z + 0.5 <= shell_depth
assert fe_top_z + fe_comp_h + 0.5 <= shell_depth - rib_low

y_bot = -inner_h / 2                            # inner bottom wall face
y_band_top = y_bot + band_h
band_cy = y_bot + band_h / 2
disp_cy = y_band_top + panel_gap + disp_l / 2

# ============================================================
# HELPERS
# ============================================================
def blk(cx, cy, z0, z1, w, h, r=0.0):
    b = cq.Workplane("XY").workplane(offset=z0).center(cx, cy).rect(w, h).extrude(z1 - z0)
    return b.edges("|Z").fillet(r) if r else b

def cyl(cx, cy, z0, z1, d):
    return cq.Workplane("XY").workplane(offset=z0).center(cx, cy).circle(d / 2).extrude(z1 - z0)

def cross(h, w, z0, z1, off, cx=0.0, cy=0.0):
    """cross-shaped prism, half length h, arm half width w, convex corner radius off (offset2D of a sharp cross)"""
    h0, w0 = h - off, w - off
    pts = [(w0, -h0), (w0, -w0), (h0, -w0), (h0, w0), (w0, w0), (w0, h0),
           (-w0, h0), (-w0, w0), (-h0, w0), (-h0, -w0), (-w0, -w0), (-w0, -h0)]
    return (cq.Workplane("XY").workplane(offset=z0).polyline(pts).close().offset2D(off).extrude(z1 - z0)
            .translate((cx, cy, 0)))

dp_h, dp_w, dp_r = dp_len / 2, dp_arm / 2, 1.0     # head half length, arm half width, corner radius

# Control centres (case coords). Seen from the front the D-pad is on the user's left = case +X; A/B on the right.
# D-pad centre and A/B pair centre are mirror images about the case axis (x = 0).
dp_cx = inner_w / 2 - dp_fl_half - 0.4
pair_cx = -dp_cx
pair_cy = band_cy + ab_shift
_t = math.radians(ab_tilt)
ab_dx, ab_dy = ab_pitch * math.cos(_t), ab_pitch * math.sin(_t)
buttons = {
    "left":  (dp_cx + dp_pitch, band_cy),
    "right": (dp_cx - dp_pitch, band_cy),
    "up":    (dp_cx, band_cy + dp_pitch),
    "down":  (dp_cx, band_cy - dp_pitch),
    "A":     (pair_cx - ab_dx / 2, pair_cy + ab_dy / 2),
    "B":     (pair_cx + ab_dx / 2, pair_cy - ab_dy / 2),
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
# the strips above and below the display pocket are hollow down to the plate (leaves a strip_wall around the pocket)
y_strip_top = disp_cy + disp_l / 2 + strip_wall
cav_top = (cq.Workplane("XY").workplane(offset=plate_t).center(0, (y_strip_top + inner_h / 2) / 2)
           .rect(inner_w, inner_h / 2 - y_strip_top).extrude(shell_depth).edges("|Z and >Y").fillet(cav_r))
cav_band = (cq.Workplane("XY").workplane(offset=plate_t).center(0, y_bot + (band_h + panel_gap - strip_wall) / 2)
            .rect(inner_w, band_h + panel_gap - strip_wall).extrude(shell_depth).edges("|Z and <Y").fillet(cav_r))
cavity = cav_disp.union(cav_band).union(cav_top)
rebate = (cq.Workplane("XY").workplane(offset=shell_depth - skirt_h)
          .box(outer_w - 2 * (wall - skirt_t), outer_h - 2 * (wall - skirt_t), skirt_h + 1, centered=(True, True, False))
          .edges("|Z").fillet(max(0.1, corner_r - (wall - skirt_t))))
shell = outer.cut(cavity).cut(rebate)

# --- display window (centred on the case axis), glass pocket, FFC relief, PCB pocket ---
# The PCB (glass off-centre on it) is shifted +X by pcb_dx so that the active area sits at x = 0.
win_w, win_h = active_h + 2 * win_margin, active_w + 2 * win_margin     # window along X, along Y
win_r, win_ch = 1.5, 0.6                                                # window corner radius, 45 deg bezel chamfer
gl_x = -(pcb_h / 2 - disp_ear - glass_h / 2) + pcb_dx
shell = shell.cut(blk(0, disp_cy, -1, z_pcb_front + 0.1, win_w, win_h, win_r))
bez = (cq.Workplane("XY").workplane(offset=-0.01).center(0, disp_cy).sketch().rect(win_w + 2 * win_ch + 0.02, win_h + 2 * win_ch + 0.02)
       .vertices().fillet(win_r + win_ch).finalize().extrude(win_ch + 0.01, taper=45))
shell = shell.cut(bez)
if window_t > 0:        # step for the clear sheet: window + 3 mm overlap all round, 0.2 mm clearance
    shell = shell.cut(blk(0, disp_cy, lip_t, lip_t + window_t + 0.01, win_w + 6.4, win_h + 6.4, 1.5))
shell = shell.cut(blk(gl_x, disp_cy, lip_t + window_t, z_pcb_front + 0.1, glass_h + pcb_clear, pcb_w + pcb_clear))
shell = shell.cut(blk(-(ffc_y0 + ffc_y1) / 2 + pcb_dx, disp_cy, lip_t + window_t, z_pcb_front + 0.1, ffc_y0 - ffc_y1, ffc_w))
shell = shell.cut(blk(pcb_dx, disp_cy, z_pcb_front, front_z + 0.1, disp_w, disp_l, 1.0))

def split_peg(x, y, z_base, z_top):
    """upright split peg: stem from z_base to z_top, head above"""
    stem = cyl(x, y, z_base, z_top, peg_d)
    head = (cq.Workplane("XY").workplane(offset=z_top).center(x, y).circle(peg_head_d / 2)
            .workplane(offset=peg_head_h).circle(peg_d / 2 - 0.3).loft())
    slot = blk(x, y, z_top - 1.0, z_top + peg_head_h + 0.1, peg_head_d + 1, peg_slot)
    return stem.union(head).cut(slot)

for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = pcb_dx + sx * peg_xy[1], disp_cy + sy * peg_xy[0]
        shell = shell.union(split_peg(x, y, z_pcb_front - 0.1, front_z))

# --- controls: openings = head outline + op_clr straight through the plate; deck pins and rails ---
def plate_controls(body):
    """front-plate features of the control band (also used for the test plate)"""
    body = body.cut(cross(dp_h + op_clr, dp_w + op_clr, -1, plate_t + 0.1, dp_r + op_clr, dp_cx, band_cy))
    for k in ("A", "B"):
        bx, by = buttons[k]
        body = body.cut(cyl(bx, by, -1, plate_t + 0.1, ab_d + 2 * op_clr))
    for sy in (-1, 1):                                    # two pins carry the deck (split pegs)
        x, y = 0.0, band_cy + sy * deck_peg_y
        body = body.union(cyl(x, y, plate_t - 0.1, z_deck, post_d))
        body = body.union(split_peg(x, y, z_deck - 0.1, z_deck_back))
    for sx in (-1, 1):                                    # four corner rails: the deck rests on them
        for sy in (-1, 1):
            body = body.union(blk(sx * (inner_w / 2 - rail_w / 2 + 0.05), band_cy + sy * 10.0, plate_t - 0.1, z_deck,
                                  rail_w, 5.0))
    return body

z_deck_back = z_bf
shell = plate_controls(shell)

# --- Feather posts + split pegs (components face the lid) ---
for (u, v) in fe_holes:
    x, y = fe_xy(u, v)
    shell = shell.union(cyl(x, y, plate_t - 0.1, fe_floor_z, post_d))
    shell = shell.union(split_peg(x, y, fe_floor_z - 0.1, fe_top_z))

def hook_post(xc, yf, d):
    """hold-down hook of the battery: a 1.2 mm post rising from the rail beside the cell end (inner face at yf, d = +1 above
    the cell in y, -1 below), a flat-underside hook over the cell top (1.5 mm overhang) with a 30 deg lead-in on top"""
    t_ = stop_t
    pts = [(0, led_top - 0.1), (t_, led_top - 0.1), (t_, bh_top), (-bh_prot, bh_z0 + bh_tip_t), (-bh_prot, bh_z0), (0, bh_z0)]
    pts = [(yf + d * v, z) for v, z in pts]
    return (cq.Workplane("YZ").polyline(pts).close().extrude((bh_x1 - bh_x0) / 2, both=True).translate((xc, 0, 0)))

# --- battery rails + hold-down hooks (the posts are also the end stops) ---
b_y0 = y_band_top + wire_gap
b_y1 = b_y0 + batt_y + batt_slack
for sgn in (-1, 1):
    xw = sgn * (inner_w / 2 + 0.2)
    L = (b_y1 + stop_t) - (b_y0 - stop_t)
    pts = [(xw, front_z), (xw, led_top), (xw - sgn * (led_w + 0.2), led_top)]
    rail = cq.Workplane("XZ").polyline(pts).close().extrude(L).translate((0, b_y1 + stop_t, 0))
    shell = shell.union(rail)
    xs = sgn * (bh_x0 + bh_x1) / 2
    for yf, d in ((b_y0, -1), (b_y1, 1)):
        shell = shell.union(hook_post(xs, yf, d))

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

# --- buzzer: PS1240-type piezo, sound face against the inside of the TOP wall (axis along Y), glued in a cradle ---
# Top wall = away from the strap and the thumbs; the grille is centred on the case axis, nothing else is on that wall.
buz_x = 0.0
y_top = inner_h / 2
cr_w = piezo_d + 2 * 1.2 + 0.3
prof = [(y_top + 0.1, front_z), (y_top + 0.1, skirt_z0 + 3.0), (y_top - piezo_cup_h, skirt_z0 + 3.0),
        (y_top - piezo_cup_h, front_z + piezo_cup_h)]          # underside at 45 deg: no overhang
cradle = cq.Workplane("YZ").polyline(prof).close().extrude(cr_w / 2, both=True).translate((buz_x, 0, 0))
bore = (cq.Workplane("XZ").center(buz_x, buz_z).circle((piezo_d + 0.3) / 2).extrude(-(piezo_cup_h + 1.0))
        .translate((0, y_top - piezo_cup_h, 0)))
shell = shell.union(cradle).cut(bore)
for i in range(snd_slots):
    xo = buz_x + (i - (snd_slots - 1) / 2) * snd_pitch
    shell = shell.cut(blk(xo, y_top + wall / 2, buz_z - 3.75, buz_z + 3.75, snd_w, wall + 1.5))

# --- lid snap grooves + pry notches ---
sk_o_w, sk_o_h = outer_w - 2 * (wall - skirt_t) - 2 * fit, outer_h - 2 * (wall - skirt_t) - 2 * fit
# Snap tabs: fewer and wider, near the stiff corners. Top end: rigid tongues (below). Seam y positions measured in case frame.
_tab_end = sk_o_h / 2 - 11.3                          # tab centre: 3.5 mm of straight wall left to the corner
tabs = [("-Y", 0.0),
        ("-X", -_tab_end), ("-X", _tab_end),
        ("+X", _tab_end), ("+X", usb_y + usb_hole_w / 2 + 1.5 + tab_w / 2 + tab_slit + 0.5)]   # +X lower tab sits above the USB opening
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
tg_xs = (-17.5, 17.5)                                 # tongue centres: either side of the piezo notch in the top skirt
y_rebate = outer_h / 2 - (wall - skirt_t)             # rebate face of the top wall
tg_z_top, tg_z_bot = seam_z - tg_z, seam_z - tg_z - tg_h     # hook (lid) z range
for xc in tg_xs:                                      # groove under a 2.9 mm ledge, 0.4 mm free above and below the hook
    shell = shell.cut(blk(xc, y_rebate + tg_depth / 2 - 0.05, tg_z_bot - 0.4, tg_z_top + 0.4, tg_w + 0.6, tg_depth + 0.1))
def pry(lid_or_shell):
    s = lid_or_shell
    s = s.cut(blk(0, -outer_h / 2 + 0.5, seam_z - 1.0, seam_z + 1, tab_w, 2.0))               # bottom tab
    return s.cut(blk(-outer_w / 2 + 0.5, -_tab_end, seam_z - 1.0, seam_z + 1, 2.0, tab_w))   # lower -X tab
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

# notches in the skirt: USB plug, piezo disc
lid_asm = lid_asm.cut(usb_cut)
lid_asm = lid_asm.cut(blk(buz_x, y_top + 0.5, skirt_z0 - 0.1, seam_z + 0.1, cr_w + 2.0, 6))

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
# rigid tongues: hooks out of the (unslotted) top skirt into the grooves of the top wall
tongue_hooks = None
for xc in tg_xs:
    pts = [(0, tg_z_bot), (tg_prot * 0.4, tg_z_bot), (tg_prot, tg_z_bot + 0.4), (tg_prot, tg_z_top), (0, tg_z_top)]   # lower outer corner chamfered
    pts = [(sk_o_h / 2 - 0.1 + v, z) for v, z in pts]
    t_ = cq.Workplane("YZ").polyline(pts).close().extrude(tg_w / 2, both=True).translate((xc, 0, 0))
    tongue_hooks = t_ if tongue_hooks is None else tongue_hooks.union(t_)
lid_asm = lid_asm.union(tongue_hooks)
lid_asm = pry(lid_asm)

# --- inner rib grid: full height where there is room, low over the cell and the Feather, none over the wire channel and piezo ---
def poly_solid(p, z0, z1):
    out = cq.Workplane("XY").workplane(offset=z0).polyline(list(p.exterior.coords)[:-1]).close().extrude(z1 - z0)
    for h in p.interiors:
        out = out.cut(cq.Workplane("XY").workplane(offset=z0 - 1).polyline(list(h.coords)[:-1]).close().extrude(z1 - z0 + 2))
    return out
def rib_solids(geom, z0, z1):
    out = None
    for p in getattr(geom, "geoms", [geom]):
        if p.is_empty or p.area < 1.0: continue
        o = poly_solid(p, z0, z1)
        out = o if out is None else out.union(o)
    return out
_hx, _hy = sk_o_w / 2 - skirt_t - rib_inset, sk_o_h / 2 - skirt_t - rib_inset          # centre line of the frame rib
_lines = [LineString([(x, -_hy), (x, _hy)]) for x in (-rib_pitch, 0.0, rib_pitch)]
_lines += [LineString([(-_hx, y), (_hx, y)]) for y in (-2 * rib_pitch, -rib_pitch, 0.0, rib_pitch, 2 * rib_pitch)]
_lines.append(sbox(-_hx, -_hy, _hx, _hy).exterior)
grid = unary_union([ln.buffer(rib_w / 2, cap_style=2) for ln in _lines]).buffer(0.6).buffer(-0.6)   # rounded inner joins
z_lo_zone = unary_union([sbox(-40, -80, 40, y_band_top), sbox(-40, b_y0 - stop_t, 40, b_y1 + stop_t)])
z_free = unary_union([sbox(-40, y_band_top, 40, b_y0 - stop_t),                                  # wire channel
                      sbox(-(piezo_d / 2 + 2.4), y_top - piezo_h - 2.0, piezo_d / 2 + 2.4, y_top)])
ribs_hi = rib_solids(grid.difference(z_lo_zone).difference(z_free), seam_z - rib_h, seam_z + 0.1)
ribs_lo = rib_solids(grid.intersection(z_lo_zone).difference(z_free), seam_z - rib_low, seam_z + 0.1)
for r_ in (ribs_hi, ribs_lo):
    if r_ is not None: lid_asm = lid_asm.union(r_)

# --- lid inlay: wordmark flush in the back face (assembly frame: back face at z = total_depth, read from +Z) ---
inlay_t = 0.4
INLAY_FONT = "Helvetica"
CAP_RATIO = 0.72                                    # Helvetica Bold cap height / font size (measured)

def text_solid(txt, cap_h, z0=0.0):
    """bold text centred on the origin, reads normally seen from +Z, from z0 to z0 + inlay_t"""
    t = (cq.Workplane("XY").workplane(offset=z0)
         .text(txt, cap_h / CAP_RATIO, inlay_t, font=INLAY_FONT, kind="bold", halign="center", valign="center"))
    bb = t.val().BoundingBox()          # centre on the ink: halign uses the advance width (side bearings), B sat off-centre
    return t.translate(((-(bb.xmin + bb.xmax) / 2), -(bb.ymin + bb.ymax) / 2, 0))

lid_cap_h = 4.2                                    # stems ~0.8 mm = 2 lines of the 0.4 nozzle
lid_wm_y = -outer_h / 2 + front_chamfer + 5.0       # text centre 5 mm above the flat edge: spans 3.25 .. 6.75 mm
lid_inlay_asm = text_solid("XC DISPLAY", lid_cap_h, total_depth - inlay_t).translate((0, lid_wm_y, 0))
lid_asm = lid_asm.cut(lid_inlay_asm.union(lid_inlay_asm.translate((0, 0, 0.5))))
def to_lid_print(shape):
    return shape.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, total_depth))
lid = to_lid_print(lid_asm)
lid_inlay = to_lid_print(lid_inlay_asm)             # printed face down: mirrored here, reads right from the back

# ============================================================
# BUTTON CAPS (Game-Boy style: head through the opening, flange behind the plate; printed head down)
# ============================================================
def front_text(txt, cap_h, x, y):
    """bold text readable from the FRONT (case +X is on the viewer's left), centred at case (x, y), upright"""
    return text_solid(txt, cap_h).translate((-x, y, 0)).mirror("YZ")

def arrow(tip, direction, s=2.2, r=0.4):
    """solid triangle pointing along direction (dx, dy), outer tip at distance tip from the origin.
    Minkowski sum of an equilateral triangle (side s) and a disc r: 3.0 wide, 2.7 long, rounded corners (min stroke)."""
    dx, dy = direction
    t = tip - r
    pts = [(0, t), (-s / 2, t - s * math.sqrt(3) / 2), (s / 2, t - s * math.sqrt(3) / 2)]
    pr = [(px * dy + py * dx, -px * dx + py * dy) for px, py in pts]
    return cq.Workplane("XY").polyline(pr).close().offset2D(r).extrude(inlay_t)

fl_off = op_clr + fl_ov                      # flange outline beyond the head outline
dp_th = dp_proud + plate_t                   # D-pad head length (front tip z = 0 in cap coordinates)
ab_th = ab_proud + plate_t

def make_dpad_cap():
    head = cross(dp_h, dp_w, 0, dp_th, dp_r).faces("<Z").chamfer(head_ch)
    fl = cross(dp_h + fl_off, dp_w + fl_off, dp_th, dp_th + fl_t, dp_r + fl_off).faces("<Z").chamfer(0.6)
    z_fb = dp_th + fl_t
    R = (nub_r ** 2 + nub_h ** 2) / (2 * nub_h)
    nub = (cq.Workplane("XY").sphere(R).translate((0, 0, z_fb + nub_h - R))
           .intersect(blk(0, 0, z_fb - 0.05, z_fb + nub_h + 1, 8, 8)))
    cap = head.union(fl).union(nub)
    for dx, dy in ((dp_pitch, 0), (-dp_pitch, 0), (0, dp_pitch), (0, -dp_pitch)):
        cap = cap.union(cyl(dx, dy, z_fb - 0.05, z_fb + stub_h, stub_d))
    return cap

def make_ab_cap():
    head = cq.Workplane("XY").circle(ab_d / 2).extrude(ab_th).faces("<Z").chamfer(head_ch)
    fl = (cq.Workplane("XY").workplane(offset=ab_th).circle(ab_d / 2 + fl_off).extrude(fl_t).faces("<Z").chamfer(0.6))
    z_fb = ab_th + fl_t
    cap = head.union(fl).union(cyl(0, 0, z_fb - 0.05, z_fb + boss_h, boss_d))
    return cap.union(cyl(0, 0, z_fb + boss_h - 0.05, z_fb + stub_h, stub_d))    # stub past the boss (travel stop)

# Legends (coaster style): BODY-colour inlay 0.4 mm deep, flush in the flat cap top, same print frame as the cap.
cap_letter_h = 4.5                                   # cap height of A / B on the 9.5 mm caps
arrow_inset = 2.5                                    # arrow tip to the arm end
def dp_legend():
    out = None
    for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        a = arrow(dp_h - arrow_inset, d)
        out = a if out is None else out.union(a)
    return out
def pocketed(cap, legend):
    return cap.cut(legend.union(legend.translate((0, 0, -0.5))))

dp_leg, ab_leg = dp_legend(), {k: front_text(k, cap_letter_h, 0, 0) for k in ("A", "B")}
dp_cap, ab_cap_plain = pocketed(make_dpad_cap(), dp_leg), make_ab_cap()
ab_cap = {k: pocketed(ab_cap_plain, ab_leg[k]) for k in ("A", "B")}
ab_x1 = fl_off + dp_h + ab_d / 2 + 7.0               # print frame: D-pad at the origin, A and B beside it
ab_x2 = ab_x1 + ab_d + 2 * fl_off + 2.0
caps_print = dp_cap.union(ab_cap["A"].translate((ab_x1, 0, 0))).union(ab_cap["B"].translate((ab_x2, 0, 0)))
caps_inlay = dp_leg.union(ab_leg["A"].translate((ab_x1, 0, 0))).union(ab_leg["B"].translate((ab_x2, 0, 0)))
caps_asm = dp_cap.translate((dp_cx, band_cy, -dp_proud))
caps_inlay_asm = dp_leg.translate((dp_cx, band_cy, -dp_proud))
for k in ("A", "B"):
    caps_asm = caps_asm.union(ab_cap[k].translate((buttons[k][0], buttons[k][1], -ab_proud)))
    caps_inlay_asm = caps_inlay_asm.union(ab_leg[k].translate((buttons[k][0], buttons[k][1], -ab_proud)))

# ============================================================
# SWITCH DECK (snaps onto the two pins over the caps and traps the flanges; carries the six switch cups)
# printed ceiling face down
# ============================================================
tri_big = (cq.Workplane("XY").workplane(offset=z_deck - 1)
           .polyline([(cxm, cym), (cxm + fill + 1.0, cym), (cxm, cym + fill + 1.0)]).close().extrude(z_bf - z_deck + 2))
deck = blk(0, band_cy, z_deck, z_bf, inner_w - 2 * deck_fit, band_core - 2 * deck_fit, 2.0)
for (u, v) in fe_holes:
    x, y = fe_xy(u, v)
    deck = deck.cut(cyl(x, y, z_deck - 1, z_bf + 1, post_d + 0.6))
for sy in (-1, 1):
    deck = deck.cut(cyl(0.0, band_cy + sy * deck_peg_y, z_deck - 1, z_bf + 1, peg_d + 0.2))
deck = deck.cut(tri_big)
for name, (bx, by) in buttons.items():
    deck = deck.union(blk(bx, by, z_bf - 0.1, cup_top, cup_o, cup_oy))
    deck = deck.cut(cyl(bx, by, z_deck - 1, z_bf + 0.1, plunger_hole_d))
    deck = deck.cut(blk(bx, by, z_bf, cup_top + 1, sw_pocket, sw_pocket_y))
    for sgn in (-1, 1):                       # retention wedges, flat underside just behind the body
        xw = bx + sgn * sw_pocket / 2
        za = z_bb + 0.1
        pts = [(xw + sgn * 0.1, za), (xw - sgn * lip_p, za), (xw + sgn * 0.1, za + lip_len)]
        w = cq.Workplane("XZ").polyline(pts).close().extrude(lip_w).translate((0, by + lip_w / 2, 0))
        deck = deck.union(w)
    # leg slots: the legs kink out to 7.9 mm from 0.7 mm behind the body; they leave on the case +-Y sides (wedges are on +-X)
    deck = deck.cut(blk(bx, by, z_bb, cup_top + 1, sw_pocket, cup_oy + 1))

# ============================================================
# FRONT INLAY (2 layers = 0.4 mm, flush with the front face, same frame as the shell)
# One dark screen panel around the window, DMG style. No text, no lines.
# ============================================================
flat_hw, flat_hh = outer_w / 2 - front_chamfer, outer_h / 2 - front_chamfer     # flat front face half sizes
pn_x, pn_top = flat_hw - panel_m, flat_hh - panel_m
win_open_bot = disp_cy - win_h / 2 - win_ch          # window edge on the front face (with its chamfer)
pn_bot = win_open_bot - panel_bot                      # mat below the window
pn_r = max(1.0, corner_r - front_chamfer - panel_m)   # concentric with the outer corners (1.5)
panel = blk(0, (pn_top + pn_bot) / 2, 0, inlay_t, 2 * pn_x, pn_top - pn_bot)
for (cx_, cy_, r_) in ((pn_x, pn_top, pn_r), (-pn_x, pn_top, pn_r),
                       (pn_x, pn_bot, pn_r),                       # front-left (case +X)
                       (-pn_x, pn_bot, panel_r_big)):              # front-right (case -X): the nod
    panel = panel.edges(cq.selectors.NearestToPointSelector((cx_, cy_, inlay_t / 2))).fillet(r_)
# the panel runs right to the glass: its edge follows the window chamfer (a straight cut would leave a body-colour sliver)
inlay = panel.cut(bez).cut(blk(0, disp_cy, -1, inlay_t + 1, win_w, win_h, win_r))
shell_plain = shell
shell = shell.cut(inlay.union(inlay.translate((0, 0, -0.5))))

# ============================================================
# FEEL-TEST PLATE: the control band of the front plate with openings, pins, rails, Feather posts (deck + caps go with it)
# ============================================================
tp_w, tp_h = outer_w, band_core + 10.0
testplate = (blk(0, band_cy, 0, plate_t, tp_w, tp_h, 4.0).faces("<Z").edges().chamfer(0.8))
testplate = plate_controls(testplate)
for (u, v) in fe_holes:
    x, y = fe_xy(u, v)
    testplate = testplate.union(cyl(x, y, plate_t - 0.1, fe_floor_z, post_d))

# ============================================================
# SEAM TEST: the top seamtest_h mm of the shell (rim ring with the tab grooves, tongue grooves, pry notches, USB, piezo cradle).
# Printed like the shell (seam side up, cut face on the bed): fit the lid, pull, drop it before printing the whole shell.
# ============================================================
seamtest_h = 8.0
seamtest = shell.intersect(blk(0, 0, seam_z - seamtest_h, seam_z + 1, outer_w + 2, outer_h + 2))

# ============================================================
# EXPORT
# ============================================================
def stl(shape, name, dz=0.0):
    cq.exporters.export(shape.translate((0, 0, dz)) if dz else shape, name, tolerance=0.01, angularTolerance=0.1)

if __name__ == "__main__":
    stl(shell, "case_shell.stl"); stl(inlay, "case_shell_inlay.stl")
    stl(lid, "case_lid.stl"); stl(lid_inlay, "case_lid_inlay.stl")
    stl(caps_print, "case_buttons.stl"); stl(caps_inlay, "case_buttons_inlay.stl")
    stl(deck, "case_deck.stl", -z_deck); stl(testplate, "case_testplate.stl")
    stl(seamtest, "case_seamtest_shell.stl", -(seam_z - seamtest_h))
    stl(shell.union(deck).union(lid_asm).union(caps_asm).union(inlay).union(lid_inlay_asm).union(caps_inlay_asm), "case_assembly.stl")
    for shp, nm in ((shell, "case_shell"), (lid, "case_lid"), (caps_print, "case_buttons"), (deck.translate((0, 0, -z_deck)), "case_deck")):
        cq.exporters.export(shp, nm + ".step")
    print(f"Outer {outer_w:.1f} x {outer_h:.1f} x {total_depth:.1f} mm (shell {shell_depth:.1f}); band_h {band_h:.1f}")
    print(f"PCB back z={front_z:.1f}; switch body z={z_bf:.1f}-{z_bb:.1f}; Feather PCB z={fe_floor_z:.1f}-{fe_top_z:.1f}; "
          f"USB y={usb_y:.2f} z={usb_z:.2f}; deck z={z_deck:.1f}-{z_bf:.1f}")
    for k, v in buttons.items(): print(f"  {k:5s} ({v[0]:7.2f}, {v[1]:7.2f})")
