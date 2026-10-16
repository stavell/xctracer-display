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
      gfx/       1-bit renderer: Bitmap1, gray dither, glyph atlas, font, shapes
      sensors/   compass tilt + hard-iron, AHRS, heading smoother, mount table
      panel/     pure panel helpers: line diff, Sharp frame packing
      input/     button state machine
      util/      spsc_ring, types (Vec3, MagCal, finite-math guard)
      hw/        generic drivers: panel, button, battery, power, compass/thermo sensors, watchdog, pins
      ports.h    ports to the generic hardware (panel, input, battery, compass, thermo, power)
      app_host.h IApp (name/begin/end/tick/onInput) and AppHost (holds the active app)
    firmware/src/apps/vario/   the flight display
      core/      pure flight maths: parser, flight model, wind fit, thermal memory, heading selector, sim
      view/      draws a ViewModel onto an Adafruit_GFX canvas; no hardware, no state
      app/       the state machine (App) behind the vario ports in app/ports.h, VarioApp (IApp adapter)
      hw/        vario-only hardware: tracer BLE central, laptop BLE peripheral + protocol, flash config,
                 diagnostics
    firmware/src/main_vario.cpp   wiring only; one main_<image>.cpp per firmware image
    firmware/emu/        PC emulator: the same platform + vario core/view/app with mock ports, tests, screen renders
    host/                Python tools: replay, control, capture, analysis (.venv at the repo root)

## Rules (each one was learned the hard way)

- **Layering.** Everything except `hw/` directories (`platform/hw/`, `apps/*/hw/`) must compile on the PC:
  no `Arduino.h`, no Bluefruit, no `millis()`. Time is passed in. Hardware access goes through a port
  (`platform/ports.h`, `apps/vario/app/ports.h`). If logic in a `hw/` directory can be pure, move it to
  `core/` or `platform/sensors/` and test it. `platform/` never includes anything from `apps/`; an app
  includes platform headers as `"platform/gfx/bitmap1.h"` (paths from `-Isrc`).
- **BLE callbacks only copy bytes into queues.** No parsing, no maths, no drawing, no flash writes, no
  Bluefruit calls that the loop also makes. Parsing inside a callback once hung the device.
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
    cd firmware && ../.venv/bin/pio run              # device build (vario image: platform/ + apps/vario/ via build_src_filter)
    scripts/flash.sh                      # flash over USB: checks the port is free, verifies "Device programmed."

Before every commit: the emulator tests pass and the device build succeeds. Look at the rendered PNGs
after any view change.

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
