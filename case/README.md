# Enclosure

A screwless, boltless 3D-printed case for the display box: Sharp 2.7" memory LCD (Adafruit 4694) in portrait,
Feather nRF52840 Sense (4516), a 55 x 35 x 13 mm LiPo, a housed piezo (PS1240 type) and six tact switches
(rocker D-pad + A/B). Use cases: paragliding flight display, handheld game console (Tetris, Snake, Breakout), generic gadget.

| Front (dark screen panel) | Back (wordmark inlay) | Open |
|---|---|---|
| ![Front](preview_front.png) | ![Back](preview_back.png) | ![Open](preview_open.png) |

| Front, iso | Back, iso | Section through the D-pad column |
|---|---|---|
| ![Iso front](preview_iso_front.png) | ![Iso back](preview_iso_back.png) | ![Cutaway](preview_cutaway.png) |

Section through the A cap: ![Cutaway A](preview_cutaway_ab.png)

Close-up of the control band: ![Close-up](preview_front_closeup.png)

**Status:** rev 5. Model regenerates, fitcheck and inlay_check pass, not printed yet.
The previews show the two filaments: body (light) and accent (charcoal).
Print the feel-test plate first (below). See `NOTES.md` for what to measure.

## Features

- Outer **66.9 x 99.8 x 24.8 mm** (ratio 1 : 1.49), 3 mm walls, 5 mm outer corners, one 1.5 mm chamfer on all outer edges.
- **Front, two colours:** a dark screen panel (accent, 0.4 mm inlay) around the window, 2.0 mm inside the flat front face on top and sides, concentric corners (r 1.5), 3.5 mm below the window, bottom corner on the A/B side r 9 (the Game-Boy nod). It runs right to the glass (its edge follows the 0.6 mm window chamfer, no body-colour sliver). No text on the front; the control band is plain body colour.
  The case is 3.5 mm longer than rev 4 (`panel_gap`): without it the D-pad opening was only 3.5 mm below the window and the panel could not end above it. The panel now ends 3.5 mm above the D-pad opening.
- Display window **centred on the case axis** (side bezels 15.28 mm each); the PCB sits off-centre inside (the glass is off-centre on the breakout). 0.6 mm 45 deg bezel chamfer in the window.
- **Controls, Game-Boy style:** one 22 mm rocker D-pad (arms 7.5 mm, stands 2.5 mm proud, flat top, four outward arrows as body-colour legends) on the user's left, A and B (9.5 mm, 2.0 mm proud, flat top, body-colour letters 4.5 mm cap height,
  14 mm apart, tilted 25 deg, B lower-left of A) on the right. D-pad centre and A/B pair centre are mirrored about the case axis (x = +-17.3 mm).
  Each cap has a head through the front-plate opening (head + 0.3 mm clearance) and a 1.2 mm flange behind the plate (1.2 mm overlap) that traps it.
- D-pad pivot: a 0.9 mm dome nub on the flange back rests under the switch deck; four 3.4 mm stubs press the four switches (8 mm from the centre).
  Tilt about the opposite plate edge: actuation after ~0.35 mm of stub travel (0.1 gap + ~0.25), hard stop at ~0.5 mm (nub meets the deck).
  The opposite arm lifts away, it never presses its switch. 6 GPIO + GND to the Feather.
- All electronics are in the shell: Feather on 4 posts with split snap pegs, battery on rails with stops, display on 4 split pegs. The lid is a plain plate.
- **Switch deck** (separate part): carries the 6 switch cups and snaps onto two pins over the caps.
- **Flat lid, no holes** (63.9 x 96.8 mm flat), wordmark "XC DISPLAY" (accent inlay, 4.2 mm cap height) in the bottom 8 mm; the rest is free for velcro. No access to the Feather reset or user switch from the back: the six front keys replace them, reset needs the lid off.
- Strap tunnel through the bottom corner (3.2 x 10.4 mm, 10 mm strap or cord). USB opening aligned with the Feather micro-B.
- **Piezo** (PS1240-type, 12.2 x 6.5 mm) glued in a cradle on the inside of the TOP wall, grille of 5 slots centred on the axis.
- Lid snaps on with 10 skirt tabs (1.8 % strain). No screws, bolts or inserts.

## Parts and print orientation (Bambu A1, PETG, 0.2 mm, no supports)

| Part | Orientation | Notes |
|---|---|---|
| `case_shell.stl` | front face down | pockets for the inlay (0.4 mm) are already cut |
| `case_shell_inlay.stl` | front face down | same coordinates as the shell, 0.4 mm thick, accent colour (the screen panel) |
| `case_deck.stl` | ceiling face down (already at z = 0) | switch cups, wedges |
| `case_lid.stl` | outer face down | pocket for the wordmark (0.4 mm) is cut |
| `case_lid_inlay.stl` | outer face down | same coordinates as the lid, accent colour (mirrored in this frame, reads right from the back) |
| `case_buttons.stl` | head down (D-pad + A + B), as exported | accent colour; pockets 0.4 mm deep for the legends |
| `case_buttons_inlay.stl` | head down | same coordinates, BODY colour: letters A, B and the four arrows |
| `case_testplate.stl` | front face down | feel test, ~25 min |
| `case_assembly.stl` | view only | |

### Multi-colour inlays in Bambu Studio
Two filaments: filament 1 = BODY (light grey / off-white), filament 2 = ACCENT (charcoal). Every inlay is exactly the first 2 layers (0.2 mm layers), flush in pockets of the face that lies on the bed.
1. Shell: load `case_shell.stl` and `case_shell_inlay.stl` together; answer **"Load as a single object with multiple parts"** (they share one coordinate frame, so the panel drops into its pocket). Shell part = filament 1, inlay part = filament 2.
2. Lid: `case_lid.stl` (filament 1) + `case_lid_inlay.stl` (filament 2), the same way.
3. Caps: `case_buttons.stl` (**accent**, filament 2) + `case_buttons_inlay.stl` (**body**, filament 1), as one object with two parts. The legends are the coaster-style letters and arrows.
4. Without AMS: a single filament change at layer 3 does NOT work (the part's own bed face is also in layers 1-2). Print the inlay as a separate 2-layer part and glue it in with a drop of CA, or paint the pockets.
5. Minimum stroke of all inlay features is 0.6 mm (`inlay_check.py` measures all three). A 3MF with both parts was not produced: CadQuery 2.8 cannot export 3MF.
All bed faces are flat: no dishes or domes (the caps have flat tops, 0.6 mm head chamfer only).

### Feel-test print (do this first, 20-30 min)
`case_testplate.stl` is the control band of the front plate (openings, deck pins, rails, Feather posts) without walls. Print it, `case_buttons.stl` and `case_deck.stl`,
drop in the caps, snap the deck on, clip in six real tact switches: feel the spacing, travel, click and the rocking before printing the 25 cm3 shell.

## Assembly (shell face down on the bench)
1. Shell front face down. **Drop the three caps in from the inside** (heads go through the openings, the flanges stop against the plate; they rock freely).
2. Press the **deck** onto the two pins (cups face the back). The deck traps the flanges. The shell must stay face down until this step is done, a loose cap would fall out.
3. **Clip the six tact switches** into the deck cups from the back, **legs toward the top and bottom of the case** (into the leg slots; the wedges are on the other two sides) (the plunger enters the hole, the 0.35 mm wedges catch the body). A drop of CA glue on each switch body where it sits in its cup (only if the wedges are loose).
   **Never put glue near a cap**: the caps must move freely.
4. Solder 6 GPIO wires + one common GND to the switch legs (see NOTES for the pin map to decide) and route them over the cup walls toward the Feather.
5. Snap the display onto its 4 pegs, press the Feather onto its 4 posts (components toward the back), connect the wires and the battery.
6. Glue the **PS1240 piezo** in its cradle on the top wall with a drop of CA (sound face against the wall, grille side), solder the 2 leads (one 100 ohm series resistor on one of them).
   Drive it from two GPIOs in anti-phase (nRF PWM), no other hardware.
7. Battery onto its rails (foam tape), snap the lid on.

## Files

| File | What |
|---|---|
| `case.py` | the CadQuery model, all dimensions in its PARAMETERS section |
| `fitcheck.py` | interference, cap assembly (head passes, flange retains), rocking, snap strain, panel clearance to the D-pad |
| `inlay_check.py` | minimum stroke of the three inlays (shell, lid, caps; needs shapely) |
| `render.py`, `make_previews.py` | preview renderer (numpy, no OpenGL) |
| `*.stl`, `*.step` | parts to print, assembly |
| `NOTES.md` | hardware dimensions, assumptions to verify, print plan |

## Regenerate

    python3.11 -m venv .venv && .venv/bin/pip install -r requirements.txt
    .venv/bin/python case.py            # STL + STEP
    .venv/bin/python fitcheck.py        # checks
    .venv/bin/python inlay_check.py     # inlay strokes
    .venv/bin/python make_previews.py   # PNGs
