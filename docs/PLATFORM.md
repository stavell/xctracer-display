# Platform guide: building another app on the box

The box is an nRF52840 board with a 240x400 1-bit panel, one switch (six buttons when the case has them), a gyro / accelerometer /
magnetometer, a thermometer and a barometer. The vario is one app on it. This page is the entry point for writing another one
(a tool, a game that steers by tilt, a gadget). Read `CLAUDE.md` first: its rules (layering, RAM, loop stack) apply to every app.
`NOTES.md` has the measurements behind the numbers below. The working example is `firmware/src/apps/examples/level/` (Level, a
bubble level, 104 lines): open it next to this page.

## 1. What the platform gives you

### The frame loop (`platform/app_host.h`)

`AppHost::tick(now)` runs every 10 ms (1 ms while a game is active). One iteration: poll input, call `onKey` / `onInput` / `onHeld`,
call your `tick(now)`, apply a pending app switch, then draw. **An app never clears or presents the panel.** The host clears it
to white, calls `draw(bm, gfx, now)` and presents (the panel only refreshes when the picture changed). Everything is pure C++:
the emulator and the PC player run the same host.

### The IApp contract

| Method | What it is for |
|---|---|
| `name()` | the name `APP <name>` and the registry use |
| `begin(now)` / `end()` | become / stop being the active app. Start sensors in `begin`, stop them in `end` (power) |
| `tick(now)` | every host iteration: poll sensors, run state machines. No drawing |
| `onInput(key, ev)` | the switch: `BTN_SHORT` and `BTN_LONG` on release (long = 0.7 to 1.5 s; a release between 1.5 and 3 s is cancelled). `BTN_HOLD` only if `handlesHold()` |
| `onKey(key, down, ms)` | key edges of the six buttons (and of the switch as `KEY_MAIN`) for held keys, auto-repeat |
| `onHeld(ms)` | how long the switch has been down |
| `wantsFrame(now)` / `draw(bm, gfx, now)` | change-driven drawing: say when a frame is needed, draw it on a white canvas |
| `nextWakeMs(now)` | when you next need the host (lets the device sleep) |
| `ownsPanel(now)` | something else draws (the laptop's frame mode); the host then neither clears nor presents |
| `frameIntervalMs()` / `step(simMs, dtMs)` | **game mode**, below |
| `gameInfo(dst, cap)` | your part of the once-a-second `gm` status line |
| `handlesHold()`, `comboExit()` | gestures, see section 1.2 |
| `sheetBlocked()`, `sheetStart()`, `onSheet(open, now)` | system sheet hooks, see 1.2 |

**Game mode**: return `frameIntervalMs() > 0` (games use 20: 50 fps) and the host keeps a frame clock. Each due frame it calls
`step(simMs, 20)` once (at most twice when late: the simulation stays in real time, the picture is the newest state), then clears,
calls `draw` once and presents. Physics goes in `step`, input handling in `onKey` / `onInput`, drawing in `draw`. `wantsFrame` is
not used. `host.stats()` gives fps, worst gap, dropped slots. A game that cannot keep up runs in slow motion (`kMaxCatchUp = 2`),
it does not stutter the clock. While a game is the active app the host also turns the laptop BLE link off (`ILinkGate`).
`frameIntervalMs() == 0` (default) is change-driven: the vario, Tetris, Settings.

### The input grammar

Every app must be usable with the switch alone and with the six buttons alone; both are always live.

| Gesture | Switch | Six buttons |
|---|---|---|
| move / next | short press | UP / DOWN (LEFT / RIGHT) |
| do / select | long press (0.7 s) | A (`KEY_ACTION`) |
| back / cancel | (none: use the sheet) | B (`KEY_B`) |
| system sheet (Back, Home, Sleep, Power off) | hold 3 s | A + B held 1.5 s |
| power off (System OFF) | hold 6 s (the sheet shows a bar) | |

The host handles the last two rows; an app only sees the events before them. The sheet is the one way out of every app (the cursor
starts on Home; an app can start it on Back with `sheetStart()`, the vario does). While it is open the host feeds the input to the
sheet only, keys that were down are released to the app, game-mode apps are not stepped (`onSheet(true)`: pause; `onSheet(false)`:
resume, the frame clock restarts), change-driven apps keep ticking. `sheetBlocked()` = true lets hold and combo reach the app
(only the start-up animation, where they skip it).

Mapping both devices in an app, the existing way: handle `onInput(KEY_MAIN, BTN_SHORT)` and the matching `onKey(KEY_xxx, true)` the
same way (Level toggles on both; Compass swaps pages on short / DOWN / RIGHT / A), and word hints by
`nav()->sixButtons()` (true while the last key came from a button), so a hint never names a control that was not touched
(`LauncherApp`, `SettingsApp`, Level). `nav()` also gives `openLauncher()`, `openApp(i)`, `appName(i)`. Flight-safety rule: only a
deliberate gesture leaves the start-up app; never open the launcher on a plain press.

### App registry, LazyApp, AppArena

`platform/app_registry.h`: `registry.add(title, sub, about, appIndex, icon, startable)` makes the tile on the home screen
(`appIndex` = the index `host.add()` gave it; `startable = false` keeps it out of Settings' start-up choice). Icons are the
`IconId` list in `platform/gfx/icons.h`; `ICON_GENERIC` serves until you draw one. At most 8 apps (`AppHost::MAX_APPS`,
`AppRegistry::MAX`). The AppHost, not the registry, owns the list.

**RAM rules.** The box links with about 72 kB of heap and FreeRTOS lives on it; below 48 kB `scripts/ram_check.py` fails the build.
So no app is a static object. Apps live in **one `AppArena`** (`platform/lazy_app.h`) sized for the largest of them
(`maxSize(sizeof(A), maxSize(...))` in `main_box.cpp`); `LazyApp<T>` constructs `T` in the arena in `begin()` and destroys it in
`end()`. Consequences: your state starts afresh at every open (keep what must last in the key store / `BoxSettings`); what must
outlive your app (radios and their queues, settings, the wall clock, the pressure log) is static in `main_box.cpp`; interrupt and
BLE callbacks never touch an app object. An app smaller than the arena costs only its code; a bigger one raises the arena for
everybody, so check `scripts/ram_check.py` and put big buffers behind a port.

## 2. Ports (`platform/ports.h`)

An app takes ports by reference in its constructor; the device implements them in `platform/hw/`, the emulator with mocks
(`emu/mocks.h`: `Rig` holds one of each). Never include `Arduino.h` outside `hw/`.

| Port | Gives | Owner |
|---|---|---|
| `IPanel` | `gfx()`, `bitmap()`, `clear()`, `present()`; used by the host, not by apps | host |
| `IInput` | switch events, `heldMs`, key edges; used by the host | host |
| `IBattery` | `update(now)`, `percent()` | main |
| `ICompass` | accel (m/s^2), mag (uT), gyro (rad/s) in the board frame, `enable(bool)`. Apps use it through `MotionService` | the app that enabled it (one at a time) |
| `IThermo` | air temperature / humidity, every 2 s (`readNow` for an immediate one) | main |
| `IBaro` | pressure in hPa on demand (`readNow`) | main (the `WeatherLog` sampler) |
| `IPower` | `shutdown()` (clock sleep), `powerOff()` (System OFF); the sheet calls it | main |
| `IKeyStore` | a few persistent numbers by `StoreSlot` (start-up app, high scores, time zone, mounting, calibration). Add a slot at the end of the enum for new data; a changed meaning resets nothing, a new slot reads 0 | main |
| `ILinkGate` | laptop BLE off while a game runs | main |

## 3. Services

**MotionService** (`platform/sensors/motion.h`): the orientation sensors as one object. Construct with `(ICompass&, BoxSettings&)`;
`start()` in `begin`, `poll(now)` in `tick`, `stop()` in `end`. It applies the case mounting and the stored magnetometer
calibration, runs the tilt-compensated compass and the gyro fusion (`Ahrs`) and picks the heading. Outputs: `heading()` (compass
direction of the top of the screen, 0 = magnetic north), `tilt(&roll, &pitch)` in degrees from level (0/0 = lying flat; pitch = top
edge up positive, roll = right edge down positive), `quaternion(q)` (w x y z, device to world east-north-up), `accel()`, `gyro()`,
`valid()`, `fused()`, `trust()` (field quality). One service per owner, about 0.7 kB, no sensor access outside it. A game that
steers by tilt:

```cpp
void MyGame::begin(uint32_t) { motion_.start(); }
void MyGame::end() { motion_.stop(); }                  // sensors off when the app is left
void MyGame::tick(uint32_t now) { motion_.poll(now); }  // reads at most one sample
void MyGame::step(uint32_t, uint32_t dtMs) {            // fixed 20 ms step
  float roll, pitch;
  if (!motion_.tilt(&roll, &pitch)) return;             // no gravity direction yet
  vx_ += clampf(roll, -20, 20) * 0.5f * dtMs * 0.001f;  // right edge down: a ball rolls right (a bubble would go left)
  vy_ += clampf(pitch, -20, 20) * 0.5f * dtMs * 0.001f;  // top edge up: the ball rolls down the screen (y grows downward)
}
```

The flight sim has an older `sim::AccelTilt` (upright box = zero, own smoothing); new code should use `tilt()` and smooth itself
(Level: `x += 0.25 * (new - x)` per step). `tilt()` is raw: filter it.

**WeatherLog** (`platform/power/weather.h`): 24 h of barometric pressure, one sample per 10 min, plus the 3 h tendency and a
forecast class; sampled by the main loop, shown by the clock sleep and the compass app. **Wall clock** (`platform/clock.h`):
time of day and date from the tracer's GPS or `TIME` from the PC; reads "unknown" after 30 days, never a made-up time.
**Retained state** (`platform/power/retained.h`): the pressure log and the clock survive reset, `REBOOT` and System OFF in a RAM
page at the top of RAM (`.retained`, 0x2003f000, taken from the heap by `platformio.ini`; a `.noinit` low in RAM is rewritten by the bootloader on every reset); not a USB flash (DFU) and not battery removal. **BoxSettings** (`platform/settings/box_settings.h`): time zone,
case mounting (0..23), magnetometer calibration, intro on/off, over the key store; Settings owns the editing, you read.
**ILinkGate**: see above; implemented in `main_box.cpp`, nothing for an app to do.

## 4. Graphics (`platform/gfx/`, `platform/gfx3d/`)

Portrait 240x400. The canvas is white when `draw` starts; ink is `DRAW_CLEAR` (black), paper `DRAW_SET`, `DRAW_INVERT` flips.

- **Bitmap1** (`bitmap1.h`): the native buffer with rotation. `pixel`, `hline`, `vline`, `rectIncl`, rotation-aware; per-pixel calls
  are the slow path. Never `fillRect` the whole screen (142 ms): the host already cleared it.
- **Gray** (`gray.h`, `shapes.h`): 17 dither levels, `Gray(0)` black to `Gray(16)` white, 8x8 Bayer. `fillRect(bm, ..., Gray(g))`,
  `fillDisc`, `fillRing`, `blitGray` for sprites with transparency. Dither is pixel exact and stable across frames.
- **Fonts** (`font.h`): `Font1` renders a GFX font once into per-glyph atlases for one rotation and draws with byte copies
  (GFX's `drawChar` is per pixel). Blocks are **refcounted and shared**: building the same font again for the same rotation points
  at the live block, so apps do not pay twice. Keep `Font1` objects as members (never on the 4 KB stack). `UiFonts` (small, bold,
  mid) is the box's standard set.
- **ui** (`ui.h`): `titleBar` (black bar, title, battery; optional badge), `hintLine` (the one bottom hint of every non-game
  screen), `fitFont` (first font that fits a width), `centered`, `wrapped`, `roundFill` / `roundFrame`, `bar`. Use them so the box's
  screens look alike. Constants `ui::W`, `ui::H`, `ui::BAR_H`, `ui::HINT_TOP`.
- **Widgets** (`gfx/widgets/compass_dial.h`): the heading-up compass dial, drawn through a pen so it serves both the vario's
  canvas and a plain Bitmap1.
- **gfx3d** (`platform/gfx3d/`): the sim's engine as platform code. `voxel` (heightmap terrain renderer), `sky3d`, `chase`
  (chase camera and glider sprite), `height_cache`, `air`, `world`, `glider`. Reads terrain through `terrain_source.h`
  (the map pack in the QSPI flash; `platform/terrain/`). `apps/games/sim` and `apps/intro` are the users to copy from.
- Also: `platform/gfx/icons.h` (48 px tile icons), `atlas.h` (cached bitmaps), `strokes.h` (stroked glyphs for projected text),
  `compass3d` (the dome of the compass app, `apps/compass3d`).

## 5. Performance rules (measured: NOTES.md "Drawing speed", "Sim speed pass", "CPU options pass")

- The nRF52840 has a single-precision FPU and no libm acceleration. Cycles: `sqrtf` 17 (inlined with `-fno-math-errno`), `sinf` 80,
  `tanf` 230, `atan2f` 300, `fmodf` 170, `expf` 1300, `logf` 1260, `powf` 2540. Keep `expf` / `powf` / `logf` out of per-pixel and
  per-frame loops (use tables, `fexp` / `fpow` in `gfx3d/sim_math.h`). `double` only for lat / lon and accumulations.
- newlib-nano `memset` / `memcpy` are byte loops (6 and 8 cycles per byte): use `platform/util/fastmem.h` (`fastFill`, `fastCopy`,
  `fastEqual`) for buffers of a few dozen bytes and up.
- `HOT_FILE_O3` (`platform/util/hot.h`) as the first line of a `.cpp` compiles that file at `-O3` on the device. Measured gain only
  on loops that dominate a frame (voxel -16 %). Code in RAM, LTO and DSP intrinsics were measured and dropped.
- Games: 20 ms frames (50 fps) are reachable for map views (12 ms draw); 3D is about 20 ms and sits at 40-45 fps. A SPI transfer
  of a frame overlaps the CPU work of the next. Look at the `gm` status line (fps, draw, step, dropped) before trusting a number.
- Never fill the whole screen with `fillRect`; never draw from a BLE callback; the loop task has a **4 KB stack**: no big locals
  (view models, buffers, trails), make them members of the app. `scripts/stack_check.py` proves the worst call chain stays under 3 KB.
- Every number you print goes through a finite check (the vario's `fnum()` prints `--` for NaN / inf). Wrong data that looks right
  is worse than none.
- Measure on the device, not by guess: fonts, timings and layouts were wrong every time they were guessed.

## 6. Testing

- `firmware/emu/build.sh`: the whole emulator (sanitizers on), all tests, and screen renders `firmware/emu/out/*.png`. Tests are
  rows of the `kTests` table at the end of `emu/tests.cpp`, one process per row, all cores (`out/tests --list`,
  `out/tests --only NAME`). Keep a test under about a minute of CPU; a heavy one splits its work with `forkEach` (`emu/sim_tests.h`).
  Tests must not assert on wall-clock time. A new test: one `static void run()` in `emu/<name>_tests.h`, `#include` it in `tests.cpp`,
  add a row.
- A new app's test starts from `emu/level_rig.h`: a bare `AppHost` with the system sheet over the `Rig` mocks (set
  `rig.compass.roll / pitch / psi` to tilt the box, `rig.input.press(at, ms)` for the switch, `setKey` for the buttons) and
  `emu/level_tests.h`, which checks cadence (`host.stats()`), input, the sheet and that `end()` switches sensors off.
- Screens: write the app's view as a scenario in `emu/emu_main.cpp` (see `emu/level_shots.h`); render and **look at the PNG**.
- `firmware/emu/build.sh smoke` runs the whole box headless (launcher, vario, Tetris, tools); `build.sh play box` plays it on the PC
  in an SDL window (keys in the README, "The box on the PC").
- Device bench, over the USB serial of a flashed box (no Bluetooth needed): `APP <name>` / `APP?` enter an app by name through
  the host's normal switch; `TIME <epoch> <tz minutes>` sets the clock (`host/settime.py`); `REBOOT` saves the retained page and soft-resets; status lines while a terminal is attached: `gm ...` per second while a game
  runs, `wth ...` every 5 s (pressure, temperature, battery, `log=` samples, `ret=` none / ok / crc), `pwr ...` at boot (the last run's event ring, `platform/power/pwr_log.h`), `cmp ... rec=N` (compass bus recoveries). Read the port for a fixed number of
  seconds and close it; never leave a reader running, the bootloader needs the port. Flash only with `scripts/flash.sh`.
- `scripts/check.sh`: emulator tests, the box smoke run, every device image in `platformio.ini`, the stack check and the RAM
  check. Run it before every commit.

## 7. Add an app in 8 steps

The example is `apps/examples/level/` (`level_app.h`, `level_app.cpp`), wired in the emulator by `emu/level_*.h` and into the box
image by the `WITH_EXAMPLES` blocks of `src/main_box.cpp`.

1. **Folder**: `firmware/src/apps/<group>/<name>/`, a header and a source. Include platform headers as `"platform/..."`. No
   `Arduino.h`, no `millis()`: time arrives as arguments.
2. **Class** `: public IApp`. Take the ports you need by reference (`IBattery&`, `ICompass&`, `BoxSettings&`, ...). Put big state in
   members, not on the stack. Return a name from `name()`.
3. **Lifecycle**: `begin()` resets state and starts sensors (`motion_.start()`); `end()` stops them.
4. **Frames**: change-driven (`wantsFrame` + `draw`) or game mode (`frameIntervalMs() = 20`, `step`, `draw`). Draw with `ui::titleBar`
   and `ui::hintLine`; never clear or present.
5. **Input**: handle the switch (`onInput(KEY_MAIN, BTN_SHORT / BTN_LONG)`) and the buttons (`onKey`) the same way; hint by
   `nav()->sixButtons()`. Do not use `BTN_HOLD`, the 3 s hold and A + B belong to the system sheet; leave through the sheet.
6. **Emulator**: add the `.cpp` to `emu/Makefile` (a `LEVEL :=` style variable in `LIBSRC_SAN`), write `emu/<name>_tests.h` from
   `level_rig.h`, add the row to `kTests`, add a render in `emu_main.cpp`. `build.sh` must pass; look at the PNG.
7. **Box image**, in `src/main_box.cpp` (the PC twin is `emu/box.h`): add an `APP_<NAME>` index at the end of the enum (the index is the
   order of `host.add`); a `make<Name>(mem, ctx)` that does `new (mem) T(ports...)` and `static LazyApp<T> nameApp(arena, "name", make...)`
   after `introApp`; `host.add(nameApp)`; `registry.add("Name", "sub", "one line", APP_NAME, ICON_GENERIC, false)`. If `sizeof(T)` is
   bigger than the arena, add it to the `maxSize(...)` chain of `kArenaBytes`. Add the app's folder to `build_src_filter` in
   `platformio.ini`. `pio run -e feather_sense_box_examples` builds exactly this for Level (`-DWITH_EXAMPLES`): .heap 71872 bytes vs
   71904 for the plain box: the app costs 32 bytes of static RAM (its LazyApp), the rest is code in flash. Run `scripts/ram_check.py` after every such change.
8. **Check and measure**: `scripts/check.sh`, then on the device read the `gm` line (fps, dropped, draw ms) for a game, and
   note measurements in `NOTES.md`. Settings-like data goes in a `StoreSlot`, not in the app.

Level is kept out of the shipped images on purpose (it is a template, and every registry tile is a choice the pilot sees). To try
it on the board: `scripts/flash.sh feather_sense_box_examples`, then `APP level` over USB or pick Level on the home screen.
