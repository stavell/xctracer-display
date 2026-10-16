# Enclosure notes / progress

Status: **Phase 2 done** (shape + features). Phase 3 (final checks, print settings) pending. Not yet printed.
Source of truth: `case.py` (CadQuery, all dimensions in the PARAMETERS sections). Run `python case.py` to regenerate STLs.

## Hardware
- Display: Adafruit SHARP Memory 2.7" 400x240 breakout (#4694). PCB 63.5 x 55.88 mm, 4x 2.5 mm holes at 58.42 x 50.8. Glass ~63.5 x 41.2 mm, active 58.8 x 35.3. Sources: Adafruit Eagle .brd (Adafruit-Sharp-Memory-Display-PCBs) + product photos.
- MCU: Adafruit Feather nRF52840 Sense (#4516). 50.8 x 22.86 mm, micro-B at one short end, holes at 2.54 / 48.26 x 2.54 / 20.32, Reset (SW1) and user switch (SW2) on one long edge. Source: Adafruit-Feather-nRF52840-Sense-PCB .brd.
- Battery: LiPo 55 x 40 x 10 mm (JST-PH assumed, unverified).

## Design
- Two parts: shell (front-face down) + lid (outer face down). Outer 82.9 x 62.3 x 21.2 mm, 3 mm walls, 5 mm corner radius, 1.5 mm chamfer on front/back edges.
- No screws, all snap fits:
  - display PCB on 4 split pegs through its mounting holes
  - Feather on 4 split pegs on lid posts (components face the lid)
  - lid on 6 skirt snap tabs (hook + groove), 2 pry notches at the seam
- Display window 59.4 x 35.9 mm, glass recessed 1.2 mm behind the bezel lip.
- 4 front buttons: printed flexure tongues (1.2 mm thick, 11 mm long) pressing 6x6 tact switches in cups; lid pins hold switches in.
- Feather Reset + user switch: flexure tongues in the lid with nubs pressing the switches.
- USB: 11.5 x 7 mm opening in the top wall, matching notch in the lid skirt.
- Layout: display PCB on the left of the cavity, button column on the right, battery on its side (40 x 55) behind the display left, Feather behind the display right, USB at top.

## Assumptions to verify (measure the real parts)
- Glass module height above PCB: **2.4 mm guessed** (`glass_t`). Calipers needed.
- Tact switch: 6x6 mm, body 3.5 + plunger 1.5, plunger toward the front. Check the real part before printing.
- Flexure stiffness is an estimate; may need `tongue_t` tweak after first print.
- Snap-tab arms print upright (weak direction); test fit, may need thicker/shorter arms or a different tab layout.
- Battery JST polarity vs Feather. Check before plugging in.
- Battery x position: keep >= 0.3 mm clear of the lid skirt (battery sits at x ~ -38.1 .. 1.9 in case coordinates).
- Feather reset/user switch positions are from the .brd (SW1 u=11.1, v=6.22; SW2 u=29.93, v=5.85); check the lid nubs line up.

## Print plan (draft)
PETG, 0.2 mm layers, 0.4 mm nozzle (Bambu A1), 3+ walls on lid tongues/tabs, ~15% gyroid, no supports.
Shell: front face on bed. Lid: outer face on bed. Slow down for the thin flexures, check first-layer squish.

## TODO
- [ ] Phase 3: final printability review + print recommendations
- [ ] Measure glass height, confirm switch type, update `case.py`
- [ ] Test print; check pegs, flexure feel, tab retention, USB alignment
- [ ] Re-sync button/wire positions if the firmware pin map changes
- [ ] Add `case/.venv/` to `.gitignore`; decide whether to commit viz_*/p_*.png
- [ ] Nice-to-have: wire notches in the switch cups, battery retaining ribs, lanyard hole

## Files
- `case.py` (model + export), `fitcheck.py` (interference check with stand-in parts + cutaway previews)
- `case_shell.stl`, `case_lid.stl`, `case_assembly.stl`, `*.step`
- `p_*.png`, `preview_assembly.png`, `viz_*.png/stl`: preview renders
- Previews were rendered with `preview.py` from `~/dev/3d` (not copied here)
