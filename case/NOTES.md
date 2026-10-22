# Enclosure notes / progress

Status: **Rev 6** (see the last section; rev 5 text follows) (see the section at the end; the text below is rev 4 unless it says otherwise). Rev 4: (rocker D-pad + A/B Game-Boy-style caps, switch deck, centred window, front inlay, PS1240 buzzer). Fitcheck passes. Not printed yet.
Source of truth: `case.py` (CadQuery, all dimensions in PARAMETERS). `python case.py` regenerates the STL/STEP files,
`python fitcheck.py` checks interference with stand-in parts, `python make_previews.py` redraws the PNGs (own numpy renderer, no OpenGL).

## Hardware
- Display: Adafruit SHARP Memory 2.7" 400x240 (#4694). PCB 63.5 x 55.88 mm, 4x 2.5 mm holes at 58.42 x 50.8. Glass ~63.5 x 41.2, active 58.8 x 35.3.
  Used in PORTRAIT: the 63.5 side is vertical, the header ear (6.4 mm) ends up on the user's right. Firmware must rotate the content (verify rotation 1 vs 3).
- MCU: Feather nRF52840 Sense (#4516), 50.8 x 22.86, micro-B at one short end, holes 2.54 / 48.26 x 2.54 / 20.32, Reset SW1 (u 11.1, v 6.22), user SW2 (u 29.93, v 5.85), JST at u~10.8 v~18.
- Battery: LiPo 55 x 35 x 13 (measured 2026-10-22; JST-PH assumed).
- 6 x TS-061 6x6 tact switches: 4.2 mm free, 4.0 pressed (0.2 mm travel, measured); body 3.9-4.0 measured, actuator 0.2 above it.

## Design (rev 4)
Outer **66.9 x 96.3 x 24.8 mm** (rev 3: 62.3 x 96.9 x 22.6). Walls 3 mm, 5 mm corner radius, 1.5 mm chamfer front/back (the one chamfer size on all outer edges).
Case frame: Z=0 front; X/Y as seen from the BACK. Seen from the front case +X is the user's left (D-pad at +X, A/B at -X).

- **Window centred**: the glass sits off-centre on the breakout PCB (ear + header side), so the PCB is shifted +X by 2.09 mm inside the case (`pcb_dx`); the case grew 4.2 mm in width (62.3 -> 66.5; length 96.9 -> 95.9 as the control band shrank 27 -> 26 mm). Side bezels 15.28 / 15.28 mm; top bezel ~5 mm cannot equal the sides (the active area is only 35.3 mm wide).
- **Controls (Game-Boy way)**: cap = head through a front-plate opening (head outline + 0.3 mm, no web, no counterbore) + flange behind the plate (1.2 mm overlap all round, 1.2 mm thick). Caps are dropped in from the inside while the shell is face down; a separate **switch deck** (`case_deck.stl`, 1.2 mm ring + cups) snaps onto 2 split pegs (x = 0, band_cy +-10.6) over them and traps the flanges, resting on 4 corner rails. The cups could not stay in the shell: they sit behind the arm ends and the flange could never pass them.
  D-pad: 22 mm cross, arms 7.5, corner radius 1.0, top 2.5 mm proud, centred at x = +17.33 (case +X = user's left). Opening arms 8.1 x 22.6. Flange arms 10.5 x 25. Four 3.4 mm stubs 0.8 long at 8 mm from the centre press the plungers (0.1 gap). Pivot: 0.9 mm dome nub (r 2.0) on the flange back, 0.3 mm below the deck ceiling.
  Kinematics (fitcheck): springs hold the flange on the plate; pressing an arm tilts the cap about the opposite opening edge; stub travel 0.1 gap + ~0.25 actuation = 0.35, the nub meets the deck at 0.51 mm (hard stop, 1.5 deg); the opposite arm lifts away so it cannot press. A flat centre press travels 0.3 mm < 0.35: no switch fires. Tune with `nub_h`.
  A/B: 9.5 mm round caps, 2.0 mm proud, 14 mm apart, line tilted 25 deg (B lower-left of A seen from the front), pair centre mirrored to the D-pad at x = -17.33, y offset -1.0 mm (A must clear the Feather posts). Opening 10.1 mm, flange 12.5 mm, 6 mm boss (0.8) acts as travel stop (0.4 mm), plunger touches the boss top (0.1 gap).
  Switch plane: plunger tip z 4.1, body 5.6-9.1, deck ceiling z 4.4, ring 1.2 mm. Wires: switch legs point to the back into the 3.5 mm gap under the Feather; **6 GPIO + GND = 7 wires** run over the cup walls.
- **Inlay**: 0.4 mm pockets in the front face (z 0-0.4) cut from the shell, same shapes exported as `case_shell_inlay.stl`: 1.0 mm frame 1.7 mm outside the window (r 3.2 / 4.2), A and B (Helvetica Bold 5 mm, cap height ~3.6), vertical wordmark XC DISPLAY (4.6 mm) in the right bezel. No D-pad arrows: no room above the up arm (1.4 mm) and the cross already reads. `inlay_check.py`: strokes >= ~0.6 mm (0.5 mm opening loses 0.8 mm2 = corner tips only), frame 1.0 mm. The shell has a 0.6 mm window chamfer and window corner radius 1.5.
- **Feather** (all electronics now in the shell, lid is a plain plate): 4 posts (4.4 mm) rise from the front plate in the button band, with 2.4 mm split snap pegs through the Feather holes
  (comps face the lid). PCB underside z 12.0, top z 13.6, USB end 0.3 mm from the +X wall. Posts instead of wall ledges: wall ledges would need unsupported 6+ mm cantilevers.
- **Battery**: lies 55 (X) x 40 (Y) behind the display next to the band, on two 2.5 mm rails with 35 deg undersides, 4 stop blocks; the lid presses it. Add foam tape (1.4 mm free in Z).
- **Reset / SW2**: pin holes through the LID (reset 1.8 mm, SW2 3.4 mm) with 4.6 mm guide tubes down to 0.8 mm above the switches, away from the flat velcro zone. Removing the back exposes both switches directly.
  Flexure nubs were dropped (lid is now plain). A different back needs its own 2 holes if reset must work with it on.
- **USB**: 11.5 x 7 mm opening (45 deg chamfered top, no wide bridge) in the +X wall at y -31.9, z 14.95; matching notch in the lid skirt.
- **Strap**: 45 degree rectangular tunnel 3.2 x 10.4 mm through a solid lug at the bottom -X corner (user's bottom right), 2.7 mm bridge to the outer corner. Fits a 10 mm strap; a 3 mm cord fits too (no separate hole).
- **Buzzer**: PS1240-type housed passive piezo (TDK PS1240P02BT 12.2 mm dia x 6.5 mm tall, pins 5.0 mm pitch on the back; `piezo_d/h/pin_pitch`) glued with a drop of CA in a cradle on the inside of the TOP wall (axis along Y, axis height z 15, sound face on the wall), 3 mm deep, underside 45 deg (no overhang), 5 slots 1.0 x 7.5 through the wall centred on the case axis. Pins/leads and a 100 ohm series resistor go on the open back (6.5 mm of body leaves ~13 mm to the battery stop). Driven from two GPIOs in anti-phase by the nRF PWM (bridge drive, no extra hardware). 18 mm notch in the lid skirt. Moved from the bottom wall: a 12.2 mm tall body there would hit the Feather.
- **Flat back**: lid outer face has no features except the 2 pin holes at the bottom edge. Flat 63.5 x 92.9 mm; hole-free 63.5 x 79.6 mm (everything above y = -33). Pry notches are only at the seam edge (top wall and left wall).
- Snap fit: lid skirt 1.2 mm thick x 7 mm free length (L/t 5.8), hook 0.5 deep x 1.2 high (50 deg lead-in, flat retaining face), **10 tabs** (top 2, bottom 2, left 3, right 3), 10 mm wide with 0.8 slits. Strain eps = 1.5 t d / L^2 = **1.8 %** (PETG limit ~2 %).
- Display window 59.4 x 35.9, glass recessed 1.2 mm behind the bezel; 4 split pegs (2.4 mm, hole 2.5) hold the display PCB.

## Print orientation (Bambu A1, PETG, 0.2 mm layers, 0.4 mm nozzle, no supports)
- **Shell**: front face down. Overhangs/bridges checked: strap slot ceiling bridge 3.2 mm; USB top is a 45 deg chamfer (flat part 6.5 mm); switch pocket wedges 0.35 mm overhang; plate openings are through holes (no bridges); the cross-shaped opening is open to the front;
  battery rails 35 deg from vertical; buzzer ring 1.0 mm cantilever; sound slots 1.0 mm bridge; hook grooves 50 deg; peg heads taper up, 0.3 mm undercut at the slot.
- **Lid**: outer face down (plate first, skirt and tubes upright). Tab hooks 50 deg both sides. Pin holes vertical. Skirt arms bend across layers: the weak direction, strain kept < 2 % and test fit.
- **Caps**: head down on the bed (front tip on the bed, 0.6 mm 45 deg chamfer on the head top edge, shallow dish 0.3 mm in the D-pad centre). The flange above has a 0.6 mm 45 deg chamfer on its underside edge; the remaining ~0.9 mm eave is a short overhang (check one). Stubs, nub and boss are on top. **Deck**: ceiling face down, cups rise (wedges 0.35 mm overhang). **Test plate**: front face down.
- Each part fits the 256 mm bed easily. Walls >= 0.85 mm everywhere (thinnest: switch pocket wall 1.15, deck ring 1.2).
- Sliding clearances 0.25 (lid), 0.1-0.3 for caps; pegs 2.4 in 2.5 holes (press/snap). **Pegs are 2.4 mm, below the 2.5 mm upright guideline** because the PCB holes are 2.5: they are short (<= 2.5 mm tall incl. head) and slotted; print the shell slow, check one.

## Assumptions to verify before printing
- ~~Glass module height above the PCB~~: measured 2026-10-22, PCB 1.6, PCB + glass 3.6 mm (`glass_t` 2.0; was guessed 2.4).
- ~~Tact switch body height~~ measured 3.9-4.0 + 0.2 actuator; actuator diameter (3.5 assumed, the 3.4 stub must not be wider), switch legs: 3.5 mm per the generic 6x6 drawing, 4.5 mm left under the Feather (`sw_leg_clear`).
- ~~Battery real size~~ 55 x 35 x 13 measured; JST polarity vs Feather, battery lead length to the Feather JST (JST sits on the Feather top edge near the USB end).
- Feather tall part height 6 mm (`fe_comp_h`), USB overmold fits 11.5 x 7.
- PS1240 piezo real size (12.2 x 6.5) and lead position; D-pad nub gap (feel); cap flange eave print quality; deck pin snap fit; inlay colour change; back-of-display component height (3.5 mm zone, assumed).
- Display PCB orientation: Eagle frame assumed to be the front view; the ear is on the user's right in portrait.
- Skirt tab stiffness; if too stiff reduce `hook_depth`.
- Press-fit of peg heads through the 2.5 mm holes.

## TODO
- [ ] Measure switch / battery (glass height done), update `case.py`
- [ ] Test print: caps feel, wedges retention, tabs, USB alignment, strap lug
- [ ] Firmware: 6 GPIO pin map, display rotation, piezo anti-phase PWM pins

## Files
- `case.py` model + export, `fitcheck.py` interference/strain, `render.py` + `make_previews.py` previews
- `case_shell.*`, `case_lid.*`, `case_buttons.*` (print), `case_assembly.stl` (view only)
- `preview_*.png`

## Measured parts, tolerances (2026-10-22)
- Display PCB 1.6, with the glass 3.6 mm (`glass_t` 2.0). Battery 55 x 35 x 13. TS-061 switches: body 3.9-4.0, actuator 0.2 above it, flush when pressed (4.2 free / 4.0 pressed). No maker drawing found (generic part).
- Depth now set by the battery stack, not the Feather: `shell_depth = max(Feather stack, display + 3.5 comp zone + 13 cell + 1.5 foam)`; outer 66.9 x 96.3 x 24.8.
- Tolerances: display pocket +0.8 (`pcb_clear`), battery 1.5 mm between its stops (`batt_slack`), 1.5 mm behind the cell (`batt_foam`),
  ~2.5 mm over the Feather parts to the lid.
- **Wire channel**: the battery starts 6 mm above the button band (`wire_gap`), a full-depth channel across the case for the display, battery and piezo leads to the Feather.
- Short plunger: the cap stubs are derived (`stub_h` 2.0) and reach through the deck's plunger holes; A/B get a stub past their 0.8 mm boss (still the travel stop, 0.4 mm).
  0.2 mm travel: D-pad nub gap 0.25 (`nub_h` 0.95) so a centre press (0.25) stays below firing (gap 0.1 + 0.2 = 0.3) and an arm reaches 0.43 at the stop. `fitcheck.py` now fails if that order breaks.
- Generic 6x6 drawing (user, 2026-10-22): actuator 3.5 mm diameter (the 3.4 stub presses the actuator only), legs 3.5 mm behind the body, kinking out to 7.9 mm from 0.7 mm behind it.
  The legs make the switch 6.5 mm across on their side even bent flush: cup pockets are 6.3 (X, wedges) x 6.8 (Y, legs), leg slots above the body on +-Y. Switches go in legs up/down. Leg room under the Feather 4.5 (legs + solder).

## Rev 5: look (2026-10-22)
Goal: a restrained two-colour consumer look (BODY light, ACCENT charcoal), still only the first-layers trick: a 0.4 mm inlay flush in pockets of the bed face, 2 filaments per part.
- **Lid**: reset and user-switch pin holes and their guide tubes removed, back fully flat. The front six keys replace them; reset now needs the lid off. Wordmark "XC DISPLAY" (Helvetica Bold, cap height 4.2 mm, accent inlay, centred, text centre 5 mm above the flat edge) in the bottom 8 mm; the rest is free for velcro. `case_lid_inlay.stl` is in the lid's print frame (mirrored about Y there, because the lid prints face down; the preview shows it reads right from the back).
- **Front**: blue frame line, vertical wordmark and A/B letters removed. One dark screen panel instead: margin m = 2.0 mm inside the flat face (outer outline minus the 1.5 mm chamfer) on top and sides, corner r 1.5 (flat-face corner 3.5 minus m, concentric), 11.0 mm below the window edge, bottom corner on the A/B side r 11, the other bottom corner r 1.5. Margins to the window: sides 11.4 mm (the bezel), top 7.0, bottom 11.0. No branding on the front.
  The panel runs right to the glass: its hole follows the window chamfer cone. A straight cut at the chamfer's outer edge would leave a 0.6 mm body-colour bevel, a hole at the window edge a 0.2 mm lip; the cone version leaves only a 0.2 mm body-colour line (the last 0.2 mm of the 0.6 mm chamfer is above the inlay layers), too fine to read as a ring.
- **Case 116.2 mm long** (rev 4: 96.3; `panel_top` 7.0, `panel_bot` 11.0, `ctrl_gap` 5.0, all window-edge-to-panel-edge or opening-to-edge): the owner chose the framed panel (2.0 mm inside the flat face), whose first margins (top 1.4 / sides 11.4 / bottom 3.5) read as a heavy U. Now sides 11.4, top 7.0, bottom 11.0, panel to the D-pad opening 5.0, D-pad opening to the flat bottom edge 5.0, big radius 11. The control band grew to 29.6 mm inner height (caps centred in it; deck and test plate keep the 26 mm core). The length added above and below the display is hollow down to the 2 mm plate with a 3 mm wall around the display pocket. The A/B pair stays 1 mm below the band centre (A must clear the Feather posts).
- **Caps**: finger dishes removed (flat tops, head chamfer 0.6 kept). Printed in ACCENT with BODY legends 0.4 mm deep (coaster style): A and B bold, cap height 4.5 mm, upright for the user (not turned with the 25 deg line); D-pad: four arrows pointing outward (equilateral, 3.0 wide x 2.7 long, corners rounded r 0.4, tip 2.5 mm from the arm end). Geometry, flange, stubs, nub, boss unchanged; fitcheck passes.
- **Min stroke**: `inlay_check.py` now checks shell, lid and caps. Sharp letter corners lose 0.01-0.07 mm2 each in a 0.6 mm opening test (the disc does not reach into a tip); a thin stroke would lose a strip, so it fails on a lost piece > 0.1 mm2. Lid wordmark stems are about 0.65-0.7 mm (Helvetica Bold at 4.2 mm cap height): thin, check the print; a larger cap height or Arial Black is the fix.
- Firmware consequence: see PLAN.md (wake from System OFF on a front key, A+B held 6 s for power off).

## Rev 5: drop safety
Reasoning first, then the numbers.
- **Battery load path.** The 55 x 35 x 13 cell (~40 g) used to rest on its rails and the lid was the only thing behind it (1.5 mm of foam). In a back-down drop the cell's inertia (several hundred g at the impact) goes into the lid, which snaps on with tabs: the lid bows, the side tabs (which release by sliding out sideways) let go, the lid opens. Now the shell holds the cell: four posts rise from the rails beside the cell ends (the old stop blocks, 1.2 mm thick, 4.25 wide, 13.3 free length) with a hook over the cell top: flat underside (1.5 mm overhang, 0.3 mm over the cell), 30 deg lead-in on top. A 45 deg underside would be easier to print but then the hook only holds by its bending stiffness (about 4 N); the flat underside loads the post in tension (~290 N) and prints as a 1.5 mm overhang, which is the one thing to check on the first shell (the hooks are not in the seam test). Insertion: posts flex 1.7 mm (strain 1.7 %, `fitcheck`), cell slack in Y 1.0 mm, overlap over the cell 0.5 to 1.5 mm. The foam pad (1 mm) goes on the cell top, 2 mm clear of its edges (the hooks).
- **Shell depth** 22.8 -> 24.7 mm: the hook top (22.87) + 0.4 + the low ribs (1.4) decide it; the Feather stack (21.7 + 1) did not. Total depth 27.3 with the 2.6 mm lid.
- **Stiffer lid.** Plate 2.0 -> 2.6 mm, inner rib grid (1.2 wide, pitch 20 mm, frame rib 2.5 mm inside the skirt so no rib touches a tab arm): 2.5 high where there is room (display / top strip), 1.4 high over the cell and the Feather (their parts stop at 21.7, the rib bottom is at 23.3), none over the wire channel (4.8 mm between the band and the cell) and none over the piezo. Joins rounded (0.6 mm closing), generated with shapely. The outer face is still flat.
- **Tabs and layers.** Tab hooks bend the skirt wall, whose layers lie in the bending plane: the tension face is across the layer lines (weakest direction). Strain is kept at 1.84 % (rev 4 value, 1.2 mm skirt, 7 mm free, 0.5 mm hook). Wider tabs (14 mm) do not change strain but spread the load; fewer (5 instead of 10) tabs near the stiff corners (3.5 mm of straight wall to the corner) because mid-wall tabs are the ones a bowed lid releases. Positions: -X wall both ends, +X wall at the top end and above the USB opening, bottom wall centre. Pry notches at the bottom tab and the lower -X tab.
- **Tongues.** The top end has no tabs: two 15 mm rigid hooks (0.8 mm out of the skirt, 1.2 high, lower outer corner chamfered) enter 1.0 mm deep grooves under a 2.9 mm ledge of the top wall (0.8 mm of the wall stays behind the groove; groove 0.4 mm free above and below). The lid is assembled by hinging about the outer top rim edge (y = outer_h / 2, z = seam): `fitcheck` rotates the lid 20, 12, 6, 3, 0 deg about that edge against the shell, with the tab hooks left out (they flex): no interference. Flat, lifting the tongue hooks by 0.8 mm hits the ledge. The tongues sit either side of the piezo notch (the top skirt is cut there) and clear of the sound slots. The shell groove ceiling is a 1 mm flat bridge.
- **Seam test.** `case_seamtest_shell.stl` = the top 8 mm of the shell (5.5 cm3, about 1 h). Print it with `case_lid.stl`: the tongue entry, the tabs, the flex, pull and drop tests of the lid before the 34 cm3 shell.
- **Clear window** (`window_t`, default 0): step 0.2 mm bigger than window + 3 mm per side, display moves back by window_t; fitcheck passes with 1.0 (depth 28.3). Not printed.
- **Colours** in the README. The previews use Forest (cream body, forest green panel and wordmark, orange caps with cream legends).
- **3MF projects** (`export_3mf.py`): written in the layout of a Bambu Studio 2.x file. The Bambu Studio CLI segfaults on any 3MF here (even Bambu's own example), so they were checked with OrcaSlicer 2.0: Bambu's custom G-code macros and the prime tower were blanked for that test; layers 1-2 use both extruders. Open them in the GUI to confirm the part names.

## Rev 6: no hooks inside the shell, heat stakes, cradle, one snap joint (2026-10-22)
Print rules (owner): Bambu A1, PETG, 0.4 nozzle, 0.2 layers, NO supports. Every layer's perimeters must sit on the layer below or span a bridge between two supports: cantilevers only as slopes <= 45 deg from vertical, a flat free ledge <= 0.4 mm, bridges <= 10 mm. `printcheck.py` checks the STL files in their print orientation (shell / seam test / test plate front down, deck ceiling down, lid outer face down, caps head down); it also prints the strain table (`FLEX` in `case.py`; limits 1.0 % across layers, 2.0 % in the layer plane).

### Why the old retention was dropped
- **Split pegs** (display 4, Feather 4, deck 2): the head's underside is a catch face pointing at the bed (an overhang in the air), and the split legs flex across the layers. Replaced by printed stake pins melted over with an iron: nothing flexes, nothing overhangs.
- **Battery hooks** (1.2 mm posts, 1.5 mm flat overhang over the cell): a 1.5 mm free ledge, a 1.7 % strain cantilever across layers, and the cell inertia still ended in the lid. Replaced by a cradle (rounded surfaces, 45 deg undersides, end walls) and foam tape.
- **Wall hooks** (50 deg symmetric wedge, 0.5 mm, 1.84 % strain across layers): the lid fingers are now 9.8 mm long, tapered 1.6 -> 0.8 mm, hook 0.35 mm with a 45 deg catch face: 0.92 %.
- The compression stack (lid clamping the cell) and hot glue were rejected by the owner.

### Numbers
- Stake: pin 2.2 in a 2.5 hole, 3.5 above the board top, tip chamfer 0.3, root fillet 0.5 inside a dia 3.25 x 0.5 recess. Pin volume above the board 13.0 mm3; formed head (dome dia 4.2 x 1.2) 9.2 mm3: ratio 1.41 (>= 1.2). Head overlap round the hole 0.85 mm. Display pins: recess edge to the glass pocket 1.8 mm (-X side) / 3.7 mm (+X).
- Cradle: pocket 57.3 x 35.5 (cell 55 x 35, `batt_edge_r` 6.0 ASSUMED), tape 0.8 on the +X end face + shelves (0.4 thick lip, 0.2 flat overhang). Cell top to lid ribs 0.9 mm. The corner blocks leave the middle of the wire channel open (blocks 7 mm in from each side wall).
- Lid: shell depth 23.6 (rev 5: 24.7; the hooks no longer set it, the Feather stack + low ribs do), total 26.2. Rebate 10.0 deep, lid skirt 9.8 (the lid seats on the rim top). Finger: L 9.8, t 1.6 -> 0.8 (outer face 0.2 proud so the rim gap is 0.05), hook 0.35 beyond the rim face, catch 45 deg, lead-in 36 deg, root fillet 0.5, slit 1.0 with a round end reaching 0.5 into the plate, rim relief 0.6. Strain = beam integration with the load at the catch face, deflection 0.35 + 0.10 margin = 0.92 %. The Feather end edge sits 0.05 from the skirt, so the skirt has a notch at the +X wall over the Feather.
- Tongues: flat lip 0.2 then 45 deg (hook and groove roof), groove 0.75 deep, 0.3 slack. Their retention is partly a wedge (the top skirt flexes to release): the five fingers do the holding.
- Switch deck wedge: lip 0.25 (was 0.35), cup walls 0.8 (cup_o 7.9): 0.93 % across layers; a drop of CA is still the real retention.
- Strap: outer bar 5.0 (was 2.7), tunnel inner edges and mouths r 1.0, tunnel bottom z 2.0 (roof 1.0 under the lug top).
- Piezo cradle arms stop 3.2 mm above the axis (31 deg): higher, the bore is a roof steeper than 45 deg.
- Cap flange chamfer 1.1 (flat left 0.4).

### Audit (part | feature | print orientation | load | stress vs layers | strain / overhang | mitigation)
| Part | Feature | Print orientation | Load | Stress vs layers | Strain / overhang | Mitigation |
|---|---|---|---|---|---|---|
| lid | 5 snap fingers | face down, skirt rises | assembly, opening; drop only through the hooks | bending across layers (weak) | 0.92 % (limit 1.0); catch 45 deg | 9.8 mm long, tapered 1.6 -> 0.8, root fillet 0.5, round slit ends, hook 0.35 |
| lid | 2 rigid tongues | face down | lid lift at the top edge | shear of the hook across layers | flat lip 0.2 + 45 deg; no flex | 15 mm wide, groove behind a 2.9 mm ledge |
| lid | rib grid, skirt | face down | bow in a drop | in-plane | none | ribs rise from the plate |
| shell | 12 stake pins | front down, pins rise | board inertia (display about 15 g, Feather 6 g) | shear across layers | none (rigid, melted head) | dia 2.2, root fillet in a recess, 4 pins per board |
| shell | battery cradle blocks, end walls, shelves | front down | cell lateral inertia (40 g) | compression / shear | undersides 45 deg, shelf lip 0.2 flat | tape carries the lift, walls only locate |
| shell | strap lug | front down | strap pull | bending of the 5 mm outer bar across layers | tunnel roof = bridge 4.7 mm | bar 5.0 (strength ~ t^2), r 1 edges |
| shell | USB opening | front down | none | - | roof flat 6.5 = bridge, 45 deg sides | as before |
| shell | piezo cradle | front down | glue only | - | underside 45 deg, arms 31 deg above axis | arms trimmed |
| shell | sound slots | front down | none | - | 1.0 mm bridges | - |
| shell | tab grooves | front down | hook catch | compression | roof 45 deg | parallel to the catch face |
| shell | tongue grooves | front down | tongue catch | compression | lip 0.2 flat + 45 deg | - |
| deck | 12 switch wedges | ceiling down | switch insertion | bending of the cup wall across layers | 0.93 %; wedge underside 0.25 flat | cup wall 0.8, lip 0.25, CA drop |
| deck | stake holes | ceiling down | none | - | - | 2.5 mm holes |
| caps | flange | head down | pull-out against the plate | shear | chamfer 45 deg, flat 0.4 | 1.1 mm chamfer |
| all | inlay pockets (0.4 deep, bed side) | - | - | - | roof filled by the 2nd colour | reported "inlay pocket (filled)" |

`printcheck.py` results (all PASS), regions near a limit: cradle shelf lip 0.40 (flat 0.2 + the 45 deg step), cap flange 0.40, tongue-groove roof 0.39 / 0.29, sound slots 0.46 (1 mm bridges), USB roof 3.4 (6.5 mm bridge), strap tunnel roof 1.4 (4.7 mm bridge).
