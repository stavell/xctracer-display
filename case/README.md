# Enclosure

A screwless, boltless 3D-printed case for the display box: Sharp 2.7" memory LCD (Adafruit 4694) in portrait,
Feather nRF52840 Sense (4516), a 55 x 35 x 13 mm LiPo, a housed piezo (PS1240 type) and six tact switches
(rocker D-pad + A/B). Use cases: paragliding flight display, handheld game console (Tetris, Snake, Breakout), generic gadget.

| Front (dark screen panel) | Back (wordmark inlay) | Open |
|---|---|---|
| ![Front](preview_front.png) | ![Back](preview_back.png) | ![Open](preview_open.png) |

| Front, iso | Back, iso | Lid from inside (rib grid) |
|---|---|---|
| ![Iso front](preview_iso_front.png) | ![Iso back](preview_iso_back.png) | ![Lid inside](preview_lid_inner.png) |

| Open (lid off, cell in its cradle) | Lid tongue under the ledge (top end) | Snap finger, hook in its groove |
|---|---|---|
| ![Open iso](preview_open_iso.png) | ![Tongue](preview_section_tongue.png) | ![Finger](preview_section_tab.png) |

| Heat stake: pin through the PCB, melted head, iron access | Battery cradle (section across the cell) | Strap corner (section at mid height) |
|---|---|---|
| ![Stake](preview_section_stake.png) | ![Cradle](preview_section_cradle.png) | ![Strap](preview_strap_corner.png) |

Section through the D-pad column: ![Cutaway](preview_cutaway.png)

Section through the A cap: ![Cutaway A](preview_cutaway_ab.png)

Close-up of the control band: ![Close-up](preview_front_closeup.png)

**Status:** rev 6. Model regenerates; `fitcheck.py`, `inlay_check.py` and `printcheck.py` pass; not printed yet.
The previews show the default colours ("Forest", see Colours).
Print the feel-test plate first (below). See `NOTES.md` for what to measure.

## Features

- Outer **66.9 x 116.2 x 26.2 mm** (shell depth 23.6 + lid 2.6; rev 5: 27.3), 3 mm walls, 5 mm outer corners, one 1.5 mm chamfer on all outer edges.
- **Front, two colours:** a dark screen panel (accent, 0.4 mm inlay) around the window, 2.0 mm inside the flat front face on top and sides (corners r 1.5, concentric), bottom corner on the A/B side r 11 (the Game-Boy nod). Picture-mat margins, window edge to panel edge: sides 11.4, top 7.0, bottom 11.0. It runs right to the glass (its edge follows the 0.6 mm window chamfer). No text on the front; the control band is plain body colour.
  Panel to the D-pad opening and D-pad opening to the flat bottom edge are both 5.0 mm (`ctrl_gap`); the case is as long as these margins need (`panel_top`, `panel_bot`, `ctrl_gap` in `case.py`). The strips above and below the display pocket are hollow down to the 2 mm plate.
- Display window **centred on the case axis** (side bezels 15.28 mm each); the PCB sits off-centre inside (the glass is off-centre on the breakout). 0.6 mm 45 deg bezel chamfer in the window.
- **Controls, Game-Boy style:** one 22 mm rocker D-pad (arms 7.5 mm, stands 2.5 mm proud, flat top, four outward arrows as body-colour legends) on the user's left, A and B (9.5 mm, 2.0 mm proud, flat top, body-colour letters 4.5 mm cap height,
  14 mm apart, tilted 25 deg, B lower-left of A) on the right. D-pad centre and A/B pair centre are mirrored about the case axis (x = +-17.3 mm).
  Each cap has a head through the front-plate opening (head + 0.3 mm clearance) and a 1.2 mm flange behind the plate (1.2 mm overlap) that traps it.
- D-pad pivot: a 0.9 mm dome nub on the flange back rests under the switch deck; four 3.4 mm stubs press the four switches (8 mm from the centre).
  Tilt about the opposite plate edge: actuation after ~0.35 mm of stub travel (0.1 gap + ~0.25), hard stop at ~0.5 mm (nub meets the deck).
  The opposite arm lifts away, it never presses its switch. 6 GPIO + GND to the Feather.
- All electronics are in the shell: the display (4), the Feather (4) and the switch deck (4) are **heat-staked** (printed pins melted over with a soldering iron), the battery sits in a **cradle** on foam tape. There are no hooks, clips or split pegs inside the shell (see Rev 6 in `NOTES.md`).
- **Heat stakes (rev 6).** Pin dia 2.2 through 2.5 mm board holes, 3.5 mm above the board's top face (0.3 mm tip chamfer, 0.5 mm root fillet in a dia 3.25 x 0.5 recess so the board still sits on the rim). Melted over with an iron it becomes a dome of about dia 4.2 x 1.2 mm (0.85 mm overlap round the hole; the pin holds 1.4 x the head volume). Display: 4 pins rise from the glass-side floor through the PCB corner holes. Feather: 4 pins on its 4 posts. Switch deck: 4 pins (x = +-3, band centre +-10.6) through the deck ring. `fitcheck.py` keeps a head envelope (dia 4.5 x 1.5) and an iron access cylinder (dia 5 x 8 above it) clear of every part, wall, the cradle and the wire channel, and the formed heads clear of the lid ribs.
- **Battery cradle (rev 6).** The cell is assumed to have rounded long sides (wrapped cell, `batt_edge_r` = 6.0 mm). Four corner blocks hug those rounded edges (pocket 35.5 wide) and two end walls stop it in X (pocket 57.3 long: 1.5 slack + 0.8 for the tape); everything has 45 deg undersides because it hangs over the display pocket. A double-sided foam tape (VHB type, 0.8 mm) goes on the +X end face (against the end wall) and on the two shelves under the ends of the cell. The lid ribs stay 0.9 mm above the cell: nothing loads the lid. The wire channel (6 mm between the control band and the cell) stays open between the corner blocks.
- **Lid: the only snap joint (rev 6).** 5 snap fingers (14 mm wide: 2 on each -X / +X side, 1 on the bottom end) and 2 rigid tongues (15 mm) at the top end that slide under a ledge in the top wall. The lid prints face down and the fingers bend across the layers, so they are 9.8 mm long and tapered (1.6 mm at the root, 0.8 mm at the tip), hook 0.35 mm, 45 deg catch face, 0.5 mm root fillet, round slit ends: peak strain 0.92 % (limit 1.0 %). The lid plate is 2.6 mm with an inner rib grid (outer face flat).
- **Switch deck** (separate part): carries the 6 switch cups and is heat-staked onto four pins over the caps.
- **Flat lid, no holes** (63.9 x 120.7 mm flat), wordmark "XC DISPLAY" (accent inlay, 4.2 mm cap height) in the bottom 8 mm; the rest is free for velcro. No access to the Feather reset or user switch from the back: the six front keys replace them, reset needs the lid off.
- Strap tunnel through the bottom corner (3.2 x 10.4 mm, 10 mm strap or cord), 45 deg through the corner lug; the outer bar between the tunnel and the outer corner is 5 mm (rev 5: 2.7), the tunnel's inner edges and mouths are rounded r 1. USB opening aligned with the Feather micro-B.
- **Piezo** (PS1240-type, 12.2 x 6.5 mm) glued in a cradle on the inside of the TOP wall, grille of 5 slots centred on the axis.
- No screws, bolts or inserts.
- **Optional clear window** (`window_t` in `case.py`, 0 = off, shipped as 0): with `window_t` = 1.0 the shell gets a step between the bezel lip and the glass for a sheet of that thickness and the display moves back by it (case 1.0 mm deeper; fitcheck passes). Cut the sheet 41.9 x 65.4 mm (window + 3 mm all round, corner radius 1.5 mm), pocket 42.3 x 65.8 mm; hold it with a drop of CA at two corners or a thin double-sided tape frame.

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
| `case_seamtest_shell.stl` | seam side up (cut face on the bed) | the top 8 mm of the shell: rim ring with the tab grooves, tongue grooves, pry notches, USB, piezo cradle. Fit the lid, pull it, drop it: ~1 h before the full shell |
| `case_testplate.stl` | front face down | feel test, ~25 min (with the deck stake pins and Feather posts) |
| `case_front.3mf`, `case_back.3mf`, `case_buttons.3mf` | as above | Bambu Studio projects, see below |
| `case_assembly.stl` | view only | |

### Colours
Three filaments in total, two per part, colour only in the first 2 layers (the inlays). Filament 1 = BODY, 2 = PANEL (and lid wordmark), 3 = CAPS; cap legends are body colour.

| Name | Body | Screen panel + lid wordmark | Caps (legends in body colour) |
|---|---|---|---|
| **Forest** (default, the previews) | cream (238, 228, 205) | forest green (36, 86, 62) | orange (232, 112, 40) |
| Pocket | yellow | navy | red |
| Lake | light blue | navy | yellow (legends navy) |
| Sunset | cream | teal | coral |

Forest in Bambu PETG HF: Cream / Forest Green / Orange or the nearest. The previews render the display glass silver-grey.

### Bambu Studio projects (`export_3mf.py`)
`case_front.3mf` (shell = filament 1 + panel = filament 2), `case_back.3mf` (lid = 1 + wordmark = 2), `case_buttons.3mf` (caps = 3 + legends = 1): each is ONE object with two named parts, centred on the A1 plate in the print orientation above, A1 0.4 nozzle / 0.20 mm Standard / PETG HF presets, three filament slots coloured as Forest. Open them with File > Open and slice; change the filament colours to your palette. `python export_3mf.py` rebuilds them after `case.py`.
Check done: the Bambu Studio command line crashes on every 3MF on this machine (also on one of Bambu's own samples), so the files were test-sliced with OrcaSlicer 2.0 (its command line, Bambu custom G-code blanked): all three slice, layers 1 and 2 carry both extruders (T0 + T1, caps plate T2 + T0) and layer 3 up carries only the body (caps plate: caps). They were not opened in the Bambu Studio GUI.

### Multi-colour inlays by hand in Bambu Studio
1. Shell: load `case_shell.stl` and `case_shell_inlay.stl` together; answer **"Load as a single object with multiple parts"** (they share one coordinate frame, so the panel drops into its pocket). Shell part = filament 1, inlay part = filament 2.
2. Lid: `case_lid.stl` (filament 1) + `case_lid_inlay.stl` (filament 2), the same way.
3. Caps: `case_buttons.stl` (**accent**) + `case_buttons_inlay.stl` (**body** colour), as one object with two parts.
4. Without AMS: a single filament change at layer 3 does NOT work (the part's own bed face is also in layers 1-2). Print the inlay as a separate 2-layer part and glue it in with a drop of CA, or paint the pockets.
5. Minimum stroke of all inlay features is 0.6 mm (`inlay_check.py` measures all three); lid wordmark stems about 0.8 mm (two 0.4 mm lines).
All bed faces are flat: no dishes or domes (the caps have flat tops, 0.6 mm head chamfer only).

### Feel-test print (do this first, 20-30 min)
`case_testplate.stl` is the control band of the front plate (openings, deck pins, rails, Feather posts) without walls. Print it, `case_buttons.stl` and `case_deck.stl`,
drop in the caps, stake the deck on, clip in six real tact switches: feel the spacing, travel, click and the rocking before printing the shell.
Then print `case_seamtest_shell.stl` and `case_lid.stl` (the tongue, the tabs, the flex of the 2.6 mm lid) before the shell.

## Tools
Soldering iron with a conical (or fine chisel) tip, **220-240 C for PETG staking** (tin the tip, wipe it clean), a drill 1.6 mm and 1.75 mm filament offcuts (rework), double-sided foam tape (VHB type, 0.8 mm), CA glue, side cutters, a thin pry tool.

## Assembly (shell face down on the bench)
1. **Caps.** Shell front face down. Drop the three caps in from the inside (heads go through the openings, the flanges stop against the plate, they rock freely).
2. **Deck + stake.** Lay the deck on its four posts (cups face the back; the pins go through its holes). Press the iron onto each pin tip for 2-3 s while pushing lightly: a dome of about 4 mm dia x 1.2 mm forms. The deck traps the flanges. Keep the shell face down until this is done, a loose cap would fall out. Never put glue near a cap.
3. **Switches.** Clip the six tact switches into the deck cups from the back, **legs toward the top and bottom of the case** (a drop of CA on each body if the wedges are loose). Solder 6 GPIO wires + one GND and route them over the cup walls toward the Feather.
4. **Feather + stake.** Lay the Feather (components toward the back) on its four posts, stake the four pins.
5. **Display + stake.** Lay the display PCB (glass toward the bezel) on its floor, the pins come through the corner holes; stake the four pins (the iron reaches them from the back, the cell is not in yet).
6. **Battery.** Foam tape on the +X end face of the cell and on its two end shelves; lower the cell into the cradle (leads out toward the wire channel), press the taped end against the +X end wall.
7. **Piezo.** Glue the PS1240 in its cradle on the top wall (sound face against the wall, grille side), solder the 2 leads (100 ohm in series on one), drive it from two GPIOs in anti-phase.
8. **Wires.** Connect display, battery, piezo and switch leads to the Feather through the wire channel.
9. **Lid.** Put the two tongues of the top end into the grooves (lid tilted, its top rim on the top rim of the shell; it may slide 0.3 mm toward the groove), rotate down about that edge and press along the long sides and the bottom: the 5 fingers snap.

**Opening:** push a thin tool into the pry notch at the bottom end (or on the left wall), lift that edge, release the other fingers the same way, lift the bottom end first and slide the lid out of the top tongues. **Battery out:** peel the tape end from the wall and lift the cell.

**Rework of a stake.** Cut the head flush with side cutters, drill 1.6 mm about 3 mm into the stub, press in a 1.75 mm filament piece (a drop of CA), leave it 3.5 mm proud and stake again. A board can be lifted and re-staked a few times this way.

## Files

| File | What |
|---|---|
| `case.py` | the CadQuery model, all dimensions in its PARAMETERS section |
| `fitcheck.py` | interference, cap assembly, rocking, battery cradle (4 cell positions, drop-in sweep, X/Y stops, lid clearance), heat stakes (head volume, envelope, iron access, glass), strain table, lid rotation into the tongue grooves, panel clearance to the D-pad |
| `printcheck.py` | printability of the 6 printed parts in their print orientation: overhang regions (>45 deg from vertical, limit 0.4 mm free ledge or a bridge <= 10 mm) and the strain table of the flexing features |
| `inlay_check.py` | minimum stroke of the three inlays (shell, lid, caps; needs shapely) |
| `export_3mf.py` | the three Bambu Studio projects (`case_front.3mf`, `case_back.3mf`, `case_buttons.3mf`) |
| `render.py`, `make_previews.py` | preview renderer (numpy, no OpenGL) |
| `*.stl`, `*.step` | parts to print, assembly |
| `NOTES.md` | hardware dimensions, assumptions to verify, print plan |

## Regenerate

    python3.11 -m venv .venv && .venv/bin/pip install -r requirements.txt
    .venv/bin/python case.py            # STL + STEP
    .venv/bin/python fitcheck.py        # checks
    .venv/bin/python printcheck.py      # print rules on the STL files
    .venv/bin/python inlay_check.py     # inlay strokes
    .venv/bin/python make_previews.py   # PNGs
    .venv/bin/python export_3mf.py      # Bambu Studio projects
