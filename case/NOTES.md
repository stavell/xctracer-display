# Enclosure notes / progress

Status: **Rev 4** (rocker D-pad + A/B Game-Boy-style caps, switch deck, centred window, front inlay, PS1240 buzzer). Fitcheck passes. Not printed yet.
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
