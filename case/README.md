# Enclosure

A screwless, boltless 3D-printed case for the display box: Sharp 2.7" memory LCD (Adafruit 4694) in portrait,
Feather nRF52840 Sense (4516), a 55 x 40 x 10 mm LiPo and six tact switches (D-pad + A/B).
Use cases: paragliding flight display, handheld game console, generic gadget.

| Front | Back (plain lid) | Open |
|---|---|---|
| ![Front](preview_front.png) | ![Back](preview_back.png) | ![Open](preview_open.png) |

| Front, iso | Back, iso | Section through the D-pad column |
|---|---|---|
| ![Iso front](preview_iso_front.png) | ![Iso back](preview_iso_back.png) | ![Cutaway](preview_cutaway.png) |

**Status:** model regenerates, fitcheck passes, not printed yet. See `NOTES.md` for what to measure first.

## Features

- Outer **62.3 x 96.9 x 22.6 mm**, 3 mm walls, chamfered edges.
- Portrait display; below it D-pad (up/down/left/right, 9 mm pitch) on the left and A/B (10.6 mm apart, B lower-left) on the right, six 6x6 mm tact switches, separate printed caps with retaining flanges. 6 GPIO + GND.
- All electronics are in the shell: Feather on 4 posts with split snap pegs, battery on rails with stops, display on 4 split pegs. The lid is a plain plate, swappable for other backs.
- Flat lid, 59 x 80 mm free of holes (velcro sticker), 2 small access holes at the bottom edge for Feather reset (1.8 mm pin) and user switch (3.4 mm). Pry notches only at the seam.
- Strap tunnel through the bottom corner: 3.2 x 10.4 mm (10 mm strap, or a cord).
- USB opening aligned with the Feather micro-B; optional 12 mm piezo disc cup with sound slots in the bottom wall.
- Lid snaps on with 10 skirt tabs (about 1.8 % strain). No screws, bolts or inserts.

## Print (Bambu A1, PETG, 0.2 mm, no supports)

| Part | Orientation |
|---|---|
| `case_shell.stl` | front face down |
| `case_lid.stl` | outer face down |
| `case_buttons.stl` | 6 caps, head down |

Details and checked overhangs in `NOTES.md`. `case_assembly.stl` is for viewing only.

## Files

| File | What |
|---|---|
| `case.py` | the CadQuery model, all dimensions in its PARAMETERS section |
| `fitcheck.py` | interference check against stand-in parts, snap strain, flat-area report |
| `render.py`, `make_previews.py` | preview renderer (numpy, no OpenGL) |
| `*.stl`, `*.step` | parts to print, assembly |
| `NOTES.md` | hardware dimensions, assumptions to verify, print plan |

## Regenerate

    python3.11 -m venv .venv && .venv/bin/pip install -r requirements.txt
    .venv/bin/python case.py            # STL + STEP
    .venv/bin/python fitcheck.py        # interference check
    .venv/bin/python make_previews.py   # PNGs
