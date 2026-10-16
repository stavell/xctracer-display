# Findings, measurements and open items

What was learned while building this, so it does not have to be rediscovered. The README says what the
project is and how to run it; this file says what we found and why things are the way they are.

## Hardware facts (verified on the unit)

- Board: Feather nRF52840 Sense. I2C scan found all five sensors (0x1C LIS3MDL, 0x39 APDS9960, 0x44 SHT30,
  0x6A LSM6DS33, 0x77 BMP280). The sensors are unused by the vario firmware; the old sensor dashboard
  lives at the git tag `sensor-dashboard`.
- User switch is `PIN_BUTTON1` = Arduino pin 7 (P1.02), active low.
- Display wiring as built: SCK, MO (SI), CS on the RX pad (pin 1), EXTCOMIN on the MI pad (pin 24).
  Where DISP and EXTMODE are connected was never recorded. The display works, so presumably tied high.
- VCOM: a 1 Hz FreeRTOS timer toggles EXTCOMIN. The nRF PWM cannot go as low as 1 Hz (about 4 Hz minimum).
- The Adafruit nRF52 Arduino core runs `loop()` on a task with a **4 KB stack** (`LOOP_STACK_SZ`). Any
  large array must be static or a member of a global object (the view model and flight model are).
- `Adafruit_SharpMem` no longer defines BLACK/WHITE (0 = black, 1 = white).
- Flash use is about 215 KB of 815 KB, RAM about 34 KB of 249 KB.

## XC Tracer Mini III GPS (verified from a capture)

- BLE name from `bleName` in its config file (here `XC-Tracer`); service `0xFFE0`, characteristic `0xFFE1`,
  notifications, about 3 lines per second, `stringToSend=XCTRACER`:
  `$XCTRC,Y,M,D,h,m,s,cs,lat,lon,alt,speed,course,vario,,,,pressure,battery%*CS`.
  Checksum is the XOR of the characters between `$` and `*`.
- lat = lon = 0.000000 means no GPS fix (indoors). Speed and course are blank then.
- **Speed unit is assumed km/h and is not verified.** It needs a capture outdoors with a fix.
- The tracer accepts one BLE connection: disconnect phone apps first.

## Software findings

- **BLE callbacks must not do real work.** The first design parsed injected lines and ran the flight maths
  inside the Bluetooth callback. On the device the simulator worked (its data is made in the main loop) but
  replayed lines hung the box. The callbacks now only copy bytes into small queues; everything else runs in
  the main loop. After the change a full replay ran cleanly on the hardware.
- **Full-screen `fillRect` takes 142 ms on the nRF52840** (it falls back to one `drawPixel` per pixel).
  `IPanel::clear()` uses the library's buffer clear instead. This cut a thermal-page redraw from 255 ms to
  108 ms. Never fill the whole canvas with a rectangle.
- Redraw cost now (thermal page, measured on the device): SPI refresh 53 ms, drawing about 55 ms. At 3 Hz
  the CPU is about two thirds idle between redraws. The button is only read between ticks, so a press
  shorter than about 110 ms that falls inside a redraw could be missed.
- The flight clock comes from the data's own time stamps, not `millis()`. That is why a replay at any speed
  behaves like real flight time.
- Source arbitration: injected data (replay) and the built-in simulator silence the live tracer for 3 s.
  Without this, a live tracer with no fix and a replay alternate on screen.
- Killing a replay abruptly can leave the box holding a stale laptop connection for several seconds. Wait
  about 10 s before reconnecting, or the next connect may fail with "SharpDisplay not found".
- The saved tracer is stored in the nRF's flash filesystem. It was cleared whenever the stored struct
  changed (a few times during development). Reselect the tracer after such a firmware change.
- A floating-point NaN can reach a screen only through `fnum()`, which prints `--` for anything not finite.
  With no GPS fix, speed, track and wind read 0 and the speed cell is titled NO FIX; glide stays `--`.

- **Same-named devices.** Reconnecting by name as well as by address (added to survive private addresses)
  made the box connect to whichever `XC-Tracer` it heard first, which can be a different unit from the one
  chosen in the list. Matching is now by address only, with the name used as a fallback only when the saved
  address is a private (changing) one. The device list shows the last two address bytes (for example `D6:75`)
  so same-named units can be told apart. The serial status prints `dev saved=... conn=...` and the last raw
  line (`raw ...`), which shows exactly what the box is connected to and receiving.
- Observed (2026-10-15, 12:32 UTC): the saved tracer (F3:50:DF:21:D6:75, static random address) sent
  `lat = lon = 0.000000` (no fix) at 137.7 m with 46% battery while the user reported a second tracer
  outdoors with a fix. Whether that second unit exists, or the saved one simply had no fix, was not settled.

## Wind estimate

- Method: least-squares circle fit to the ground-velocity vectors of the last 30 s while turning (centre =
  wind, radius = airspeed), accepted only with at least 200 degrees of heading coverage, airspeed 14-72 km/h
  and a small residual, then smoothed over about 8 s.
- On the real flight (`2026-08-16-XCT-SVE-09.igc`, 3 h) it gave consistent results: airspeed fitted at
  32-38 km/h in nearly every thermal and 6-20 km/h wind toward about 250-290 degrees across 21 episodes.
- Typical fit residual on real data is about 7.5% of airspeed whatever the turning fraction, so a tighter
  residual threshold would reject good circles.
- A flaw found by test: windows that start or end in a straight glide (different airspeed) bent the estimate
  by about 1 km/h as a thermal was left. The fit now requires the first and last 5 seconds of the window to
  be turning. In the test it then stays within 0.1 km/h; on the real flight the wind is found about as often.
- The arrow convention: the compass-corner arrow shows where the wind blows to, relative to the heading;
  the text shows the direction it comes from (for example `14 km/h E` = wind from the east).

## Thermal assistant

- The map is wind-compensated: a point from `a` seconds ago is shifted by wind x `a`, so a thermal holds still
  while you circle in it. A unit test shows 0.0 m radius spread compensated versus 32 m for the ground track.
- A remembered thermal = the lift-weighted centre of the circling part of a climb (at least 15 m gained, run
  of at least 8 s at 0.5 m/s or more). Using every climbing point pulled the centre toward the straight
  climb on the way in; weighting by turning points fixed that. In a simulated drifting thermal the marker is
  within 4 m of the truth 40 s after leaving it.
- Memory: up to 6 thermals, 15 minutes, merged when re-detected within 50 m, drifting with the wind.

## Testing without the device

- `firmware/emu/build.sh` runs 107 checks with address and undefined-behaviour sanitizers and renders the
  screens. `emu replay` runs a whole flight through the real app and reports non-finite values, wind and
  circling coverage and the worst tick time. A scenario that mixes the live no-fix stream with a replay is
  included.
- The preview is only as good as the stubs: it uses the real Adafruit_GFX and fonts, but not the real SPI
  or timing. Timing was measured on the device through the serial status line (`tick=`, `refresh=`).

## Magnetic compass

- When the GPS gives no usable track (no fix, no data, or slower than about 4 km/h with 2.5 km/h hysteresis),
  the compass page uses the magnetometer. `HeadingSelector` (core) picks the source; the dial labels it
  `TRK` (GPS track) or `MAG`; with neither it stays north-up and shows `--`. The sensors are powered only
  when the compass page is showing and the GPS track is not in use.
- Bug found by the new tests: the first compass maths (written for the old sensor dashboard, never verified
  on the unit) did not normalise gravity, so the heading was distorted. Fixed and covered by tests (every
  heading, +-50 degrees pitch and roll, free fall rejected).
- Calibration is a least-squares sphere fit (hard iron only). Min/max was tried first and left 7.9 degrees of
  error from a coarse sweep; the sphere fit gives 0.0 on the same data. Stored in its own flash file so it
  never resets the saved tracer. Started from the device list ("Calibrate compass") or
  `python host/ctl.py cal start|save|cancel`.
- The magnetic heading is magnetic north, not true north (no declination model), so it differs from the GPS
  track by the local declination.
- **Not yet done on the unit: the axis mapping.** The sensors report in their own axes; `toDeviceAccel` and
  `toDeviceMag` in `src/platform/hw/compass_lis.cpp` are still identity. `host/orient_log.py` records a session and
  `host/orient_analyze.py` solves both mappings (self-tested on synthetic data) and prints the code to paste.
  First reading from the unit: accelerometer gravity along -z at rest in its current pose.

- Gyro fusion and board mounting (2026-10-16, emulator only, NOT yet tried on the device): the LSM6DS33 gyro
  runs at 104 Hz, +-500 dps, and is read in the same 14-byte burst as the accelerometer (the Adafruit
  `getEvent` already reads temp+gyro+accel, so the gyro adds no bus time, only supply current, to be
  measured). `platform/sensors/ahrs` is a Mahony filter on a quaternion: gravity corrects roll/pitch (tau 1 s, ignored
  when |a| is not within 12% of g), the horizontal field corrects the heading only, about the vertical axis
  (tau 2 s; a field spike cannot tilt the attitude; magnitude and dip gates reject disturbed fields), gyro
  bias from the PI integral and, faster, from a still-detector (two quiet 0.5 s windows: gyro std < 0.6 dps,
  gravity and field directions unchanged), bounded to 12 dps; seeded from accel+mag at start and after a
  gap > 0.5 s with gains x8 fading over 2 s. App: the fused heading is the MAG source, glide tau 30 ms with
  the same 0.4 deg deadband; no gyro, or a frozen one (80 identical readings), falls back to the old
  mag+accel path. Emulator numbers (80 Hz, gyro 0.1 dps rms + 1-2.7 dps bias, mag +-1 uT uniform noise,
  48 uT / 60 deg dip): still box max heading error 0.19 deg over 60 s (tilt 0.08 deg); with the
  magnetometer removed after learning, the heading drifts 0.1 deg in 60 s; 90 deg turn at 180 dps: max
  error 0.34-0.43 deg during the turn; strong field spike: 0.1 deg heading, 0.05 deg tilt; a 25 deg field
  twist for 3 s moves the heading but tilts nothing (0.06 deg). Mounting: the single hardcoded mapping
  became two: per-chip fixed `toBoardAccel/toBoardMag` in `platform/hw/compass_lis.cpp` (identity: no board docs in
  the library folders, so the real chip axes are unknown) and the board -> case rotation, an index 0..23
  (`platform/sensors/mount.cpp`, stored in `/mount.bin`, `ctl.py orient`), applied to accel, gyro and mag in the app.
  The hard-iron offset is stored in the device frame and is rotated along when the index changes.
  `orient_analyze.py` now prints the index (and the toBoardMag fix if the two chips disagree).
  To check on the device: the `cmp` line noise when still (`hdg`, `bias`), the turn response, supply current.

## Maths load (estimate, not yet measured on the device)

- The Cortex-M4F has a single-precision FPU; `double` is software (about 10-30x slower). `double` is used in
  the wind fit, the thermal finder and memory, the flight model's lat/lon arithmetic and the parser's `atof`.
  Everything drawn is `float`.
- Estimated data-path cost: about 10 ms per second of flight (wind fit about 5 ms only while turning, parsing
  about 1 ms, air trail plus thermal finder 1-3 ms), roughly 1% of the CPU, against about one third for
  drawing and the SPI refresh (measured). The maths is not the bottleneck.
- `lat`/`lon` must stay `double` (a float resolves only about 0.4 m at these coordinates).
- The serial status line can report `parse=`, `windfit=` and `viewmodel=` timings (only when a terminal is
  attached). They were added but not yet read on the device: the board was not on USB at the time.

## Fuzz, soak and clock-wrap testing (emulator)

- `firmware/emu/build.sh` now runs about 88,900 checks: a 60,000-line parser fuzz, an app-level fuzz with hostile
  lines and laptop commands, and the `millis()` wrap at 211 positions. `build.sh soak <lines> --runs 3` replays a
  whole flight repeatedly with link drops, page changes, presses, calibrations and the simulator.
- Real bugs it found, all fixed and now hard checks:
  1. **A corrupt position was accepted as a GPS fix** ("nan", "inf", 1e300, lat 95). It reached the trail and wind
     code, and on the thermal page a NaN angle was cast to int in `drawArrow` (undefined behaviour; it aborted
     the sanitizer build). A fix now needs a finite position on the globe; `drawArrow` and the map heading also
     refuse non-finite angles.
  2. Battery percentage and time fields were read with `atoi` (overflow, "XC 250%"); now strict 0..100 / ranges.
  3. GPS course was passed to the dial unnormalised (the parser allows -720..720): the dial could show "-400".
     Courses and magnetic headings are now normalised to 0..360.
  4. and 5. `0` was used as "never" for the inject, last-sample and laptop-draw time stamps, but `millis()` is 0
     once every 49.7 days: replaced by explicit flags.
- Soak on the real 3 h flight, 3 runs: 9.1 virtual hours, 0 heap allocations inside `App::tick()`, every
  buffer within its limit, 0 bad lines, worst tick 10 ms on the PC with sanitizers.

## Upload failure and recovery (2026-10-15)

- After the temperature-profile build, every full upload died about a third of the way ("Timed out waiting
  for acknowledgement" / "port not open") and the board stayed in the bootloader. **PlatformIO still printed
  SUCCESS**: only "Device programmed." in the output means the upload worked.
- Two suspects, not separated: (1) the 8 s watchdog survives the jump into the bootloader and resets it during
  a ~20 s transfer; (2) the bootloader's UF2 USB drive was wedged (listing and force-unmounting it hung), which
  starves the serial DFU. A 53 KB rescue image always uploaded (about 5 s); full images did not.
- What fixed it: unplug USB **and** the battery for 10 s, plug USB back without pressing RESET, upload at once.
- Recovery kit: a tiny rescue firmware (Serial only) uploads in a few seconds even when full uploads fail;
  `firmware.uf2` dragged onto `FTHR840BOOT` in Finder also bypasses serial DFU (Terminal may lack permission
  to write to removable volumes).
- The watchdog is disabled in `main_vario.cpp` until uploads with it running are proven (or it is stopped by a
  longer timeout / the app hands over cleanly before DFU).
- **Found later the same day:** it happened again with the watchdog off. A serial logger (`logger.py`, left
  running since the device soak at 17:44) held `/dev/cu.usbmodem*` and reopened it within 0.2 s after every
  reset, so it read the bootloader's DFU replies and the upload starved ("No data received", "Device not
  configured"). Short transfers sometimes got through. After killing it, one more upload failed (the board
  was still in the bootloader from the earlier failure) and the next one worked. So the logger is the
  confirmed cause; the watchdog is not proven guilty or innocent.
- Prevention: flash with `scripts/flash.sh`. It refuses while any process holds the port (and names it),
  retries up to 3 times, and only reports success on "Device programmed.". Never leave serial monitors or
  loggers running in the background.
- Partial refresh (only changed native lines are sent): on the compass page with a 3x replay,
  174-192 of 240 lines change per frame (in portrait a native line is a screen column, and the dial
  spans almost all of them), so the panel write drops from 53 ms to about 37-41 ms. A small gain.
- First on-device stack margin: `loopstack=2356` bytes free on the compass page (target >= 1024).

## Watchdog (wired in, then DISABLED: see above)

- 8 s nRF52 WDT, started right after the power-on gate, fed every loop iteration and inside the power-off
  waits. Once started it cannot be stopped and it survives soft resets; the Adafruit bootloader feeds a
  running WDT in its DFU wait loop (checked in its source), so USB uploads should keep working.
- The core resets on a hard fault (`NVIC_SystemReset`), but its stack-overflow and malloc-failed hooks
  only log and return in release builds: an overflow continues on a corrupted stack. Only stack margins
  and the watchdog protect against that, so the status line now reports `loopstack=` (smallest free
  stack of the loop task so far, bytes) and `rst=` (reset cause at boot: 0x2 watchdog, 0x4 soft reset
  including a hard fault, 0x10000 wake from System OFF, 0x1 reset pin, 0 power-on).
- To verify before relying on it: (1) two uploads in a row still work; (2) an injected hang (test build)
  resets the board within 8 s and `rst=0x2` is reported; (3) a powered-off box stays off for minutes
  (the WDT must not wake it); (4) the reset button recovers the board in every state.

## Power (not measured)

- The board has no current sensor and the nRF52840 has no current ADC. Battery voltage on A6 is read for the
  header percentage only.
- Options, in order of usefulness: a Nordic PPK2 (time-resolved current, sleep current), an inline USB
  power meter (relative comparisons), a multimeter in series with the battery, an INA219 on I2C (the box
  could report its own current; about 0.1 mA resolution, no use for sleep), a MAX17048 fuel gauge (better
  state of charge and a %/hour drain rate).
- Not yet done: USB-present detection, any measurement of 2 Hz versus 3 Hz, sleep or low-power modes
  (only System OFF exists).

## Open items

1. Verify the tracer's speed unit and behaviour with a real GPS fix, outdoors.
2. Auto-switch to the thermal page while circling (the Maxx does this; about 16 s back to the main page).
3. Partial (changed-line) display refresh to cut the 53 ms SPI transfer, if battery needs it.
4. Measure real current draw (PPK2 or similar) and tune refresh rate and BLE intervals against it.
5. Confirm `DISPLAY_ROTATION` (1 or 3) once the box is mounted; record where DISP and EXTMODE are wired.
6. The four front buttons are still unwired; only the user switch is used.
7. Glide ratio shows `--` when not sinking; decide whether another value would be better.
8. Check how close thermal markers look at wide zoom (labels are skipped when they would collide).

## Fast math removed every NaN check on the device (2026-10-15)

- The Adafruit nRF52 core compiles with `-Ofast`, which implies `-ffast-math` / `-ffinite-math-only`:
  GCC assumes NaN never occurs and folds `isfinite()` / `isnan()` to constants. Every NaN guard in the
  firmware (fnum's `--`, the arrow guard, the parser's fix checks from the fuzzing) was dead code on the
  device; the emulator (no `-Ofast`) never saw it. Found when the new heading smoother stayed at NaN.
- Fix: `build_unflags = -Ofast`, `-O2` in `platformio.ini`; `apps/vario/core/types.h` refuses to compile when
  `__FINITE_MATH_ONLY__` is set, so it cannot come back silently.
- Magnetometer is now always on (all pages), so it has a settled heading when the GPS track drops out.
- Panel SPI: 2 MHz -> 4 MHz (full refresh 53 -> 27 ms) -> 8 MHz (about 14 ms full, 2-9 ms partial).
  8 MHz is beyond Adafruit's default; recheck for artefacts when cold. Compass-page tick with the
  dial animating: 60-80 ms (was 108 ms).

## Drawing speed (2026-10-15, diagnostics build profiler)

- Adafruit_GFX's generic path cost 1.46 us per pixel; a thermal frame with a full trail took 103 ms to
  draw (trail 41, cells 28, header 16). `platform/gfx` Bitmap1 + `platform/hw/fast_sharp.h` write straight into the frame
  buffer (bit-identical, tested against a copy of the library's mapping): the same frame now takes
  22 ms (trail 5, cells 9, header 2); fillRect 100x20 went from 4.6 ms to 0.08 ms.
- Single pixels must not go through the rect code: that first version made pixels 3x slower (4.7 us).
  The inline `Bitmap1::pixel` brings GFX's per-pixel call to about 1.2 us, most of it GFX's virtual
  dispatch. Text is the largest remaining cost (2.8 ms for "1234" in FreeSansBold24pt).
- Animation at 20 fps (50 ms) while the heading glides: worst loop tick 49-60 ms on the thermal page with
  a full trail, so it runs at roughly 17-20 fps. While circling the map never settles, so the CPU runs
  flat out the whole time (power cost not measured yet).
- Fast text (2026-10-16, `platform/gfx/atlas` + `font`, glyphs pre-rotated into native byte rows): "1234" in
  FreeSansBold24pt went from 2783 us to 332 us (8.4x); the four thermal cells from about 9 ms to 2-3 ms.
  The four fonts take 22.3 KB of RAM and 97 ms to build once, on the first frame; free heap after that
  is 62.8 KB. The thermal frame (live) now draws in 5-12 ms; the thermal memory block (up to 7 ms once
  marks exist) and the present (2-10 ms SPI) are now the largest parts. The profiler's clock ticks in
  about 1 ms steps, so small sections read 0 or 977 us.
- Known, not fixed: the wind profile page divides by zero in landscape (`areaH / rows`); the box only
  runs in portrait.
- Background panel send (2026-10-16, EasyDMA on SPIM2, packed copy of the changed lines as the second
  buffer): present() now costs 2.2-2.9 ms of CPU (change detection + packing + snapshot) instead of
  2-10 ms of blocking SPI; the transfer itself (3-8 ms) overlaps the next frame, wait=0 and no timeouts
  in a 45 s thermal replay. Thermal page loop tick 15-30 ms (windfit is ~6 ms of it). +12.7 KB RAM.
- The profiler now uses the DWT cycle counter; micros() on this core only ticks in ~977 us steps, so
  every earlier sub-millisecond figure in these notes is rounded to that grid.
- Next hot spot: the live (compass) page without a fix draws in 48 ms (tick 55-73 ms), far above every
  other page.
- Cause of the 48 ms (found 2026-10-16 with the emulator work counters, `firmware/emu/build.sh count`): in portrait a
  horizontal grey span is a native column, so the grey ring (about 5900 pixels) cost one native row
  read-modify-write each plus a dither pattern of 8 Bayer evaluations each (47k per frame). Fix: pattern
  cache per (rotation, w&7, h&7, level), table Bayer, and platform/gfx/shapes `fillRing` that emits vertical
  runs (native rows) in rotations 1/3 (344 rows). Circles and ticks use `circle`/`line` (same pixels as
  Adafruit_GFX, no virtual call). Rendered screens are byte-identical in all rotations. Device time not
  yet measured; sections ring/bezel/ticks/labels/hdgnum added to the profiler line.
- Compass page (2026-10-16, `platform/gfx/shapes`: fillRing/fillDisc as native-row runs, cached Bayer row
  patterns, line/circle on the bitmap): live page without a fix 48 ms -> 7.6 ms on the device
  (ring 2.2, bezel 1.2, ticks 0.5, labels 0.6, heading number 0.3 ms); loop tick 18 ms. The ring was
  5918 read-modify-writes of a native row per frame (a screen row is a native column in portrait), each
  recomputing an 8-pixel dither pattern. All screens are pixel-identical to before.
- Seen while measuring: `windfit=` costs about 5.7 ms per tick even with no data and no fix.
- Instant compass (2026-10-16, not yet measured on the device): LIS3MDL 10 -> 80 Hz (medium performance:
  datasheet limits UHP to 10 Hz, HP to 40 Hz, MP to 80 Hz; same mode as before, so per-sample noise is
  unchanged and the filter averages more), LSM6DS33 accelerometer 26 -> 104 Hz. The mag is read only when
  its STATUS ZYXDA bit is set (one-byte I2C read; the Adafruit library has no data-ready call). Compass
  low-pass is time-based (accel tau 60 ms, mag tau 50 ms; was 0.5/0.4 per 100 ms sample, tau about 200 ms).
  HeadingSmoother tau 90 ms for MAG, 250 ms for the GPS track (App sets it when the source changes), with
  a 0.4 deg deadband / 300 ms drift hold for MAG so noise does not keep the glide running. Compass page
  redraws every 33 ms while gliding. Emulator step test (90 deg turn, whole chain): 90% after about
  250 ms at 80 Hz (252 ms), 200 ms at 10 Hz; still input with +-0.3 deg noise stays settled.
  To check on the device: `compass=` time in the status line (one 80 Hz read per loop tick) and the
  dial jitter with the box lying still.
  Measured on the device: compass= 46 us; but lying still on a desk (next to a laptop) the shown heading
  wandered 260-270 deg and ~180 lines changed every frame, so the 30 fps redraw never stopped. 50 ms of
  filtering cannot hide the magnetometer noise; gyro fusion is the fix (in progress).
- Correction to the line above (2026-10-16): `windfit=` is not a per-tick cost of the flight model. It is
  a synthetic benchmark in `apps/vario/hw/diagnostics.cpp` (one `fitWind` over a canned circle) that was re-run on
  every status line, so it showed up as ~5.7 ms in the loop tick of that moment even with no data. It now
  runs once. The real fit runs once per data second, only with a fix, and rejects straight windows before
  any trigonometry (emulator, 900 s real flight: 0.47 -> 0.37 us per call, identical outputs; the device
  figure is not measured). Same change: EKF wind has a real age (seconds since an update while turning),
  the EKF is seeded from the first circle fit when not yet confident, and home is the position/altitude
  where speed first passed 10 km/h instead of where takeoff was detected 5 s later.
- Gyro fusion on the device (2026-10-16, Mahony, I2C now 400 kHz): lying still the heading held
  110.8 deg over three status lines (was wandering 260-270 deg without the gyro). The LSM6DS33 reads
  about (5.3, -7.8, -3.9) dps at rest (zero-rate level, datasheet allows +-10 dps per axis); the learned
  bias matched it (|b| 10.1 dps) within seconds. The bias bound is 12 dps, close to that: raise it if
  another board reads more. compass= 52 us, ahrs= 55 us per tick.
- Fused dial smoothness (2026-10-16, emulator only, not yet on the device). Symptom on the desk: "drifts, UI lags,
  brief moments smooth then lagging". Cause: the HeadingSmoother deadband (0.4 deg, 300 ms hold, snap within 0.8
  deg) turned slow motion and sub-degree wander into rest-and-jump steps, and the settled() redraw cadence dropped
  between jumps. Also KP_MAG 0.5 /s passed magnetometer noise through as +-0.8 deg wander. Changes: fused source has
  no deadband and snaps at 0.01 deg (was 0.5), tau 30 ms; redraw rule for the fused source is "shown heading differs
  from the last drawn one by more than half a pixel at the dial ring (0.28 deg)" instead of settled(); GPS and
  compass-alone paths unchanged. AHRS: KP_MAG 0.1 /s (tau 10 s), KI_MAG 0.0025 (kp^2/4); field boost 40x (was 8x,
  shared with gravity) and restarted for each of the first three bias estimates; the field correction is weighted
  by a graceful trust (1 up to 5% magnitude/dip deviation, 0 at 20%). Emulator, still box with +-1 uT mag noise and
  a 2.7 dps gyro bias: fused heading peak-to-peak 0.12 deg over 40 s after 30 s of convergence; leftover error
  after start is a few tenths of a degree that decays with tau 10 s (bias learner accuracy / KP). Start: error
  0.69 deg max in 1..2.1 s. Slow turn at 2 deg/s: largest step 0.07 deg per 33 ms frame; still: 29 presents in
  10 s; turn: 67 in 10 s. Recovery from a 25 deg field twist now takes tens of seconds (tau 10 s). Trade-off:
  with an unlearned gyro bias in motion (never still since power-up) the heading sits bias/kp off until the
  integral catches up (about 20 s).
- Fused compass on the device, 2026-10-16. Lying still after the KP_MAG 0.1 change, the heading spun at
  about 5 deg/s. The new `ahrs` status line showed why: the calibrated field, seen through the attitude,
  was almost vertical (dip -0.999, horizontal 2-5 uT, below MIN_HORIZ_UT 8). So the magnetometer was
  rejected (trust 0), and every rejected sample also marked the still-detection window bad, so the gyro
  bias was never learned. Fix: field rejection no longer blocks still detection. Bias is now learned in
  about 3 s (10.1 dps) and the heading holds flat on the gyro alone. Open: why the horizontal field is
  near zero. The raw readings give a plausible dip (about 60 deg), so the stored hard-iron calibration is
  the first suspect; recalibrate away from the laptop. If the field is still vertical after that, the
  mag and accel chip axes disagree (run orient_log.py / orient_analyze.py).

## Continuous thermal map (2026-10-16, emulator only, not yet on the device)

The thermal map used to move only when a sample arrived (about 3 Hz), so trail and rings jumped several
pixels. Now `apps/vario/core/map_motion.*` interpolates the view between samples; `App::updateMap` / `buildViewModel`
feed the result to the view (`ViewModel::mapLive/mapCx/mapCy/mapHdg/mapScale`). Design:

- Display time = now minus (sample interval + 50 ms): 383 ms live, 161 ms in a 3x replay (the interval is an
  EMA of the arrival gaps). The centre is interpolated linearly between the two samples around it: never a
  position that was not measured, never past the newest one; with no new data it holds there. After a pause
  below 2 s the display time catches up at 2x speed (no jump); a gap above 2 s, a jump above 150 m or a
  lost fix snaps. Arrival jitter of +-25 ms changes the frame step by at most 1.3x the mean (test).
- Air-mass drift between samples: the data seconds are only 1 s resolution, so the drift uses the measured
  ratio of data time to wall time (1.00 live, 3.00 at `igc_replay --speed 3`; test).
- Trail: points newer than the display time are dropped and the centre is appended, so the line ends
  under the arrow. The view applies the centre offset itself (no copy of the trail).
- Heading: the map never used `HeadingSmoother` (it turned by the trail's last 3 s chord, in jumps of a
  sample). Now the same chord on the interpolated path (centre now vs. centre 3 data seconds earlier), so it
  is continuous and agrees with the translation by construction; below 3 m it uses the GPS course
  interpolated on the shortest arc. Largest step 0.06 deg per 33 ms frame through north (test). The
  compass page and the smoother are untouched.
- Zoom: the fixed steps (2.2 ... 0.11 px/m) stay, the shown scale glides to the step with tau 300 ms;
  snaps at page entry. Accelerating from 5 to 40 m/s: 373 changing frames, largest frame change 3.7 %.
- Cadence: on the thermal page 33 ms while the view differs from the drawn frame by more than half a pixel
  (centre in pixels at the current scale, rotation at 104 px, scale still gliding), else the usual 333 ms.
  The map shows about 30 s of track whatever the speed, so straight flight moves it only 3-5 px/s: 5.6
  draws/s at 10 m/s with a settled zoom. Circling (18 deg/s) asks for 33 ms: 25 draws/s with the 10 ms
  test tick (a tick every 40 ms). Data frozen: 2.9 draws/s.
- The robust-test storm detector (draws closer than 100 ms) now skips the thermal page.
- Static emulator renders (no ViewModel::mapLive) are byte-identical to before.

## Tetris and the PC player (2026-10-16, emulator only)

- Timers in the rules are absolute deadlines (gravity, lock, DAS, soft drop); `tick(now)` runs the ones that
  passed in time order, so ticking every 1 ms or once at the end gives the same game (test). Pause shifts
  all deadlines by the pause length.
- Hold needs a key and the switch is the only one on the current hardware: MAIN pauses on a short press (acted
  on at release) and holds on a long press (400 ms). A pause therefore lags the key by the press length.
- `Bitmap1::rectIncl` FILLS the rectangle; outlines need four spans (first title screen came out black).
- 9 pt FreeSansBold "HOLD" / "LEVEL" are 48-50 px wide: too wide for the 48 px side column, the labels use the
  regular 9 pt. Six-digit scores in 12 pt bold are 84 px, so score and best sit in a strip under the well.
- Piece looks: I solid with highlight, O hollow, T / J / L grey 8 / 12 / 4 of 16, S diagonal stripes, Z
  horizontal stripes; the 2 px dark bottom-right edge is the bevel. Distinct hashes are a test.
- SDL: define `SDL_MAIN_HANDLED` (no SDL2main on the link line from `sdl2-config --libs`). The player converts
  the native buffer to RGB through `Bitmap1::getPixel`, so the rotation mapping is the device's.
