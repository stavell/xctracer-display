# CLAUDE.md

Guidance for AI agents (and humans) working in this repository. Read this before changing code.
`NOTES.md` holds measurements and findings; `PLAN.md` the current improvement plan; `README.md` the
user-facing overview.

## What this is

A handheld display for an XC Tracer Mini III GPS paragliding vario. Hardware: Adafruit Feather nRF52840
Sense + 2.7" 400x240 Sharp memory LCD (used in portrait, 240x400) + LiPo, one user switch. The box is a
BLE central to the tracer (`$XCTRC` text lines, about 3 per second) and a BLE peripheral for a laptop
(control commands, IGC replay, legacy frame drawing). It is a flight instrument: wrong data that looks
right is worse than no data.

## Layout

    firmware/src/platform/   reusable by every app (pure code compiles on the PC, only hw/ touches hardware)
      gfx/       1-bit renderer: Bitmap1, gray dither, glyph atlas, font, shapes, icons, ui (title bar, rounded boxes)
      sensors/   compass tilt + hard-iron, AHRS, heading smoother, mount table
      panel/     pure panel helpers: line diff, Sharp frame packing
      input/     button state machine, A+B combo hold, key debounce
      settings/  BoxSettings (time zone, mounting, magnetometer calibration over the key store), CompassCal (the calibration
                 flow) and its screen: shared by the vario and the Settings app
      storage/   map pack in a NOR flash: flash port (IFlash), flash header, USB serial frames + loader state machine, pack block reader
      util/      spsc_ring, types (Vec3, MagCal, finite-math guard)
      hw/        generic drivers: QSPI flash + PackService (map pack, USB loader), panel, buttons (+ keymap.h for the six front buttons), battery, power, compass/thermo
                 sensors, key store (boot app, high scores, shared settings), watchdog, pins
      app_registry.h  the apps the home screen lists (name, text, icon) and the start-up app; clock.h  wall-clock time of day
      ports.h    ports to the generic hardware (panel, input + key edges, battery, compass, thermo, power, key store)
      app_host.h IApp (begin/end/tick, input, wantsFrame/draw, gestures) and AppHost: THE FRAME LOOP (poll input,
                 tick, clear + draw + present when the app wants a frame), app switching, launcher gestures
    firmware/src/apps/vario/   the flight display
      core/      pure flight maths: parser, flight model, wind fit, thermal memory, heading selector, sim
      view/      draws a ViewModel onto an Adafruit_GFX canvas; no hardware, no state
      app/       the state machine (App) behind the vario ports in app/ports.h, VarioApp (IApp adapter)
      hw/        vario-only hardware: tracer BLE central, laptop BLE peripheral + protocol, flash config,
                 diagnostics
    firmware/src/apps/launcher/   the home screen (LauncherApp: status bar, icon grid, footer)
    firmware/src/apps/settings/   the Settings app (compass calibration, mounting, start-up app, time zone, About)
    firmware/src/apps/games/tetris/   rules, 1-bit view, TetrisApp
    firmware/src/main_vario.cpp   wiring only; one main_<image>.cpp per firmware image (main_vario: vario only,
                                  main_box: home screen + vario + Tetris + compass tool + Settings)
    firmware/emu/        PC emulator: the same platform + apps with mock ports, tests, screen renders, the PC player
                         (play.cpp: `build.sh play tetris|box`), box.h = the whole box over mocks
    host/                Python tools: replay, control, capture, analysis (.venv at the repo root)

## Rules (each one was learned the hard way)

- **Layering.** Everything except `hw/` directories (`platform/hw/`, `apps/*/hw/`) must compile on the PC:
  no `Arduino.h`, no Bluefruit, no `millis()`. Time is passed in. Hardware access goes through a port
  (`platform/ports.h`, `apps/vario/app/ports.h`). If logic in a `hw/` directory can be pure, move it to
  `core/` or `platform/sensors/` and test it. `platform/` never includes anything from `apps/`; an app
  includes platform headers as `"platform/gfx/bitmap1.h"` (paths from `-Isrc`).
- **BLE callbacks only copy bytes into queues.** No parsing, no maths, no drawing, no flash writes, no
  Bluefruit calls that the loop also makes. Parsing inside a callback once hung the device.
- **The host owns the frame loop.** An app never clears or presents the panel: it answers `wantsFrame(now)` and
  draws in `draw(bm, gfx, now)` onto a white canvas (`platform/app_host.h`). All redraw cadence rules (animation
  rates, half-pixel rules) live in the app's `wantsFrame`. The vario's list / calibration screens only draw on the tick
  after they were opened (as they always did): that is part of the cadence, keep it.
- **Flight safety of the box image.** The box boots straight into the start-up app (default: the vario, stored in the
  key store). The launcher opens only on a deliberate gesture from inside an app: the vario's device list has an
  "Home" row (long press, one short press to the row, long press), a long press on the Tetris pause screen, the compass
  tool's "Home" row, the Settings "Home" row, 3 s hold of the switch in games and tools, A + B held 1.5 s on the
  six-button hardware. The vario menu holds only flying things (tracer choice, Home); compass calibration is the
  Settings app's and runs the sensors only while its screen is open. Leaving the vario stops the tracer scan and link and the compass sensors
  (`App::end`, `ITracer::pause`); entering it again restarts them. Games never run unless chosen. The vario-only image
  (`feather_sense`) has no launcher and none of this (no Home row; calibration only by `ctl.py cal`). `scripts/check.sh` proves the vario draws the same frames in
  the box as alone (`box_vario_matches_alone`).
- **RAM budget.** .heap (what static RAM leaves over) feeds FreeRTOS: the box image once had 2 kB and did not boot at all.
  Only the start-up vario is a static app; Tetris, the compass tool, the flight sim (`SimBox`: SimApp + a 2-tile Terrain) and the vario's terrain kit share one
  `AppArena` through `LazyApp` (`platform/lazy_app.h`). Big new objects go there or into a port. `scripts/ram_check.py`
  (in check.sh) fails below 48 KB of heap.
- **The loop task has a 4 KB stack.** No large locals (view model, trails, buffers): make them static or
  members of global objects. `FlightModel::reset()` assigns members for this reason.
- **Never fill the whole screen with `fillRect`** (per pixel, 142 ms on this chip). The app calls
  `IPanel::clear()` before drawing; views assume a white canvas.
- **Every number shown goes through `fnum()`** in `apps/vario/view/screens.cpp` (non-finite prints `--`).
- **`float` for maths, `double` only for lat/lon and accumulations that need it.** The FPU is single
  precision; `double` is software.
- **Flight time comes from the data's time stamps**, not the wall clock, so replays at any speed behave.
- **Measure, do not assume.** Fonts, timings and layouts were wrong every time they were guessed. Render
  screens with the emulator and look at them; time things on the device via the serial status line.

## Build, test, flash

    firmware/emu/build.sh                 # emulator: all tests (sanitizers on) + screen PNGs in emu/out/
    firmware/emu/build.sh replay <lines> <outdir> [--hz 3 --inject --mix live.txt --thermal]
    firmware/emu/build.sh play box        # the whole box on the PC in an SDL window (keys: see README, "Box on the PC")
    firmware/emu/build.sh smoke           # headless scripted run of the box: launcher, vario, Tetris, tool -> emu/out/box_*.png
    cd firmware && ../.venv/bin/pio run              # device builds: feather_sense (vario only), feather_sense_diag, feather_sense_box
    scripts/flash.sh [env]                # flash over USB (default feather_sense): checks the port is free, verifies "Device programmed."
    scripts/stack_check.py                # static worst-case stack depth from loop() / setup() of the built images (needs pio run first)

Before every commit: `scripts/check.sh` (emulator tests, box smoke run, device builds, stack check). Look at the
rendered PNGs after any view change.

The vario must not change when the platform around it does: compare `build.sh replay ... ` output (it ends with a
behaviour digest of every present() time and frame) and the `out/*.pgm` renders before and after a refactor.

Line files for replays: `python host/make_lines.py igc <file.igc> --start S --seconds N` and
`python host/make_lines.py capture host/xct_capture.log`.

## Working with the device

- **Never leave a serial reader running in the background.** The bootloader uses the same USB serial
  port for uploads; a forgotten logger starved the upload and left the board stuck in the bootloader.
  Read the status for a fixed number of seconds, then close the port. Flash only with `scripts/flash.sh`.
- Serial status (115200, only printed when a terminal is attached): `st ...` (data, timing), `dev ...`
  (saved/connected tracer), `raw ...` (last line received), `cmp ...` (compass sensors).
- `python host/igc_replay.py <igc> --start 4185 --speed 3 --page thermal` replays a real flight.
- After killing a replay, wait about 10 s before reconnecting (the box holds the stale connection).
- `python host/ctl.py` sets the name filter, time zone, simulator, page, compass calibration.
- Saved settings live in the nRF's flash; changing a stored struct resets that file.

## Conventions

- Match the surrounding code: short functions, comments that say why, not what.
- New behaviour gets a test in `firmware/emu/tests.cpp`; new screens get a scenario in
  `firmware/emu/emu_main.cpp`.
- Record measurements and non-obvious findings in `NOTES.md`, not in commit messages only.
