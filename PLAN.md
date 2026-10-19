# Review and improvement plan

Review of the firmware and tools as of commit `7f3aea1` (written by one model, reviewed by another).
The layering (core / view / app / hw, ports, PC emulator with 207 checks) is sound and worth keeping.
The gaps below are what stands between "works on the bench" and "trustworthy in flight".

## Findings

Severity: **H** = can show wrong data in flight or corrupt state, **M** = robustness or maintainability
with a real failure mode, **L** = hygiene.

| # | Sev | Finding | Where |
|---|-----|---------|-------|
| 1 | H | No watchdog. If the main loop hangs, the memory LCD keeps the last frame and the VCOM timer keeps it alive, so stale numbers look live. A hang already happened once during development. | `main_vario.cpp` |
| 2 | H | Laptop drawing runs in the BLE callback task: it writes the shared framebuffer, calls `clear()` and `setRotation(0)` while the main loop may be drawing the vario screen (108 ms per frame). Interleaved writes and a rotation change mid-draw corrupt the frame. | `apps/vario/hw/laptop_ble.cpp` |
| 3 | M | The callback-to-loop queues use `volatile` indices only. Non-volatile payload stores may be reordered after the index store, so the consumer can read a half-written line or command. | `hw/tracer_ble.*`, `hw/laptop_ble.*` |
| 4 | M | `Bluefruit.Scanner` is driven from two tasks: `onDisconnect` (callback task) calls `startScan()` while the loop calls it from `update()` / `listOpen()`. The scan list `items_` is written in the scan callback and read in the loop without synchronisation; `cfg_.updateAddr()` runs in the callback. | `apps/vario/hw/tracer_ble.cpp` |
| 5 | M | Reconnect after a dropped link uses the low-duty scan (50 ms window per 1 s), so in flight the display can stay on "SEARCHING" for many seconds. | `apps/vario/hw/tracer_ble.cpp` |
| 6 | M | Pure logic lives inside hardware adapters and has no tests: name glob, device-matching policy (the same-name bug came from here), RSSI sort (duplicated twice), laptop packet decoding and its length checks. | `apps/vario/hw/tracer_ble.cpp`, `apps/vario/hw/laptop_ble.cpp` |
| 7 | M | Diagnostic benchmarks are compiled into the production firmware: a second `FlightModel` (about 10 KB RAM) and a one-off 120-sample run that blocks the loop when a terminal attaches. | `main_vario.cpp` |
| 8 | M | Config files are rewritten by remove-then-write; a power loss in between loses the saved tracer or calibration. | `apps/vario/hw/config_store.cpp` |
| 9 | M | No single check command; a commit (`3cd90a7`) did not build for the device. | repo |
| 10 | L | README describes the early "laptop renders everything" design (one-way link, host renders, out-of-scope sensors). NOTES.md is current. | `README.md` |
| 11 | L | Legacy laptop-drawing tools (`dashboard.py`, `send_test.py`, `bench.py`) are undocumented as such. | `host/` |
| 12 | L | Strict warnings: only int to int16_t narrowing in Adafruit_GFX calls (harmless). No unused code, no shadowing. | `apps/vario/view/screens.cpp` |

Known open items that need the hardware or the user, not code review: compass axis mapping, GPS speed
unit with a real fix, magnetic declination, power measurement, `DISPLAY_ROTATION`, DISP/EXTMODE wiring.

## Status

- A, B, C: done, reviewed and merged. Review found one race agent A missed (a stale disconnect could demote
  a freshly made link after picking a new tracer from the list); fixed in the merge.
- Watchdog: wired in, device verification pending (see NOTES.md).
- D: D4 (timer-sampled switch), D5 (fuzz, soak) and D7 (clock wrap) done and merged; the fuzz found five real
  bugs, all fixed (see NOTES.md). D1 (watchdog), D2 (stack margins), D3 (heap) and D6 (device soak) need the device.

## Work packages

Three independent packages, each on its own branch in its own worktree, touching disjoint files.
Every package must keep `firmware/emu/build.sh` green (all checks pass, sanitizers on) and keep
`pio run` building for the device. Agents do not flash the device; device checks are done afterwards.

### A. BLE adapters: concurrency and testable logic (findings 2, 3, 4, 5, 6)

1. `platform/util/spsc_ring.h`: header-only single-producer/single-consumer ring (`std::atomic` indices,
   release on publish, acquire on consume). Unit tests in the emulator.
2. Use it for the tracer line queue and the laptop command queue.
3. Laptop frames: decode into a private 12 KB buffer in the panel's native layout inside the callback;
   the loop copies it to the panel on FLUSH (new `IPanel::showRaw()`). The callback never touches the
   panel, the canvas or the rotation.
4. Tracer: the scan callback only queues scan reports; the loop merges them into the list, does the
   matching and calls `connect()`. `onDisconnect` only sets a flag; the loop restarts scanning.
5. Fast reconnect: after a disconnect, scan at full duty for 30 s, then fall back to low duty.
6. Move `glob`, the RSSI sort and the auto-connect policy into `apps/vario/core/devmatch.*`, and laptop packet
   decoding into `apps/vario/app/command_decode.*`, with tests (same-name devices, private addresses, short
   and overlong packets, unknown commands).

### B. Resilience and build hygiene (findings 1, 7, 8)

1. `platform/hw/watchdog.*`: nRF52 WDT, 8 s timeout, paused while halted by a debugger, fed once per loop and in
   every blocking wait (power-on gate, wait-for-release before System OFF).
2. Diagnostics: move the status line and benchmarks out of `main_vario.cpp` into `apps/vario/hw/diagnostics.*`; the
   benchmarks compile only with `-DDIAGNOSTICS` (a second PlatformIO env `feather_sense_diag`); the
   light status line stays in both builds.
3. Atomic config writes: write a temp file, then rename over the old one.

### C. Docs and tooling (findings 9, 10, 11)

1. `scripts/check.sh`: emulator build and tests, then the device build; non-zero exit on failure.
   `scripts/install-hooks.sh`: a git pre-commit hook that runs it.
2. Rewrite `README.md` for the current product (vario display): hardware and wiring, build and flash,
   using it (switch, pages, device list, calibration), architecture, testing and replay, host tools
   (legacy laptop drawing clearly marked), troubleshooting; link `NOTES.md` and `PLAN.md`.

### D. Reliability for a flight instrument (after A, B, C are merged)

The firmware already runs on FreeRTOS (the Adafruit core): `loop()` is a task with a 4 KB stack,
Bluefruit runs its own event and callback tasks, the VCOM toggle is a software timer. More tasks would
not make it more reliable by themselves (each one adds concurrency risk); these guards do:

1. Watchdog (reviewer): 8 s hardware WDT fed by the loop and inside blocking waits; the reset cause
   (watchdog, hard fault, power-on) is reported at boot. Verified on the device: uploads still work
   (the bootloader feeds a running WDT), the reset button recovers it, System OFF does not wake up.
2. Stack margins: the core's stack-overflow hook only logs and returns in release builds (execution
   continues on a corrupted stack), so measure instead: report the stack high-water mark of the loop task
   and the Bluefruit tasks in the status line; keep at least 1 KB free under the heaviest page.
3. No heap after start: the panel's snapshot buffer is the only `malloc`; check heap use at boot and after
   a long run is identical (the malloc-failed hook also just returns).
4. Input never lost: sample the switch from a 10 ms FreeRTOS software timer into the button state
   machine, so a press during a 108 ms redraw still counts.
5. Fuzz and soak in the emulator: random and malformed lines, truncated and huge fields, time jumps,
   link drops, the whole 3 h flight replayed several times; sanitizers on, no NaN on screen, bounded tick.
6. Device soak: 2 h of replay with page switches and reconnects; no resets, stable stack and heap.
7. Review time arithmetic for `millis()` wrap (49.7 days): unsigned differences only.

### Verification (done by the reviewer, not the agents)

For each branch: read the diff, run `scripts/check.sh` (or its two steps), check the new tests really
exercise the change, merge. After all three: flash, run the 3x IGC replay on both pages and watch the
serial status, and check that a stuck loop is reset by the watchdog.

## Multi-use box (proposed 2026-10-16; restructure, Tetris, frame loop, launcher and box image done)

The same hardware as a flight display, a game console and a generic gadget, with firmware switched
between seasons.

**Firmware structure**

    platform/   hw (panel + DMA, sensors, BLE, power, flash settings), gfx renderer, input (switch,
                later 4 buttons), fused compass, app lifecycle + switching, settings UI
    apps/vario  the flight display (apps/vario/core maths + views)
    apps/games  breakout, soaring sim, ...
    apps/tools  standalone compass + temp/humidity, level, ...

- An app implements start / stop / tick / draw / input; the platform owns screen, cadence and power.
- Two images from one tree: `vario` (flight app + standalone compass, no game code: less to go wrong in
  the air) and `console` (launcher with games and tools, optionally the vario too).
- Switching: UF2 drag and drop (double-tap reset -> FTHR840BOOT drive), plus a "Switch firmware" menu
  item calling `enterUf2Dfu()`, so the reset button can stay inside the case. BLE OTA DFU later.
  Settings live in the internal LittleFS area, which a UF2 app update should not touch (verify on
  the device). Release both .uf2 files on GitHub.

**Order:** move code into platform/ and apps/vario (pixel-identical screens, all tests green) - DONE;
app interface + launcher; two build envs + switch item; game engine basics + Breakout.
The watchdog is deferred (it stays disabled in `main_vario.cpp`, see NOTES.md; decided 2026-10-16).

**Done: restructure (zero behaviour change).** `src/platform/` (gfx, sensors, panel, input, util, hw,
ports.h, app_host.h) and `src/apps/vario/` (core, view, app, hw); `main_vario.cpp` registers one app
(`VarioApp`, an `IApp` adapter around `App`) with `AppHost`. Screens, test output and a 10-minute replay
are byte-identical to before; flash/RAM within 400 bytes.
**Done: Tetris on the PC.** `apps/games/tetris/` (rules, view, `TetrisApp : IApp`), the SDL player
`firmware/emu/play.cpp` (`build.sh play tetris`), 13 test groups, screen renders `emu/out/tetris_*.png`.
`IApp` gained optional `onKey(key, down, ms)`, `wantsFrame`, `nextWakeMs`, `draw` (defaults do nothing, the vario
is untouched) and `KEY_B`. Still open for the device: the host calls `draw` and presents; D-pad / A / B wiring;
a settings port for the high score (`IScoreStore` is the seam); the `console` image and launcher.
**Done (2026-10-16, emulator + builds, not flashed): the box.** (1) The frame loop moved into the platform:
`AppHost` polls the switch (key edges, short / long / hold, held time), ticks the app, and clears / draws /
presents when `wantsFrame`; the vario's cadence rules are `App::wantsFrame` (proved identical: same frames at the same
times). Key edges and a keymap for the six front buttons are in (`platform/hw/keymap.h`, pins undefined). (2) `LauncherApp`
(icons, battery, version, "Start-up" row stored in flash), image `feather_sense_box` = vario + launcher + Tetris +
compass tool (a second `App` without tracer or laptop); `feather_sense` is unchanged. Entry to the launcher: the vario's
device list "Apps..." row, 3 s hold in games and tools, A + B for 1.5 s on the six buttons. Leaving the vario stops
tracer scan / link and the compass sensors. (3) `build.sh play box` (PC player with IGC replay, compass, keys, press
timing in the title) and `build.sh smoke`. (4) `scripts/stack_check.py` in `check.sh` (it found the 1.8 KB `fitWind`
frame; fixed). Details and sizes: NOTES.md, README "Box".
**Next:** (1) try the box image on the device (boot into the vario, device list -> Apps..., Tetris and back, tool,
start-up row, tracer reconnect time after returning; read `st` timing); (2) wire the six buttons (`PIN_KEY_*`) and
move the combo and D-pad into the vario's menus; (3) `apps/tools` as a real directory (compass is a `VarioApp` flavour
for now) and a settings UI; (4) move the mag-cal and mount-index storage out of `ConfigStore` / `ISettings` into the
platform key store, leaving the tracer filter and saved device in the vario; (5) a BLE-quiet mode for games (the
laptop peripheral keeps advertising in every app); (6) reduce the duplicated FreeSansBold24 bitmaps (about 8 KB).

## Terrain and map (study 2026-10-16, steps 1 and 2 done)

**Done (step 1):** map pack format + writer (`host/terrain/mkmappack.py`, `FORMAT.md`), pure code in `firmware/src/platform/terrain/`
(IBlockReader port, `Terrain` elevation/ceiling/hillshade/contours, `MapVectors` towns + roads), tests in
`firmware/emu/terrain_tests.h` (fixture `firmware/emu/fixtures/sopot.pack`), real pack 1.72 MB, preview renders.
**Done (step 2):** AGL in the view model and on the live pages, map page, `TerrainKit`, PC player `--pack`, tests (see NOTES.md). Device wiring waits for the reader.
**Done (2026-10-18, builds + emulator, not flashed): device pack storage.** QSPI driver on the core's nrfx (`platform/hw/qspi_flash`),
pack header + serial frames + loader state machine (pure, `platform/storage/`, tested on an in-memory flash), `PackService` (boot: header
-> terrain, `pack ...` status line, USB loader), `host/load_pack.py`, wired into `feather_sense` (static kit) and `feather_sense_box` (kit
in the app arena while the vario runs). **To do on the device:** flash, `load_pack.py bulgaria.pack`, read the `pack ...` line (flash id,
quad, MB/s), fly the map page; measure the AGL/map-page frame times and the power draw (then enable `setIdleSleepMs`). See NOTES.md "Map pack in the QSPI flash".
**Next (after the device test):** (5) below was the QSPI step (done); then the sim on the device (done: always in the box image, see NOTES "RAM budget").
Original list after step 2: Original list: (2) AGL cell on the vario page (`elevation`/`ceiling` under the GPS fix, margin for the 6.6 m RMS / 80 m worst mean
error, ceiling for "terrain above me"; a stale or missing value prints `--`); (3) map page in `apps/vario/view` (relief bands into
a grey buffer, dither, contours, `RoadIter` + `TownIter` overlay, Latin names first, Cyrillic needs the glyph atlas),
redraw only on position/heading change; (4) box player integration (pack file in the emulator via `MemReader`);
(5) QSPI driver (`IBlockReader` on the nRF52840 QSPI, XIP option) and loading the pack over USB or BLE with
`Terrain::verifyAll()` after the transfer.

## Launcher refactor (requested 2026-10-16; done in the emulator and device builds, not flashed)

- Launcher = the box's home screen: icon menu (Vario, Tetris, Compass & weather, Settings), status line (time, battery,
  map pack). Boot still goes straight to the start-up app (default vario).
- Vario menu (long press) holds only flying things: XC Tracer selection (scan list, save) and "Home" (to the launcher).
  The "Apps..." row goes away.
- Settings is a platform app: compass calibration (generic, used by every app), board mounting index, start-up app,
  time zone, About (version, map pack info, OSM / Copernicus attribution). ctl.py cal/orient talk to the platform.
- Leaving any app: its menu's "Home" item, or A+B held on the six-button hardware.

## Centring page (prototype 2026-10-16, emulator only)

Done: `core/thermal3d.*`, the centring page, cue measurement (NOTES.md). Next: fly it (replay on the device with `ctl.py page center`), tune the
25 / 50 m rings and the 8 m no-arrow threshold with a pilot; make the cue available earlier than two turns (a better first-turn
estimate); (done 2026-10-17: the start-up wind estimate that locked on 42 km/h, NOTES.md "Wind EKF locked high"); try the two buttons (A/B) for zoom.

## Clock in sleep (idea 2026-10-16)

Use the memory LCD's zero-power hold for a clock when the box is "off". The nRF52840 RTC (32.768 kHz crystal,
~20 ppm) keeps running in System ON sleep but stops in System OFF, so the 3 s "off" gesture becomes a clock
sleep: wake once a minute, redraw the digits, VCOM 1 Hz via RTC/PPI/GPIOTE without the CPU. Time comes from the
tracer's UTC (every $XCTRC line) or ctl.py, plus the stored time zone; `--:--` until set. True System OFF stays
for storage (longer hold, or critical battery). Estimated floor ~80-100 uA (Feather LDO ~55 uA and the VBAT
divider ~20 uA dominate): about a year on the ~1000 mAh LiPo. Measure before promising.

- Launcher = the box's home screen: status bar (local time from the tracer's GPS time, map pack, battery), a 2-column grid
  of icon tiles (Vario, Tetris, Compass & weather, Settings), footer with the chosen app's line and the key hint. Apps
  register in `platform/app_registry.h`. Boot still goes straight to the start-up app (default vario).
- Vario menu (long press) holds only flying things: XC Tracer selection and "Home" (box image only). "Apps..." and
  "Calibrate compass" are gone from it. The compass tool's menu is "< back" and "Home".
- Settings (`apps/settings` on `platform/settings/`): Compass calibration, board mounting index, start-up app, time zone,
  About (version, map pack, Copernicus / OSM credits), Home. Mag calibration, mounting and time zone moved from the vario's
  ConfigStore to the platform key store (`BoxSettings`), with a one-time import of the old files. `ctl.py cal / orient / tz`
  are served by the vario (the only app that reads the laptop queue), through the same platform code and store.
- Leaving an app: its menu's "Home" row (vario, tool, Settings), the Tetris pause screen (long press or B), the 3 s hold,
  A+B held 1.5 s on the six-button hardware.
- Open: laptop commands are only read while the vario is active (as before); a platform-level command reader would let
  `ctl.py cal` work from the home screen. The home grid shows six tiles at most (two columns, three rows).

## Games: maximum frame rate (user rule, 2026-10-16)

**Done in the platform (2026-10-16): game mode.** `IApp::frameIntervalMs()` / `step(simMs, dtMs)`, `IPanel::ready()`, `AppHost`
frame clock with catch-up (<= 4 steps), `AppHost::stats()` (fps, worst gap, steps, dropped), `loopDelayMs()` (1 ms in game mode),
`SharpPanel::ready()` = DMA done, main_box delay from the host. The PC player shows fps, worst gap, slowest tick and late frames in the title.
Tetris keeps its change-driven cadence (no animation worth 50 fps); the vario is unaffected (screens and digests identical). Still open:
check on the device that 50 fps motion looks sharp and that frame times stay under 20 ms; dirty-line tracking while drawing.

Games run at a steady 50 fps (user target: 20 ms per frame; do not aim higher); no power-saving cadence while a game runs.
- Platform game mode: fixed-timestep loop, a new frame as soon as the previous DMA transfer is done (vario keeps its
  change-driven cadence). Show fps and the worst frame time.
- 50 fps fits the pipeline: full frame 12.5 KB = ~13 ms at 8 MHz SPI, overlapped with drawing by DMA; no need for a
  faster SPI clock. Check on the device that 50 fps motion looks sharp (moving-bar test) and that frame times stay
  under 20 ms.
- Renderer: native-layout sprites/tiles, dirty-line tracking while drawing (no 12 KB memcmp per frame).

## Flight simulator (idea 2026-10-16, after the thermal 3D page)

**Step 1 done (2026-10-16, PC player, emulator and device builds; not flashed): game mode, sim core, top-down and 3D views,
tilt steering with a mouse emulation, tests (about 570 checks), docs (README "Games: Flight sim", NOTES "Flight simulator, step 1").**
Next: (1) run it on the device (needs the QSPI map-pack reader; the box image already carries the app and says "needs the map pack"),
measure the 3D frame with the cycle counter (estimate 12-14 ms of 20 ms, NOTES); (2) wire the AHRS to `ITilt` (roll and pitch of the
screen plane, calibrated zero) and the D-pad; (3) feed the real vario app ($XCTRC lines from the sim: position, altitude, vario,
speed, wind) so the vario can be tested against known air; (4) modes: XC tasks, ghost race against an IGC; (5) looks: forest / rock tone,
clouds at the cloud base, canopy edge, a bigger glider symbol; (6) RAM: shrink the window to 96 x 96 if the pack reader leaves too little.

Paragliding sim on the box: real Bulgaria terrain from the map pack, tilt control via the AHRS (bank = turn,
pitch = speed bar), glider polar + pendulum lag, thermals from terrain/sun triggers drifting with wind, ridge lift
from wind x DEM slope, lee sink, cloudbase; it feeds the real vario app (same $XCTRC stream as the built-in sim).
Views: top-down (map page + thermals) first, then a first-person voxel-space renderer (PC prototype + M4 estimate).
Modes: free flight from real takeoffs, XC tasks, ghost race against your own IGC. PC first in the box player.
Orientation: PORTRAIT (decided 2026-10-16): thumbs on D-pad + A/B as the case is built; voxel cost scales with
columns (240 vs 400) and a portrait screen column is a contiguous native row (byte writes), which matters for 50 fps;
same orientation as the vario pages the sim feeds; tall view suits looking down/ahead; ~70 deg horizontal FOV;
bank from the gravity direction in the screen plane. Landscape stays possible (renderer supports rotations): compare in
the PC prototype if portrait feels cramped.

**Playable air, chase camera, Day settings (2026-10-17, branch worktree-agent-a7f0bf8ddea82e02c).** Done: reachability tool and
playability tests (92 % of 288 scripted flights climb >= 500 m in 10 min, every launch has >= 2 thermals reachable with margin),
house thermals, tuned medium day, wing-tip lift feel, chase camera with glider sprite (replaces first person), three NE takeoffs
(Shumen, Madara, Nevsha), "Day" screen and pause rows (thermals weak..epic, wind direction and speed). Numbers in NOTES.md.
Next: look at the chase sprite on the device panel (halo cost), tune weak days (the scripted pilot only gains 500 m), a "reachable
thermals" cue for the assist (arrows instead of rings), ridge lift from a smoother terrain scale, take the usual wind per site from
real flying records.

## Backlog (2026-10-19)

Lessons from the sim speed pass (NOTES "sim speed"): on this chip libm (`expf` 1300 cycles, `powf` 2540, `logf` 1260),
`snprintf` with floats, soft `double` and byte-wise memory loops (memset 6.3, memcpy 8.1 cycles/byte) are the cost, not
the clock (64 MHz, cache on). Shared platform fixes (word-wise clear / pack / diff) already reach the vario.

Vario, by expected gain (measure on the device first; `box_vario_matches_alone` and replay digests must not change):
1. **Map page maths**: DONE (NOTES "Float map maths and integer number text"): ground track, makeXform, boxForWindow,
   windowCentre, update, split, sim TerrainSource now float relative to the double origin; renders byte-identical. Still to
   measure on the device (map page redraw and frame time with a long track).
2. **Number text**: DONE: `fnum()` uses `fmtFixed` (integer maths, tested against snprintf). Device timing still open.
3. **libm in wind / thermal code**: dropped, the premise does not hold (NOTES, same section).

Sim leftovers:
- 3D at ~31 fps, needs ~7 ms more for 50 (voxel 11, sprite 4.6, sky 3-4, HUD 2.5 ms); map ~30 thermal rings ~6 ms,
  glider symbol 2.9 ms.
- BLE (laptop advertising / link) off while a game runs (user asked; power, not speed).
- One 2.7 s stall on a flight restart after landing (height cache refill) seen once, not investigated.
- Recalibrate `build.sh simbench` against the device numbers.
- Map view edge strips (<= 4 px) can show blank until the next north-up picture swaps in.

CPU options (NOTES "CPU options pass"): done `-fno-math-errno`, per-file `-O3` (3D 37 -> 41 fps). Dropped: code in RAM (slower), LTO (archives do not link), DSP intrinsics (float inner loops, would change renders).

Other open items: measure the clock-sleep current (PPK2), wire the six buttons, print the case test plate, loader
speed-up (pump the loader in a tight loop during a load), QSPI deep power-down measurement, launcher footer polish.

User additions (2026-10-19):
- **Flight sim HUD: flight time and distance from takeoff** (straight-line from the takeoff point, km with one decimal;
  flight time from the sim clock, so x3 time shows sim minutes). Visual, no extra text labels beyond the standard units.
- **A 3D compass** instead of the flat dial: a tilted, perspective compass card / globe that rolls and pitches with the
  box (AHRS), shaded with the gray dither. RAM is not the limit: the compass tool lives in the shared app arena
  (91 kB block, the tool uses 43 kB) and the heap has ~80 kB. Put it in the Compass & weather tool first; the vario's
  compass page stays the flat, readable dial unless the user wants it there too (flight rule: practical, eye candy in
  the tools / games). Same 0.0..359.9 heading text.
- **Start-up animation** that shows off the box's graphics and sets expectations (user, 2026-10-19): a short (~2-3 s,
  skippable with any press, 50 fps) intro at power-on and on wake from System OFF. Storyboard (user): the sim's chase cam
  behind a paraglider thermaling tight just under cloudbase over the real 3D terrain from the map pack, climbing into the
  cloud; the cloud fills the screen as dithered white (the end frame, with the logo / name showing through), and the
  white-out is the transition into the start-up app. Rules: never delays the vario beyond the animation (tracer scan starts in parallel),
  runs from the shared arena (freed before the start-up app is built), falls back to a 2D logo animation without a
  map pack, off in the vario-only image. A Settings row to turn it off.
- **Sun shading on the vario's map page**: done 2026-10-19 (see NOTES.md, "Map page: no more 1.9 s stall"); the sim's top-down map could still use the same sun.
