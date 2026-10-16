# Enclosure notes / progress

Status: **Rev 3** (D-pad + A/B, electronics in the shell, plain lid). Fitcheck passes. Not printed yet.
Source of truth: `case.py` (CadQuery, all dimensions in PARAMETERS). `python case.py` regenerates the STL/STEP files,
`python fitcheck.py` checks interference with stand-in parts, `python make_previews.py` redraws the PNGs (own numpy renderer, no OpenGL).

## Hardware
- Display: Adafruit SHARP Memory 2.7" 400x240 (#4694). PCB 63.5 x 55.88 mm, 4x 2.5 mm holes at 58.42 x 50.8. Glass ~63.5 x 41.2, active 58.8 x 35.3.
  Used in PORTRAIT: the 63.5 side is vertical, the header ear (6.4 mm) ends up on the user's right. Firmware must rotate the content (verify rotation 1 vs 3).
- MCU: Feather nRF52840 Sense (#4516), 50.8 x 22.86, micro-B at one short end, holes 2.54 / 48.26 x 2.54 / 20.32, Reset SW1 (u 11.1, v 6.22), user SW2 (u 29.93, v 5.85), JST at u~10.8 v~18.
- Battery: LiPo 55 x 40 x 10 (JST-PH assumed).
- 6 x 6x6 mm tact switches (assumed body 3.5 + plunger 1.5).

## Design (rev 3)
Outer **62.3 x 96.9 x 22.6 mm** (was 82.9 x 62.3 x 21.2). Walls 3 mm, 5 mm corner radius, 1.5 mm chamfers front/back.
Case frame: Z=0 front; X/Y as seen from the BACK. Seen from the front case +X is the user's left (D-pad at +X, A/B at -X).

- **Buttons**: 6 tact switches below the screen (button band 27 mm tall, was 13 mm wide column). Separate printed caps (one part, 6 copies, `case_buttons.stl`)
  instead of flexure tongues: pitches this tight leave no room for 11 mm tongues, caps cannot fatigue, and a flange behind the 1.0 mm web retains them.
  Cap = 7 mm head in a 7.6 counterbore (stands 0.15 mm proud), 4.2 stem through a 4.7 hole, 6.4 flange. Head bottoms out after 0.35 mm of travel (switch needs ~0.25).
  Each switch sits in a cup (6.3 mm pocket) with two 0.35 mm snap wedges behind the body as back-stop; plunger tip is 0.1 mm from the flange.
  Wires: switch legs point to the back into the 3.5 mm gap under the Feather; run the 6 GPIO + GND wires sideways over the cup walls (cups end at the body back face) to the Feather pads. **6 GPIO + GND = 7 wires.**
- **Feather** (all electronics now in the shell, lid is a plain plate): 4 posts (4.4 mm) rise from the front plate in the button band, with 2.4 mm split snap pegs through the Feather holes
  (comps face the lid). PCB underside z 12.0, top z 13.6, USB end 0.3 mm from the +X wall. Posts instead of wall ledges: wall ledges would need unsupported 6+ mm cantilevers.
- **Battery**: lies 55 (X) x 40 (Y) behind the display next to the band, on two 2.5 mm rails with 35 deg undersides, 4 stop blocks; the lid presses it. Add foam tape (1.4 mm free in Z).
- **Reset / SW2**: pin holes through the LID (reset 1.8 mm, SW2 3.4 mm) with 4.6 mm guide tubes down to 0.8 mm above the switches, away from the flat velcro zone. Removing the back exposes both switches directly.
  Flexure nubs were dropped (lid is now plain). A different back needs its own 2 holes if reset must work with it on.
- **USB**: 11.5 x 7 mm opening (45 deg chamfered top, no wide bridge) in the +X wall at y -31.9, z 14.95; matching notch in the lid skirt.
- **Strap**: 45 degree rectangular tunnel 3.2 x 10.4 mm through a solid lug at the bottom -X corner (user's bottom right), 2.7 mm bridge to the outer corner. Fits a 10 mm strap; a 3 mm cord fits too (no separate hole).
- **Buzzer**: 12 mm disc glued on a ring on the inner face of the bottom wall, 5 slots 1.0 x 7.5 mm through the wall; 18 mm notch in the lid skirt for the disc.
- **Flat back**: lid outer face has no features except the 2 pin holes at the bottom edge. Flat 59.3 x 93.9 mm; hole-free 59.3 x 80.1 mm (everything above y = -33). Pry notches are only at the seam edge (top wall and left wall).
- Snap fit: lid skirt 1.2 mm thick x 7 mm free length (L/t 5.8), hook 0.5 deep x 1.2 high (50 deg lead-in, flat retaining face), **10 tabs** (top 2, bottom 2, left 3, right 3), 10 mm wide with 0.8 slits. Strain eps = 1.5 t d / L^2 = **1.8 %** (PETG limit ~2 %).
- Display window 59.4 x 35.9, glass recessed 1.2 mm behind the bezel; 4 split pegs (2.4 mm, hole 2.5) hold the display PCB.

## Print orientation (Bambu A1, PETG, 0.2 mm layers, 0.4 mm nozzle, no supports)
- **Shell**: front face down. Overhangs/bridges checked: strap slot ceiling bridge 3.2 mm; USB top is a 45 deg chamfer (flat part 6.5 mm); switch pocket wedges 0.35 mm overhang; flange chamber ceiling (ring) bridge 6.9 mm;
  battery rails 35 deg from vertical; buzzer ring 1.0 mm cantilever; sound slots 1.0 mm bridge; hook grooves 50 deg; peg heads taper up, 0.3 mm undercut at the slot.
- **Lid**: outer face down (plate first, skirt and tubes upright). Tab hooks 50 deg both sides. Pin holes vertical. Skirt arms bend across layers: the weak direction, strain kept < 2 % and test fit.
- **Caps**: head down on the bed, flange underside has a 0.6 mm 45 deg chamfer (no supports). Six on one plate, 10 mm pitch.
- Each part fits the 256 mm bed easily. Walls >= 0.85 mm everywhere (thinnest: cup flange chamber wall 0.85, switch pocket wall 1.15).
- Sliding clearances 0.25 (lid), 0.1-0.3 for caps; pegs 2.4 in 2.5 holes (press/snap). **Pegs are 2.4 mm, below the 2.5 mm upright guideline** because the PCB holes are 2.5: they are short (<= 2.5 mm tall incl. head) and slotted; print the shell slow, check one.

## Assumptions to verify before printing
- Glass module height above the PCB: 2.4 mm guessed (`glass_t`). Calipers.
- Tact switch body/plunger (3.5 + 1.5), plunger diameter 3.5, switch legs (3.5 mm gap under the Feather).
- Battery real size (+1 mm?), JST polarity vs Feather, battery lead length to the Feather JST (JST sits on the Feather top edge near the USB end).
- Feather tall part height 6 mm (`fe_comp_h`), USB overmold fits 11.5 x 7.
- Piezo disc diameter 12 and thickness; back-of-display component height (3.5 mm zone, assumed).
- Display PCB orientation: Eagle frame assumed to be the front view; the ear is on the user's right in portrait.
- Skirt tab stiffness; if too stiff reduce `hook_depth`.
- Press-fit of peg heads through the 2.5 mm holes.

## TODO
- [ ] Measure glass height / switch / battery, update `case.py`
- [ ] Test print: caps feel, wedges retention, tabs, USB alignment, strap lug
- [ ] Firmware: 6 GPIO pin map, display rotation

## Files
- `case.py` model + export, `fitcheck.py` interference/strain, `render.py` + `make_previews.py` previews
- `case_shell.*`, `case_lid.*`, `case_buttons.*` (print), `case_assembly.stl` (view only)
- `preview_*.png`
