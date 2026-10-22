import math
import cadquery as cq
from shapely.geometry import box as sbox, LineString, Polygon as SPoly
from shapely.ops import unary_union

# ============================================================
# PARAMETERS - Sharp 2.7" memory display (portrait) + Feather nRF52840 Sense (4516) + 55x40x10 LiPo
# Rev 3: D-pad + A/B (6 tact switches) below the screen, all electronics in the SHELL, plain lid.
#
# Rev 6: no hooks/clips inside the shell (a catch that stops a part moving toward the lid has its catch face at the bed = an overhang
# in the air). Boards are HEAT-STAKED (printed pins melted over with an iron), the battery sits in a cradle with foam tape, and the lid
# is the only snap joint. printcheck.py proves the print rules on the exported STL files.
#
# Print orientation (Bambu A1, PETG, 0.2 layers, no supports):
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
batt_edge_r = 6.0     # ASSUMED (wrapped tubular cell): the two long edges (along X) are rounded with this radius; the cradle matches it
batt_slack_x = 1.5    # mm - length slack (cells vary, leads)
batt_slack_y = 0.5    # mm - width slack
tape_t = 0.8          # mm - double-sided foam tape (VHB type) on the +X end face of the cell; the shelves under the cell carry it too
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
skirt_h = 10.0        # rebate depth in the shell; also sets the free length of the snap fingers (strain <= 1 %)
skirt_gap = 0.2       # lid skirt is this much shorter than the rebate (the lid seats on the rim top, not on the step)
skirt_t = 1.2         # lid skirt wall thickness
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
fl_ch = 1.1           # 45 deg chamfer under the flange rim (prints head down): the flat part left is fl_off - fl_ch <= 0.4
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
cup_o = 7.9           # cup outer size (wedge sides, case X): walls 0.8 = two lines, keeps the wedge strain under 1 %
sw_leg_w = 6.5        # mm - switch across its legs (legs bent flush), case Y
plunger_hole_d = 4.0
lip_p, lip_len, lip_w = 0.25, 0.9, 3.0   # switch retention wedge (0.25: the cup wall flexes across layers, strain table)
deck_fit = 0.3        # deck outline clearance to the walls
deck_peg_y, deck_peg_x = 10.6, 3.0     # four deck stake pins at x = +- deck_peg_x, band_cy +- deck_peg_y
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
usb_hole_w, usb_hole_h, usb_ch = 11.5, 7.0, 2.5

# Heat-staked boards (replace the split pegs): printed pin through the board hole, melted over into a mushroom head with an iron
hole_d = 2.5          # board holes (display PCB, Feather, deck)
pin_d, pin_prot, pin_tip_ch, pin_fil = 2.2, 3.5, 0.3, 0.5   # pin dia, height above the board's top face, tip chamfer, root fillet
rec_d, rec_h = 3.25, 0.5                                      # recess round the pin root: the fillet lives in it, the board sits on the rim
head_d, head_h = 4.2, 1.2                                     # formed head (dome): overlap (4.2 - 2.5) / 2 = 0.85 mm round the hole
env_d, env_h = 4.5, 1.5                                       # head envelope kept clear of everything
iron_d, iron_h = 5.0, 8.0                                     # soldering-iron access cylinder above the head

# Display window
glass_w, glass_h = 63.5, 41.2
active_w, active_h = 58.8, 35.3
win_margin = 0.3
ffc_w, ffc_y0, ffc_y1 = 18.0, -19.7, -24.4        # Eagle frame (y along the short side)
peg_xy = (29.21, 25.4)
disp_ear = 6.4

# Lid snap fingers (the only snap joint). The lid prints face down, the skirt rises: a finger bends ACROSS the layers, so it is long and
# tapered (thick root, thin tip) to stay under 1 % strain. Catch face 45 deg (no overhang), lead-in 36 deg.
tab_w, tab_slit = 14.0, 1.0     # wide tabs near the stiff corners; slit width (rounded end, r = slit / 2)
tab_root_u, tab_tip_u = 1.4, 0.6    # finger thickness inward of the skirt outer face at the root / at the tip (inner face is slanted)
tab_bulge = 0.2                 # finger outer face stands this far out of the skirt face (rim gap fit 0.25 -> 0.05)
hook_depth = 0.35               # engagement: hook tip beyond the rim's inner face
hook_tip_h, hook_lead = 0.2, 0.55    # vertical tip face, lead-in height
hook_clear = 0.15               # groove clearance above / beside the hook
root_fil = 0.5                  # fillet between finger and plate (outer face, the tension side)
skirt_ch = 0.6                  # lead-in chamfer on the outer edge of the skirt tip (the lid hinges in about the top rim)
rim_relief = 0.6                # 45 deg relief on the rim's inner top edge (room for the root fillet)
strain_margin = 0.10            # mm added to the nominal finger deflection (print scatter, a lid pressed to one side)
wedge_margin = 0.05             # same for the switch wedges (the switch body is a known +-0.05)

# Battery cradle (rev 6): corner blocks + end walls matching the rounded cell, 45 deg undersides (they hang over the display pocket)
cr_wall = 2.0         # mm - wall thickness round the cell (Y blocks)
cr_len = 7.0          # mm - how far the corner blocks reach in from the side walls
cr_top_off = 1.0      # mm - cradle top below the lid skirt's lower edge
batt_pad = 0.5        # min clearance cell top - lid ribs
# Lid ribs (inner face): full height where there is room, low over the cell and the Feather, none over the wire channel and piezo
rib_w, rib_h, rib_low, rib_pitch, rib_inset = 1.2, 2.5, 1.4, 20.0, 2.5
# Rigid tongues at the top end of the lid (slide into a groove under a ledge in the top wall); the rest is snap tabs
tg_w, tg_prot, tg_h, tg_depth, tg_z = 15.0, 0.8, 1.2, 0.75, 3.3
tg_lip = 0.2         # flat lip of the tongue hook / of the groove roof (max free cantilever), then 45 deg   # width, hook out of the skirt, hook height, groove depth (flat roof = 0.75 overhang), hook top below the seam

# Strap / cord tunnel through the bottom corner
strap_slot_w, strap_slot_h = 3.2, 10.4
strap_tip = 5.0       # solid bar between the slot and the outer corner (rev 5: 2.7; it bends across the layers, strength ~ t^2)
strap_z0 = 2.0
strap_r = 1.0         # rounding of the tunnel's inner edges (cross-section corners and the two mouths)

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
outer_w = inner_w + 2 * wall
outer_h = inner_h + 2 * wall
z_pcb_front = lip_t + window_t + glass_t
front_z = z_pcb_front + pcb_t                   # PCB back plane
z_bf = z_pt + sw_plunger                        # switch body front face
z_bb = z_bf + sw_body_h                         # switch body back face
fe_floor_z = z_bb + sw_leg_clear                # Feather PCB underside
fe_top_z = fe_floor_z + fe_pcb_t                # Feather component face
led_top = front_z + comp_zone
# Depth: the deeper of the Feather stack (button band) and the battery stack (display + 13 mm cell); the lid ribs sit above both.
shell_depth = max(fe_top_z + fe_comp_h + batt_pad + rib_low, led_top + batt_z + batt_pad + rib_low)
total_depth = shell_depth + lid_t
seam_z = shell_depth
skirt_z0 = seam_z - skirt_h                     # bottom of the rebate (the step the lid does not touch)
skirt_len = skirt_h - skirt_gap                 # lid skirt length
z_tip = seam_z - skirt_len                      # lid skirt tip (assembly frame)
cup_top = z_bb + 1.0
cell_pk_x = batt_x + batt_slack_x + tape_t      # cradle pocket along X (tape sits on the +X end)
cell_pk_y = batt_y + batt_slack_y               # cradle pocket along Y
assert led_top + batt_z + batt_pad <= seam_z - rib_low
assert fe_top_z + fe_comp_h + batt_pad <= seam_z - rib_low
cr_top = skirt_z0 - cr_top_off                  # cradle top (below the rebate step / lid skirt)

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

# --- heat-stake pins: printed pin through the board hole, melted over into a mushroom head with a soldering iron ---
def stake_pin(x, y, z_surf, z_top):
    """printed pin: dia pin_d, 0.3 tip chamfer, r 0.5 root fillet at the floor of a recess that starts at z_surf - rec_h"""
    r, zb = pin_d / 2, z_surf - rec_h
    prof = (cq.Workplane("XZ").moveTo(0, zb - 0.1).lineTo(r + pin_fil, zb - 0.1).lineTo(r + pin_fil, zb)
            .radiusArc((r, zb + pin_fil), pin_fil).lineTo(r, z_top - pin_tip_ch).lineTo(r - pin_tip_ch, z_top).lineTo(0, z_top).close())
    return prof.revolve(360, (0, 0, 0), (0, 1, 0)).translate((x, y, 0))

def stake(body, x, y, z_surf, z_board_top):
    """recess in the surface the board rests on (the fillet lives in it, the board sits on its rim) + pin pin_prot above the board top"""
    return body.cut(cyl(x, y, z_surf - rec_h, z_surf + 0.01, rec_d)).union(stake_pin(x, y, z_surf, z_board_top + pin_prot))

# stake sites: (name, x, y, z of the surface the board rests on, z of the board's top face)
disp_pins = [(f"display {a}{b}", pcb_dx + sx * peg_xy[1], disp_cy + sy * peg_xy[0], z_pcb_front, front_z)
             for sx, a in ((-1, "-X"), (1, "+X")) for sy, b in ((-1, "-Y"), (1, "+Y"))]
deck_pins = [(f"deck {i}", sx * deck_peg_x, band_cy + sy * deck_peg_y, z_deck, z_bf)
             for i, (sx, sy) in enumerate(((-1, -1), (-1, 1), (1, -1), (1, 1)))]
fe_pins = [(f"feather {i}", *fe_xy(u, v), fe_floor_z, fe_top_z) for i, (u, v) in enumerate(fe_holes)]
all_pins = disp_pins + deck_pins + fe_pins

# --- controls: openings = head outline + op_clr straight through the plate; deck posts + stake pins and rails ---
def plate_controls(body):
    """front-plate features of the control band (also used for the test plate)"""
    body = body.cut(cross(dp_h + op_clr, dp_w + op_clr, -1, plate_t + 0.1, dp_r + op_clr, dp_cx, band_cy))
    for k in ("A", "B"):
        bx, by = buttons[k]
        body = body.cut(cyl(bx, by, -1, plate_t + 0.1, ab_d + 2 * op_clr))
    for (_, x, y, zs, zt) in deck_pins:                    # four posts carry the deck, stake pins go through its holes
        body = body.union(cyl(x, y, plate_t - 0.1, zs, post_d))
        body = stake(body, x, y, zs, zt)
    for sx in (-1, 1):                                    # four corner rails: the deck rests on them
        for sy in (-1, 1):
            body = body.union(blk(sx * (inner_w / 2 - rail_w / 2 + 0.05), band_cy + sy * 10.0, plate_t - 0.1, z_deck,
                                  rail_w, 5.0))
    return body

# ---------- wall features first (the seam test is cut from this state) ----------
# --- strap / cord tunnel through the bottom corner: solid corner lug + 45 degree slot, rounded inner edges ---
cxm, cym = -outer_w / 2, -outer_h / 2
sq2 = 2 ** 0.5
tip_d = 2.07 + strap_tip + strap_slot_w / 2                  # diagonal distance corner -> slot centre line
fill = (tip_d + strap_slot_w / 2 + 1.8) * sq2                # leg length of the solid triangle
lug_top = skirt_z0 - 0.2                                     # clear of the lid fingers
tri = (cq.Workplane("XY").workplane(offset=plate_t - 0.1)
       .polyline([(cxm, cym), (cxm + fill, cym), (cxm, cym + fill)]).close().extrude(lug_top - plate_t + 0.1))
shell = shell.union(tri.intersect(cav_band))
slot = (cq.Workplane("XY").box(40, strap_slot_w, strap_slot_h, centered=(True, True, False)).edges("|X").fillet(strap_r)
        .translate((0, 0, strap_z0)).rotate((0, 0, 0), (0, 0, 1), -45)
        .translate((cxm + tip_d / sq2, cym + tip_d / sq2, 0)))
shell = shell.cut(slot)
_ex = sq2 * tip_d                                            # the slot leaves the -X face at y = cym + _ex, the -Y face at x = cxm + _ex
_zt = strap_z0 + strap_slot_h + 1
shell = shell.edges(cq.selectors.BoxSelector((cxm - 0.1, cym + _ex - 3.2, 0.5), (cxm + 0.1, cym + _ex + 3.2, _zt))).fillet(strap_r)
shell = shell.edges(cq.selectors.BoxSelector((cxm + _ex - 3.2, cym - 0.1, 0.5), (cxm + _ex + 3.2, cym + 0.1, _zt))).fillet(strap_r)

# --- USB opening in the +X wall at the Feather connector (flat 45 deg top = no wide bridge) ---
usb_x = outer_w / 2 - wall / 2
usb_y = fe_xy(0, fe_usb_v)[1]
usb_z = fe_top_z + fe_usb_zc
usb_cut = (cq.Workplane("XY").box(wall + 2, usb_hole_w, usb_hole_h)
           .edges("|X and >Z").chamfer(usb_ch).translate((usb_x, usb_y, usb_z)))
shell = shell.cut(usb_cut)

# --- buzzer: PS1240-type piezo, sound face against the inside of the TOP wall (axis along Y), glued in a cradle ---
# Top wall = away from the strap and the thumbs; the grille is centred on the case axis, nothing else is on that wall.
# The arms stop at 31 deg above the axis: higher up the bore would be a roof steeper than 45 deg.
buz_x = 0.0
y_top = inner_h / 2
cr_w = piezo_d + 2 * 1.2 + 0.3
buz_arm_top = buz_z + 3.2
prof = [(y_top + 0.1, front_z), (y_top + 0.1, buz_arm_top), (y_top - piezo_cup_h, buz_arm_top),
        (y_top - piezo_cup_h, front_z + piezo_cup_h)]          # underside at 45 deg: no overhang
cradle = cq.Workplane("YZ").polyline(prof).close().extrude(cr_w / 2, both=True).translate((buz_x, 0, 0))
bore = (cq.Workplane("XZ").center(buz_x, buz_z).circle((piezo_d + 0.3) / 2).extrude(-(piezo_cup_h + 1.0))
        .translate((0, y_top - piezo_cup_h, 0)))
shell = shell.union(cradle).cut(bore)
for i in range(snd_slots):
    xo = buz_x + (i - (snd_slots - 1) / 2) * snd_pitch
    shell = shell.cut(blk(xo, y_top + wall / 2, buz_z - 3.75, buz_z + 3.75, snd_w, wall + 1.5))

# --- lid snap grooves + pry notches + rim relief ---
sk_o_w, sk_o_h = outer_w - 2 * (wall - skirt_t) - 2 * fit, outer_h - 2 * (wall - skirt_t) - 2 * fit    # lid skirt outer faces
# Snap fingers: near the stiff corners. Top end: rigid tongues (below). Seam y positions measured in case frame.
_tab_end = sk_o_h / 2 - 11.3                          # tab centre: 3.5 mm of straight wall left to the corner
_fe_y1 = fe_cy + feather_w / 2                         # the +X lower finger starts above the Feather's end edge
tabs = [("-Y", 0.0),
        ("-X", -_tab_end), ("-X", _tab_end),
        ("+X", _tab_end), ("+X", _fe_y1 + 2.0 + tab_slit + tab_w / 2)]
def side_prism(side, c, pts, width):
    """prism over pts = [(v, z)] (v outward from the lid skirt's outer face, z assembly frame), width along the wall centred on c"""
    prof = cq.Workplane("XZ").polyline(pts).close().extrude(width / 2, both=True)
    if side == "+X": return prof.translate((sk_o_w / 2, c, 0))
    if side == "-X": return prof.rotate((0, 0, 0), (0, 0, 1), 180).translate((-sk_o_w / 2, c, 0))
    if side == "+Y": return prof.rotate((0, 0, 0), (0, 0, 1), 90).translate((c, sk_o_h / 2, 0))
    return prof.rotate((0, 0, 0), (0, 0, 1), -90).translate((c, -sk_o_h / 2, 0))
# hook (v from the skirt outer face): 45 deg catch face from the finger face down to the tip, short vertical tip, lead-in back
hk_tip_v = fit + hook_depth                                       # tip: hook_depth beyond the rim's inner face
hk_run = hk_tip_v - tab_bulge                                     # horizontal run of the catch face = its vertical drop (45 deg)
hk_zc = z_tip + 0.1 + hk_run + hook_tip_h + hook_lead             # catch face top (at the finger face)
hook_pts = [(tab_bulge - 0.05, hk_zc), (hk_tip_v, hk_zc - hk_run), (hk_tip_v, hk_zc - hk_run - hook_tip_h),
            (tab_bulge - 0.05, hk_zc - hk_run - hook_tip_h - hook_lead)]
# groove in the shell wall: the hook plus hook_clear above and beside it, 45 deg roof parallel to the catch face (no overhang)
g_deep = hk_tip_v + hook_clear
g_in = fit - 0.6                                                  # starts inside the cavity
g_floor = hk_zc - hk_run - hook_tip_h - 0.1
groove_pts = [(g_in, hk_zc + hook_clear + (tab_bulge - g_in)), (g_deep, hk_zc + hook_clear - (g_deep - tab_bulge)),
              (g_deep, g_floor), (g_in, g_floor)]
for side, c in tabs:
    shell = shell.cut(side_prism(side, c, groove_pts, tab_w + 0.6))
tg_xs = (-17.5, 17.5)                                 # tongue centres: either side of the piezo notch in the top skirt
y_rebate = outer_h / 2 - (wall - skirt_t)             # rebate face of the top wall
tg_z_top, tg_z_bot = seam_z - tg_z, seam_z - tg_z - tg_h     # hook (lid) z range
# groove under a ledge: flat roof for tg_lip from the mouth, then 45 deg down to the groove bottom (no free cantilever above 0.4 mm)
tg_roof = tg_z_top + 0.3
tg_groove = [(y_rebate - 0.3, tg_roof), (y_rebate + tg_lip, tg_roof), (y_rebate + tg_depth, tg_roof - (tg_depth - tg_lip)),
             (y_rebate + tg_depth, tg_z_bot - 0.4), (y_rebate - 0.3, tg_z_bot - 0.4)]
for xc in tg_xs:
    shell = shell.cut(cq.Workplane("YZ").polyline(tg_groove).close().extrude((tg_w + 0.6) / 2, both=True).translate((xc, 0, 0)))
def pry(lid_or_shell):
    s = lid_or_shell
    s = s.cut(blk(0, -outer_h / 2 + 0.5, seam_z - 1.0, seam_z + 1, tab_w, 2.0))               # bottom tab
    return s.cut(blk(-outer_w / 2 + 0.5, -_tab_end, seam_z - 1.0, seam_z + 1, 2.0, tab_w))   # lower -X tab
shell = pry(shell)
# the rim's inner top edge gets a 45 deg relief (room for the finger root fillet of the lid)
_rb_w, _rb_h = outer_w - 2 * (wall - skirt_t), outer_h - 2 * (wall - skirt_t)
relief = (cq.Workplane("XY").workplane(offset=seam_z - rim_relief).sketch().rect(_rb_w, _rb_h).vertices()
          .fillet(max(0.1, corner_r - (wall - skirt_t))).finalize().extrude(rim_relief + 0.01, taper=-45))
shell = shell.cut(relief)
shell_rim = shell          # wall features only: the seam test is cut from this

# ---------- interior: display stakes, controls, Feather posts, battery cradle ----------
for (_, x, y, zs, zt) in disp_pins:
    shell = stake(shell, x, y, zs, zt)
shell = plate_controls(shell)
for (_, x, y, zs, zt) in fe_pins:
    shell = shell.union(cyl(x, y, plate_t - 0.1, zs, post_d))
    shell = stake(shell, x, y, zs, zt)

# --- battery cradle (rev 6): end walls + corner blocks hugging the rounded cell, tape on the +X end ---
cp_y0 = y_band_top + wire_gap                        # cell pocket in Y (the cell is assumed to have r = batt_edge_r long edges)
cp_y1 = cp_y0 + cell_pk_y
b_y0, b_y1 = cp_y0, cp_y1
cp_W = inner_w / 2
cp_hx = cell_pk_x / 2
cp_pk_r = batt_edge_r + 0.25
pocket = (cq.Workplane("YZ").center((cp_y0 + cp_y1) / 2, led_top - 0.25 + (batt_z + 2.25) / 2).rect(cell_pk_y, batt_z + 2.25)
          .extrude(cp_hx, both=True).edges("|X").fillet(cp_pk_r))
def under45(sx, x_far):
    """prism that trims the underside to 45 deg: everything below z = front_z + (W - |x|), for |x| from x_far to W + 0.3 (side sx)"""
    pts = [(x_far, front_z - 1.0), (cp_W + 0.3, front_z - 1.0), (cp_W + 0.3, front_z - 0.3), (x_far, front_z + (cp_W - x_far))]
    return cq.Workplane("XZ").polyline([(sx * px, pz) for px, pz in pts]).close().extrude(80, both=True)
sh_t, sh_over = 0.4, 0.2                              # shelf under the cell: thickness, free overhang beyond the 45 deg line
z_sh = led_top - tape_t                                # shelf top: the tape lifts the cell to led_top
x_u = cp_W - (z_sh - sh_t - front_z)                     # where the 45 deg underside reaches the shelf underside
cradle_b = None
for sx in (-1, 1):
    wall_pts = [(cp_W + 0.3, front_z), (cp_W + 0.3, cr_top), (cp_hx, cr_top), (cp_hx, z_sh), (x_u - sh_over, z_sh), (x_u - sh_over, z_sh - sh_t),
                (x_u, z_sh - sh_t), (cp_W, front_z)]
    ew = (cq.Workplane("XZ").polyline([(sx * px, pz) for px, pz in wall_pts]).close()
          .extrude((cp_y1 + cr_wall - (cp_y0 - cr_wall)) / 2, both=True).translate((0, (cp_y0 + cp_y1) / 2, 0)))
    cradle_b = ew if cradle_b is None else cradle_b.union(ew)
    for (ya, yb) in ((cp_y0 - cr_wall, cp_y0 + 2.5), (cp_y1 - 2.5, cp_y1 + cr_wall)):
        xa, xb = sorted((sx * (cp_W - cr_len), sx * (cp_W + 0.3)))
        blkc = blk((xa + xb) / 2, (ya + yb) / 2, front_z, cr_top, xb - xa, yb - ya).cut(pocket).cut(under45(sx, cp_W - cr_len - 1.0))
        cradle_b = cradle_b.union(blkc)
# the iron must reach the display stakes with the cradle in place (the cell goes in afterwards)
for (_, x, y, zs, zt) in disp_pins:
    cradle_b = cradle_b.cut(cyl(x, y, zt - 0.05, zt + 40, iron_d))
shell = shell.union(cradle_b)

# ============================================================
# LID (plain plate + skirt, built in the assembly frame, flipped for printing)
# ============================================================
lid_plate = (cq.Workplane("XY").box(outer_w, outer_h, lid_t, centered=(True, True, False))
             .edges("|Z").fillet(corner_r).faces("<Z").edges().chamfer(front_chamfer))
skirt_out = (cq.Workplane("XY").workplane(offset=lid_t)
             .box(sk_o_w, sk_o_h, skirt_len, centered=(True, True, False))
             .edges("|Z").fillet(max(0.1, corner_r - (wall - skirt_t) - fit)).faces(">Z").edges().chamfer(skirt_ch))
skirt_in = (cq.Workplane("XY").workplane(offset=lid_t)
            .box(sk_o_w - 2 * skirt_t, sk_o_h - 2 * skirt_t, skirt_len + 1, centered=(True, True, False))
            .edges("|Z").fillet(0.5))
lid = lid_plate.union(skirt_out.cut(skirt_in))
lid_asm = lid.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, total_depth))

# notches in the skirt: USB plug, piezo disc, and the Feather's end edge (the skirt reaches below the board top now)
lid_asm = lid_asm.cut(usb_cut)
lid_asm = lid_asm.cut(blk(buz_x, y_top + 0.5, z_tip - 0.1, seam_z + 0.1, cr_w + 2.0, 6))
lid_asm = lid_asm.cut(blk(sk_o_w / 2 - 1.0, (-outer_h / 2 + _fe_y1 + 0.6) / 2, z_tip - 0.1, fe_top_z + 0.6, 4.0, _fe_y1 + 0.6 + outer_h / 2))

def place(local, side, c):
    """local solid (x = outward from the lid skirt's outer face, y = along the wall, z = assembly z) onto a wall, centred on c"""
    if side == "+X": return local.translate((sk_o_w / 2, c, 0))
    if side == "-X": return local.rotate((0, 0, 0), (0, 0, 1), 180).translate((-sk_o_w / 2, c, 0))
    if side == "+Y": return local.rotate((0, 0, 0), (0, 0, 1), 90).translate((c, sk_o_h / 2, 0))
    return local.rotate((0, 0, 0), (0, 0, 1), -90).translate((c, -sk_o_h / 2, 0))

# finger geometry (local frame v = outward). Outer face stands tab_bulge out of the skirt, inner face is slanted: tab_root_u at the plate,
# tab_tip_u at the tip; a root fillet root_fil on the outer (tension) face; slits with a round end that reaches into the plate.
fin_pts = [(tab_bulge - skirt_ch, z_tip), (tab_bulge, z_tip + skirt_ch)]
fin_pts += [(tab_bulge + root_fil - root_fil * math.cos(math.radians(a)), seam_z - root_fil + root_fil * math.sin(math.radians(a)))
            for a in range(0, 91, 15)]
fin_pts += [(tab_bulge + root_fil, seam_z + 0.05), (-tab_root_u, seam_z + 0.05), (-tab_tip_u, z_tip)]
def side_poly(side, c, pts, width):
    return place(cq.Workplane("XZ").polyline(pts).close().extrude(width / 2, both=True), side, c)
def slit_tool(yc):
    """stadium slot (round top end reaching tab_slit / 2 into the plate), through the skirt thickness"""
    z_lo, z_hi = z_tip - 1.0, seam_z + tab_slit / 2
    return (cq.Workplane("YZ").workplane(offset=-(tab_root_u + 0.3)).center(yc, (z_lo + z_hi) / 2)
            .slot2D(z_hi - z_lo, tab_slit, 90).extrude(tab_root_u + 1.1))
for side, c in tabs:
    for d in (-1, 1):
        lid_asm = lid_asm.cut(place(slit_tool(d * (tab_w / 2 + tab_slit / 2)), side, c))
    lid_asm = lid_asm.cut(place(blk(-0.5, 0, z_tip - 0.1, seam_z, skirt_t + 1.0, tab_w), side, c))   # the finger is rebuilt below
    lid_asm = lid_asm.union(side_poly(side, c, fin_pts, tab_w))
    lid_asm = lid_asm.union(side_poly(side, c, hook_pts, tab_w))
# rigid tongues: hooks out of the (unslotted) top skirt into the grooves of the top wall
tongue_hooks = None
for xc in tg_xs:
    pts = [(0, tg_z_bot), (tg_prot * 0.4, tg_z_bot), (tg_prot, tg_z_bot + 0.4), (tg_prot, tg_z_top - (tg_prot - tg_lip)), (tg_lip, tg_z_top), (0, tg_z_top)]   # flat lip, then 45 deg catch; lower outer corner chamfered
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
z_lo_zone = unary_union([sbox(-40, -80, 40, y_band_top), sbox(-40, b_y0 - cr_wall, 40, b_y1 + cr_wall)])
z_free = unary_union([sbox(-40, y_band_top, 40, b_y0 - cr_wall),                                  # wire channel
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
    fl = cross(dp_h + fl_off, dp_w + fl_off, dp_th, dp_th + fl_t, dp_r + fl_off).faces("<Z").chamfer(fl_ch)
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
    fl = (cq.Workplane("XY").workplane(offset=ab_th).circle(ab_d / 2 + fl_off).extrude(fl_t).faces("<Z").chamfer(fl_ch))
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
for (_, x, y, zs, zt) in deck_pins:
    deck = deck.cut(cyl(x, y, z_deck - 1, z_bf + 1, hole_d))
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
for (_, x, y, zs, zt) in fe_pins:
    testplate = testplate.union(cyl(x, y, plate_t - 0.1, zs, post_d))
    testplate = stake(testplate, x, y, zs, zt)

# ============================================================
# SEAM TEST: the top seamtest_h mm of the shell (rim ring with the tab grooves, tongue grooves, pry notches, USB, piezo cradle).
# Printed like the shell (seam side up, cut face on the bed): fit the lid, pull, drop it before printing the whole shell.
# ============================================================
seamtest_h = skirt_h + 2.0
seamtest = shell_rim.intersect(blk(0, 0, seam_z - seamtest_h, seam_z + 1, outer_w + 2, outer_h + 2))

# ============================================================
# FLEXING FEATURES (printcheck.py prints this table; fitcheck.py asserts it)
# Strain limits: bending ACROSS the layers 1.0 %, in the layer plane 2.0 %.
# ============================================================
LIM_ACROSS, LIM_INPLANE = 1.0, 2.0

def beam_strain(L_total, h_root, h_tip, L_load, delta):
    """peak bending strain (%) at the root of a cantilever whose thickness tapers linearly h_root -> h_tip over L_total,
    loaded at L_load from the root until the load point has moved delta (Euler-Bernoulli, numeric integration)"""
    n = 4000
    x = [L_load * i / (n - 1) for i in range(n)]
    h = [h_root + (h_tip - h_root) * xi / L_total for xi in x]
    k = [12.0 * (L_load - xi) / hi ** 3 for xi, hi in zip(x, h)]                  # curvature per unit load (E b = 1)
    d1 = sum(0.5 * (k[i] * (L_load - x[i]) + k[i + 1] * (L_load - x[i + 1])) * (x[i + 1] - x[i]) for i in range(n - 1))   # tip deflection per unit load
    eps = max(ki * hi / 2 for ki, hi in zip(k, h))                                # peak strain per unit load
    return eps / d1 * delta * 100.0

L_load = seam_z - (hk_zc - hk_run / 2)           # load point: middle of the catch face
FLEX = [
    dict(part="lid", feature=f"snap finger x{len(tabs)} (L {skirt_len:.1f}, t {tab_bulge + tab_root_u:.1f} -> {tab_bulge + tab_tip_u:.1f}, hook {hook_depth} + {strain_margin})",
         layer="across layers", strain=beam_strain(skirt_len, tab_bulge + tab_root_u, tab_bulge + tab_tip_u, L_load, hook_depth + strain_margin),
         limit=LIM_ACROSS),
    dict(part="deck", feature=f"switch wedge x12 (cup wall t {(cup_o - sw_pocket) / 2:.2f}, L {z_bb + 0.4 - z_bf:.1f}, {lip_p} - {sw_clear / 2} + {wedge_margin})",
         layer="across layers", strain=1.5 * ((cup_o - sw_pocket) / 2) * (lip_p - sw_clear / 2 + wedge_margin) / (z_bb + 0.4 - z_bf) ** 2 * 100,
         limit=LIM_ACROSS),
    dict(part="lid", feature="top tongues x2 (rigid, hinge in, nothing flexes)", layer="-", strain=0.0, limit=LIM_ACROSS),
    dict(part="shell", feature="stake pins x12 (rigid; a melted head, nothing flexes)", layer="-", strain=0.0, limit=LIM_ACROSS),
]

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
