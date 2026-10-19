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

## Clock sleep: power off that keeps the time (2026-10-19, emulator and builds only, NOT flashed, NOT measured)

Design. The box image's power-off (the 3 s hold) is two steps (the Settings "Power off" row and its slot are gone; the slot stays reserved as
`SLOT_POWER_MODE_UNUSED`): it no longer enters System OFF at once, but starts the clock sleep; holding on 3 s more
(`ClockSleep::kOffHoldMs`, counted from the start of the sleep, i.e. after the ~1 s radio stop; a 12-step bar under the clock, `clockview::countdown`) goes to System OFF.
The hold that powered off is released first before a wake hold counts, so the continued hold never wakes. Release early: the clock is redrawn without the bar.
The clock sleep: `PowerNrf::shutdown()` stops the radios as before (disconnect, 150 ms, stop advertising / scanning, 800 ms), then
`clockSleep()` switches the compass off, puts the QSPI flash into deep power-down (not while a map load runs; any later flash
operation wakes it by itself), draws the clock and sleeps in System ON. Why System ON: a reset loses the WallClock (it only
ever comes from the tracer's GPS time), and a wake without reset lets the start-up app restart tracer and sensors through
its normal begin() (`App::end()` pauses the tracer, `begin()` resumes it). Hence `shutdown()` RETURNS in this mode and the
loop restarts the start-up app, with the intro (`PowerNrf::takeResumed()` -> `startStartupApp(true)`; Settings > Intro Off disables it).
- Logic (pure, `platform/power/clock_sleep.h`, `ClockSleep::step()`): draw / wait until / wake / System OFF. Draws on the
  first step, when the local minute changes (the wait is to the next minute boundary + 50 ms, `WallClock::msToNextMinute`)
  and after a tap; with unknown time it only looks once a minute. A press must last 1 s to wake (the same anti-pocket rule as
  `gateOnWake`); the hold that powered off must be released first. Battery at or below 2 % on two readings 2 s apart ->
  System OFF (the battery is read about once a minute and on a tap; the percent table puts 2 % near 3.4 V).
- Screen (`platform/power/clock_view.h`): seven-segment digits drawn with `roundFill`, no font, so no RAM; `--:--` when the
  time is unknown or older than 12 h (the staleness rule of `clock.h`, unchanged); battery glyph + percentage below.
- Device wait (`power_nrf.cpp`): `ulTaskNotifyTake` with a timeout on the loop task, woken by a GPIOTE interrupt on the switch
  (`attachInterrupt`, PORT-event based, no hi-accuracy). The CPU sleeps in FreeRTOS tickless idle (the core's RTC1 keeps
  `millis()`), so the only regular wake-ups are the panel's 1 Hz VCOM timer, the watchdog feed (every 4 s: the 8 s WDT runs
  in sleep) and the minute redraw. While the switch is down it polls every 20 ms (at most 1 s).
- Vario-only image: `enableClockSleep()` is never called, `shutdown()` is System OFF exactly as before.
- Emulator: `Box::tick()` turns `MockPower::requests` into the same state machine (`Box::sleep`); the PC player shows it
  (hold `off`, then hold the switch 1 s, or press R to restart).
Sleep current: NOT MEASURED. Estimate from the datasheets: nRF52840 System ON idle with RTC running and RAM retained about
3-5 uA (the SoftDevice and its 32 kHz clock add a little; tickless idle wakes 1x per second for VCOM and every 4 s for the
watchdog, each about tens of us), QSPI flash in deep power-down about 1 uA, the Sharp LS027B7DH01 about 50-175 uW static
(roughly 15-50 uA at 3.7 V) plus the 1 Hz VCOM toggle, LIS3MDL/LSM6DS3/SHT30 and the LiPo divider (the VBAT divider is a
constant load of 2 x 100k?, about 18 uA at 3.7 V if it is that: check the Feather schematic) and the regulator's quiet
current (about 1-10 uA). Expect roughly 50-250 uA, i.e. one to several weeks on a 1000 mAh cell; System OFF is a few uA plus the
same panel-less leakage. To check on the device with a PPK2: sleep current with the panel, without the QSPI power-down
(`powerDown()` is untested in the field: NOTES "Deep power-down" above), and that tickless idle really is entered (a
current of about 1 mA would mean it is not).
- Weather station (the sleep screen, user: "the sleep mode as a clock / weather station"). Layout, top to bottom: battery (small),
  the time, temperature (integer C) and humidity (%) in big digits, the pressure in hPa with a 3 h tendency arrow (up / sideways
  / down, doubled when fast) and a forecast pictogram, a 24 h pressure chart (scale: its hi / lo hPa; ticks at -6/-12/-18/-24 h;
  dotted middle line; open dot = now). No fonts, no words (`platform/power/clock_view.h`, seven-segment glyphs and shapes). Unknown
  values read `--` (never the last good value: a missing sensor blanks), an empty chart with no data. Data (`platform/power/weather.h`,
  pure and tested): `WeatherLog`, 144 x int16 hPa*10 (288 B of RAM, lost at reset / System OFF), one sample per 10 min, taken by the
  loop while awake (`weather.sample()` in main_box.cpp, so the history exists when the sleep starts) and at every minute redraw
  of the sleep. Temperature and humidity: one SHT30 single shot (`IThermo::readNow`, ~15 ms) per redraw. Pressure: the board's
  BMP280 (`platform/hw/baro_bmp.cpp`, no library: forced mode, x4 pressure, ~10 ms, then it sleeps; compensation in
  `platform/sensors/bmp280.h`, tested with the datasheet example; I2C 0x77, then 0x76; NOT tested on the hardware). The countdown
  frames of the power-off hold do not read the sensors (only the first one does).
  Forecast rule (a rough barometer forecast, NOT a weather service): from the change d over the last 3 h (needs 3 h of history):
  d >= +1.0 hPa sun; |d| < 1.0 sun + cloud; -3.5 < d <= -1.0 cloud; -6.0 < d <= -3.5 rain; d <= -6.0 storm. Arrow: |d| <= 1.0
  steady, <= 3.5 slow, above that fast. The classic Zambretti rule also uses the sea-level pressure; the box measures station
  pressure and does not know its height, so the absolute level is deliberately not used.
  Added sleep current, estimated from datasheets, NOT measured: once per minute SHT30 single shot (about 15 ms at 0.6-1.5 mA
  typ. 0.8 mA = 12 uC) + BMP280 forced read on 1 minute in 10 (about 11 ms at 0.7 mA = 8 uC, 1 uC per minute averaged) + I2C
  transfers and a few ms of CPU on top of the redraw that already happens each minute (delay() yields to idle: about 5 ms
  at 4 mA = 20 uC) = about 35 uC per minute = 0.6 uA average; the sensors' idle currents (SHT30 about 0.2 uA, BMP280 sleep
  0.1 uA) add 0.3 uA. About 1 uA in all, under 1 % of the estimated 50-250 uA sleep current. Check with the PPK2 together with
  the rest.
Open risks: GPIOTE wake and tickless idle under Bluefruit are untested on the hardware; if the loop task misses an
edge, the 20 ms poll after the next wake still catches a hold; `radios_.resume()` restarts only the laptop advertising.

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

## Box: platform frame loop, launcher, stack check (2026-10-16, emulator and builds only, not flashed)

- **The vario did not change.** `App::tick` no longer draws; `AppHost::tick` polls the switch, ticks the app, then asks
  `wantsFrame` and does clear / draw / present. Proof (before vs after, same 600 s of 2026-08-16-XCT-SVE-09.igc from
  4185, `--hz 3` and `--hz 1`, compass, thermal, profile and home pages, `--inject`, `--mix`, rotation 3, two
  scripted-press runs through list and calibration and a 3 s power-off): every stdout line, every saved frame and the
  behaviour digest (hash of each present() time and frame buffer) are identical, and all 31 emulator screens are
  byte-identical. The test output (draw counts, cadence numbers) is identical line for line. One subtlety kept on
  purpose: the old tick never drew on the tick in which the list or calibration screen opened or closed (the
  branch had already been chosen); `wantsFrame` reproduces it with `tickMode_` (a screen change inside a tick draws on
  the next one). Without it frames come 10 ms earlier and the digest differs.
- **Sizes** (device builds, bytes). Vario only, before: flash 234108, RAM 65552. After: flash 236960, RAM 67340 (+2.8 KB
  flash for the host, edge queue and `popEdge`; RAM includes +1.5 KB, the static `fitWind` buffers). Diagnostics image
  and box image: see README. Box: flash 272432, RAM 87656; the second `App` (compass tool) is 19136 bytes of that,
  the three launcher / Tetris font sets are built on the heap on first draw. The font bitmaps of FreeSansBold24 are
  linked twice (vario view and Tetris view each `#include` the font header): about 8 KB flash, left alone.
- **Stack.** `scripts/stack_check.py`: the first run found the 1864-byte frame of `fitWind` (three `double[64]` on
  the stack) on the worst chain `loop -> VarioApp::tick -> App::tick (440) -> ... -> FlightModel::updateWind ->
  fitWind -> sin -> __kernel_rem_pio2 (648)`: 3516 of 4096 bytes, 86 %, in the shipped vario. The arrays are static now
  (same maths, byte-identical replay): worst chain 2604 bytes (63 %), now going through a flash write
  (`TracerBle::listConnect -> writeAtomic -> littlefs -> assert -> printf`). `sin` can reach `__kernel_rem_pio2`
  only for arguments above about 1.6 million radians, so the real figure is lower. What the checker cannot see:
  function pointers and library virtuals, recursion, the BLE callback task (3 KB) and the interrupt stack. Frames are
  read from the prologues (`push` lists, `sub sp`), calls from `bl`, virtual calls by the vtable slot loaded before the
  `blx` and the interfaces named in the calling class (over-estimate). GCC 7.2 has no `-fcallgraph-info`.
- **Device compiler is C++11 for aggregates**: a struct with default member initializers is not an aggregate there
  (`KeyEdge{a, b, c}` failed to compile); the emulator (C++17) accepts it. Keep ports' small structs plain.
- **Hold gesture in games.** Tetris acts on the switch's press edge (start) and its 0.4 s long press (hold piece), so a
  3 s hold to leave also swaps the piece once and, on the title screen, starts a game first. Harmless, but a
  dedicated "Exit" in Tetris's pause screen would be cleaner when the D-pad exists (A + B then replaces it).
- **Box smoke run** (`build.sh smoke`): scripted keys through the real `ButtonFsm` in virtual time, 10 ms host ticks;
  it fails if the active app at any screenshot is not the expected one.

## Terrain and map pack, step 1 (2026-10-16, pure code + host tool)

- `host/terrain/mkmappack.py` writes ONE file (format in `host/terrain/FORMAT.md`): header with CRCs, 200 m mean DEM,
  500 m MAX layer, towns (Latin + Cyrillic UTF-8, population class), main roads, attribution strings. Bulgaria + 10 km:
  **1,724,652 B** (mean 1,453,672, max 227,260, towns 8,546, roads 34,716, strings 266, header 192); 372 kB spare of 2 MiB.
  Against the study's 1,659,004 B the extra 65 kB is the per-tile CRC32 table (22 kB), vectors (44 kB).
- `firmware/src/platform/terrain/` (pure C++, device image links it but nothing calls it yet): `IBlockReader` port,
  `Terrain` (open, elevation, ceiling, relief), `MapVectors` (towns, roads, attribution), tile codec (CLZ unary reader).
  Static RAM: `Terrain` 22 kB (9 tiles of 2 kB + 2 kB flash buffer + 0.8 kB relief rows), `RoadIter` 2.7 kB,
  `TownIter` 0.3 kB. None of it on the stack.
- Integrity decision: open checks the header CRC and all sections up to 64 kB (the vector layers); DEM tiles are checked lazily,
  each against its own CRC32, on every read from flash (CRC ~12 cycles/byte x 420 B = 5k cycles vs ~60k to decode) so a
  flipped bit gives "no data" instead of a wrong height; `verifyAll()` does the whole-file CRC after a transfer.
  Checking 1.7 MB at every boot would cost 0.3 s of CRC plus the flash reads for no gain over per-tile checks.
- Bilinear agreement with the Python reference: worst 0.0002 m on 340 points (the study's float indexing gave 0.0095):
  the cell index is an int64 floor-division of the e7 coordinate differences, only the fraction is a float.
- `ceiling()` = max of the MAX-layer cells within 0.15 cell of the point (up to 2x2), never below `elevation()`.
- PC decode (Apple silicon, -O2): **4.4 us per 32x32 tile with CLZ, 7.6 us bit by bit** (420 coded B/tile); the study's
  reference decoder was 8.9 us. Cortex-M4 estimate ~0.55 ms/tile with CLZ (study: 1 ms for the bit reader), not measured.
- Relief window 240x400 (96k px) on the PC: 2.5-3.8 ms of maths; tile decodes with the 9-tile cache: 56 (100 m/px), 67 (turned
  45-60 deg), 208-256 (200 m/px, ~100 distinct tiles). A rotated row crosses more tiles than the cache holds, so the window
  is rendered in vertical strips about one tile wide, top to bottom (each tile is decoded about 2x, not once per row). On
  the M4 that is ~0.1-0.25 s per full redraw: the map page should redraw on position change, not at frame rate.
- Hillshade/contours: one height per pixel from the bilinear surface (no cell facets), shade from central differences one
  pixel apart in SCREEN axes (light stays upper left while the window turns), contour pixel = the pixel nearer to a level
  crossing between it and its 4 neighbours (1 px thin, independent of band/strip splits). Auto interval = smallest of
  20/50/100/200/500/1000 m >= 2 x m/px (100 m/px -> 200 m; 100 m gave a black mass over the Stara Planina at 100 m/px).
  The "light" style (flat = white, darkest level 10, 1.5x exaggeration, knee 0.15) keeps black contours readable; the first
  try (flat 15, darkest 9) dotted all flat ground in a regular grid, so flat ground is pure white now.
- Renders: `host/terrain/img/map_pack_sopot_100mpp.png` (fixture), `map_pack_rila_100mpp.png` (real pack).

## Terrain on the vario: AGL and the map page (2026-10-16, emulator only, not flashed)

- **AGL**: `ViewModel::agl` = tracer GNSS altitude - `Terrain::elevation()` at the fix; `clearance` = altitude - `ceiling()`. NaN (`--`)
  without a fresh fix, without a successful lookup, outside the pack. Shown bottom right of the live pages (label `AGL` above the
  number, in the wind row; a stale wind says `old` in place of `km/h` when terrain is on). Tried first in the ALT title band: too
  narrow (needs 84 px next to `ALT`). Without a pack the live pages are byte-identical to before (all 24 older renders compared).
- **Map page** (`apps/vario/view/terrain_map.*`, `screens.cpp` drawMapPage): north-up (heading-up is `AppConfig::mapHeadingUp`),
  fixed 100 m/px (`mapMPerPx`), full frame right of the thin vario (218 x 376). North-up because the relief cache then re-renders only
  after ~10 px of movement (~1 km: every minute or two in a glide, hardly ever in a thermal); heading-up re-renders per 5 degrees
  (every second while circling) and the labels jump. The relief, roads and labels are rendered into a 1-bit cache (10 kB); each frame
  copies it (row memcpy, the cache has the panel's native layout) and draws track, arrow, N, wind, AGL box and scale bar over it, so
  the arrow wanders up to 10 px from home between renders. The renders happen in `App::draw()`.
- **RAM** (a `TerrainKit`, owned by main/the player, 47.7 kB): Terrain 21.7 kB (tile cache 18 + buffers), TerrainMap 25.6 kB (cache
  10.2, 24-row bands 10.5, road/town iterators, track 1.9). Nothing in images without a pack.
- **PC timing** (-O1 with sanitizers in the tests, real pack, 218 x 376, roads + towns + labels): about 7 ms per render; `terrain_demo bench` at
  -O2: relief 240 x 400 = 2.6 ms. Device estimate (64 MHz M4F, no cache): 0.2-0.4 s per render, to be measured when the QSPI reader exists.
- Tests: `emu/map_page_tests.h` (AGL by hand on the fixture, no fix / stale / outside / no pack, page cycle, render determinism,
  cache re-render rules, projection, track ring). Screens: `emu/map_page_shots.h`.

## Launcher refactor: home screen, Settings, shared box settings (2026-10-16, emulator and builds only, not flashed)

- **Flight pages unchanged.** Same 400 lines of 2026-08-16-XCT-SVE-09.igc from 4185, `--hz 3`, pages 0-4, a frame every
  20 s: all 7 saved frames and the four behaviour digests are identical before and after (compass 5cf2bdda51c4ab9e,
  thermal b301c577141014e4, profile 3dec62d6e433bfc0, home c61a45228ffd5619). Of the 43 emulator screens 34 are
  byte-identical (all flight, map, standalone, status, profile, home pages); the six that differ are the menus and the
  home screen by design (launcher x3, list, list_tool, tetris_pause). `box_vario_matches_alone` still passes.
- **Storage.** The platform key store grew from 8 to 16 slots (`/box.cfg`, magic BOX2; a BOX1 file is read and rewritten,
  so the boot app and high score survive). New slots: mounting, time zone, five for the calibration (float bits), a
  "migrated" flag. Related sets are one flash write (`beginBatch` / `endBatch`, nesting): a calibration save is one write.
  `ConfigStore::migrateTo` imports `/magcal3.bin`, `/mount.bin` and the tz field of `/vario.cfg` once, at the first start;
  the old files are left alone. `Saved` keeps its tz field so `/vario.cfg` is not reset. The vario-only image now links the
  key store too.
- **A long press that opens another app must not be delivered to it.** Tetris first fired "Home" from the pause screen at
  the 400 ms threshold; the switch was still down, so its release reached the launcher as a long press and opened the first
  tile. It now acts on the release (the host delivers that event to the app that was active).
- **Compass calibration in the vario-only image.** No on-device row (the vario menu holds flying things); `ctl.py cal`
  still works, through the same `CompassCal` and screen as the Settings app. The vario feeds the calibration with the
  readings it already takes, so the sensor is never read by two owners.
- **Sizes** (device builds): vario only flash 266156 / RAM 68540, diagnostics 279744 / 82972, box 299720 / 90088
  (before the refactor the box was about 272 kB; Settings, the icon and UI kit and the key-store change are the difference
  together with the map page merged earlier). Stack: loop 2604 B (63 %), setup 2160 B (52 %), unchanged chain.
- **Home screen design notes.** Tiles 108 x 136 (rows of two, fewer rows get taller tiles; three rows drop the second
  text line); the chosen tile is a filled rounded box with inverted icon and text. The time is the tracer's GPS time of day
  extrapolated with the millisecond counter, `--:--` until the vario saw a fix and after 12 h without one.

## Flight simulator, step 1 (2026-10-16, emulator and builds only, not flashed)

- **What**: `apps/games/sim/` (glider, air, world, height cache, top-down and voxel views, HUD, tilt mapping, `SimApp : IApp`), the
  platform's game mode (`AppHost`, `IApp::frameIntervalMs/step`, `IPanel::ready`, `AppHost::stats`), `build.sh play sim`,
  `build.sh simbench`, tests `emu/sim_tests.h` (about 570 checks), renders `emu/sim_shots.h` (`out/sim_*.png`, from the Sopot
  fixture; `sim_demo PACK OUTDIR` renders the same from the real pack).
- **The vario did not change.** `frameIntervalMs()` defaults to 0 and the host's game branch is only taken when it is > 0. Proof: all 48
  emulator screens that existed before are byte-identical to a build of the previous commit (including the vario pages, map,
  profile, home and the box's launcher), `box_vario_matches_alone` and the behaviour digests in the tests pass, and the vario-only
  device image built. Tetris stays change-driven on purpose: its picture changes only on a key, a gravity step or a clear
  animation (a few frames a second), so a 50 fps clock would only burn power and DMA time for identical pictures; opting in is
  one override (`frameIntervalMs() { return 20; }`) when it gets animations.
- **Game mode behaviour** (tests `game_mode`): 500 +-2 steps in 10 s whatever the host tick period (1, 2, 5, 10, 13 ms), a host that is
  ticked 15 ms late still runs 50 steps per second (catch-up), a panel that is busy 50 % of the time delays frames but loses no
  sim time, a 500 ms stall catches up 4 steps and counts the rest as dropped, a plain app sees no steps and the 10 ms loop.
- **Height cache** (`HeightCache`): 128 x 128 cells of 200 m, int16 height + uint8 shade = 48 kB, toroidal; the valid area is a
  rectangle that grows by whole rows/columns inside the wanted window, at most 48 cells per physics step (a 200 m cell is
  crossed every ~15 s at 13 m/s, a row is 128 cells). Fill from the map pack on the PC: 16384 cells in 0.7 ms (tile cache warm
  and PC fast); on the device 16 tile decodes (~14 us x 35-80 each) plus 16k bilinear lookups: a start-up pause of
  roughly 0.1-0.3 s (not measured).
- **3D renderer** (`voxel.cpp`): 240 x 400, 70 degrees horizontal, one ray per pair of screen columns (120 rays), 48 steps per ray
  at most (24 m + 7 % of the distance, to 9 km), bilinear height and shade for the first 24 steps (1.2 km), nearest beyond;
  fog fades to a grey (12/16) so far ridges stay visible against the white sky; terraces = alternate 25 m height bands one or two
  grey steps apart; roll is a vertical shear of the horizon; spans are written as bytes into native rows (pattern byte per level and
  column phase, masked at both ends). **Work per frame**: 120 rays, 5760 steps, 2880 bilinear + 2880 nearest samples, about 6400 spans.
- **Timing method.** PC (Apple silicon, -O2, `build.sh simbench`): 3D frame 0.04 ms, top-down frame 0.03 ms, top-down picture
  0.66 ms (spread over ~20 frames), physics step 0.001 ms (an `Air::sample` is 1 us). The M4F estimate uses two methods that bracket the
  answer: (1) *calibrated ratio*: the same binary times the vario's thermal page (device 5-6 ms) and compass page (device 7.6 ms);
  the PC/device factor is 35 and 86 (mean 61) and gives 2.4 ms for the 3D renderer, 2.7 for the 3D frame, 2.0 for the top-down frame. This is the
  optimistic bound: those pages are pixel-call heavy, the renderer is float and table-lookup heavy, which a Cortex-M4F without
  cache runs relatively slower than a desktop core. (2) *cycle count*: the inner loop compiled for the device (arm-none-eabi-g++ -O2
  -mcpu=cortex-m4, about 45 instructions per nearest sample and 85 per bilinear sample) at
  110 / 60 cycles per sample (loads 2, VFP 1-3, branches 1-3, flash wait states), 40 per span: 0.79 M cycles = **12.3 ms**. That
  is the number to design with: 3D frame about 12-14 ms (renderer 12 + HUD about 1 + clear 0.2 + present 2.5 ms CPU, the DMA
  send overlaps), which fits the 20 ms budget with about 30 % headroom. The top-down view: picture slice 4500 px/frame x 90 cycles =
  6 ms + blit and overlays 2 ms = about 8 ms; the full picture takes ~20 frames (0.4 s), so a heading change reaches the
  map with that lag (the glider symbol turns exactly, the map follows). To be measured on the device with the cycle counter
  (`prof` sections) when the box runs it.
- **Fallbacks if the device measures over budget**: 1) kColStep 3 (80 rays, -33 %); 2) kBilinearSteps 16; 3) fewer steps
  (`ds = 24 + 0.10 s`, 36 steps); 4) 160 x 267 rendering doubled in both directions. Each is a constant in `voxel.h` / the step table.
- **Sizes** (device builds): box image flash 299720 -> 337432 B (+37.7 kB), RAM 90088 -> 173024 B (+83 kB: height cache 48 kB, top-down
  pictures 22.6 kB, trail, thermal tile cache, voxel tables). When the pack reader is wired the box also needs the 47.7 kB
  TerrainKit: about 221 kB of 249 kB. If that is too tight: window 96 x 96 (27 kB), one top-down buffer (-11 kB).
- **Looks, critically.** The map view reads well (heading up, contours every 200 m, hillshade); the glider symbol is small against
  the contours and could use a bigger halo; the assist rings are subtle (dithered 4 px rings) as intended. The 3D view works but
  is plain: the 200 m DEM has no detail below its cell size, so close to the ground the terrain is smooth, and the bilinear
  heights show as long gentle slopes with terraces as the only texture. Ideas: forest/rock tone from altitude and slope, clouds
  at the cloud base, a canopy edge at the top of the screen.
- **Physics notes.** Sink in a turn: the first idea (sqrt(n) x polar(V / sqrt(n))) gives only +4 % at 35 degrees and trim speed because trim is
  above the minimum-sink speed; induced drag (n^2) gives +30 %, which matches what pilots feel. Max bank is 35 degrees (40 gave
  46 deg/s, too nervous). A thermal column leans with the wind and the glider drifts with the air mass, so a pilot has to keep
  re-centring (relative speed 3-7 m/s); the first version also drifted the whole thermal at 25 % of the wind and nobody could stay in it.
  The ridge lift sign was wrong in the first version (caught by looking at the vario in the ridge screenshot, then by the test).
- **Site coordinates.** The given Sopot coordinates are the town (530 m). The takeoff in the table is the 1400 m slope 3.8 km to the
  north; all five sites were checked against the DEM and are marked approximate.

## Climb column and the centring page (2026-10-16, emulator and builds only, not flashed)

Prototype: `core/thermal3d.*` (per-circle core, lean, strength by height, trend, the centring cue), the vario page "centring" after the
thermal page (page 5 for the laptop), `emu cue` (hit rate of the cue on a flight). The first design was a side view plus a perspective 3D
column of the helix; both rendered well and were dropped: a pilot who is circling needs to know where to move the circle, not a picture of
the climb so far. The core maths stayed.

**What the page shows (no words):** heading up, the centre of the newest turn in the middle; the last two turns, thick where it climbed;
a bead at the best lift of the turn; the core as a target and a fat arrow centre to core (length = distance); dotted rings 25 / 50 m;
the climb now (number), a gauge (bar turn average, diamond best, triangle now); lean arrow (length = tilt); trend glyph; strength-by-height bars.
It needs two full turns (about 50 s): after one turn the cue is too often a circle that is not one. Screens: `docs/screens/center_cue.png`,
`center_sequence.png` (SVE climb from 4185 s, Sopot climb), scenarios `center*` in `build.sh`.

**Real climbs** (SVE flight from `--start 4185`, Sopot `--start 1100` and `3100`, Sopot with the GPS altitude since its pressure altitude is 0):
* The wind estimate decides everything about the lean. A wind error of dw adds 100 dw / climb m per 100 m. On the SVE climb with the right
  wind (10-14 km/h toward 275-300) the core leans 16-33 m per 100 m toward NW-NNW with rms 25 m of the core points; started at 4000 s instead the
  wind estimator locked on 42 km/h (wrong by 30 km/h: the airspeed in the circles came out 1-19 m/s) and the lean read 200-300 m/100 m. The page
  shows no lean above 60 m/100 m. That wrong start-up wind was an estimator problem, fixed 2026-10-17 ("Wind EKF locked high" below).
* Sopot: lean 6-26 m/100 m, no stable direction; the lift-weighted core wanders about 20-30 m from circle to circle.
* Strength by height followed the climb: SVE climb 4.0 m/s at 1700 m, 2.0 at 1800 m, 0.3 at the top at 1872 m; the trend read BUILDING, FADING, TOP in that order.
* The air-frame heading is useless for finding circles when the wind estimate is off (above); the ground course is robust. Circle periods
  were 24-30 s, turn rates 8-14 deg/s: a 6 deg/s threshold missed the slow ones, 4 deg/s with gaps of 8 s bridged finds them.

**Prediction measurement** (`build.sh cue lines.txt`): every second of circling the cue is recorded; one turn later the same flight's next
turn is compared with it. (a) lift: the lift-weighted centroid of the next turn lies within 90 degrees of the direction the cue pointed to
(only cues of 10 m or more, next-turn asymmetry of 3 m or more). (b) pilot: the pilot's own re-centring (next turn's centre minus this turn's, in the
air frame) agrees with the cue. Final settings: newest turn only, gain 2.5, two turns, no lean extrapolation.

| flight | cues (s) | cue vs next turn's lift | pilot re-centring agrees | median cue |
|---|---|---|---|---|
| SVE 4185, 400 s | 155 | 7 of 7 (100 %), median angle error 38 deg | 4 % of 23 | 7 m |
| Sopot 1100, 700 s | 150 | 18 of 19 (95 %), 46 deg | 93 % of 29 | 7 m |
| Sopot 3100, 500 s | 203 | 60 of 71 (85 %), 39 deg | 51 % of 85 | 18 m |

Honest reading: the side of the circle where the lift sits is stable from one turn to the next (85-100 %), so the arrow points where the lift
was. How far the core is, is not known better than about 40 m (median distance error against the next turn's own lift, 37-43 m). The pilot
agreement says little: with a wind estimate a few km/h off the circle centres drift by 10-15 m per turn in the air frame, which looks like
re-centring (SVE), and the pilots on these climbs were mostly centred already (median cue 7 m). Following the cue paid off on Sopot 1100
(+0.22 to +0.68 m/s better next turn), not on Sopot 3100 (-0.9 m/s) where the thermal broke up. Not measured: a pilot flying with the cue.
Tried and worse: blending the two older turns (lift side 41-59 % on Sopot, from 85-90 %), extrapolating along the lean (no gain with the newest turn,
worse blended), gain 1 (shifts underestimated). Cue availability: 39 % of the seconds on SVE (the first 50 s and the exit), 21-41 % on Sopot.

**Cost:** one `analyze()` with 600 points 19 us on the PC (about 1 ms on the nRF52840 by the PC/device ratio of the other maths), once per data
second and only on the page. The page draws about 60 line segments, 96 ring dots, 2771 Bitmap1 pixel writes, 285 spans, 2669 rows
(the thermal map: 438 pixels, 1311 spans, 5949 rows, 22 ms on the device), so roughly 10 ms; the PC draw is 0.2 ms. Column RAM 19 kB
(the box image is at 138 kB of 249 kB). Redraw at the normal 3 Hz (new points arrive at 1 Hz, the head at 3 Hz); no animation cadence is needed.
**Unchanged:** all 48 emulator screens byte-identical, the replay digests identical for pages 0-3 and for one press to the thermal page on both
flights; the page cycle has one more page (compass, thermal, centring, [map], profile, home), so the smoke script presses once more and the existing
tests that count presses were updated.

## Centring page: one cue (2026-10-19, emulator only, not flashed)

Implements improvements 1, 2, 3 and 5 of docs/thermal_review.md. `core/thermal3d.*`: `SectorCue` (`ThermalColumn::sec`) next to the old `CoreCue`
(kept for the tool's "before" numbers and the older tests); the page (`view/screens.cpp`, `drawCenterPage`) draws only `sec`.

**Page (1, 3):** a ring around you, heading up; the lift is one wedge of it, one of 8 sectors of 45 degrees (the bearing of the lift-weighted centroid
from the circle centre, quantised in the air frame, so the wedge turns round the ring once per circle: a clock face). The average climb of the last turn
is the number underneath. Removed from the page: lean arrow, strength-by-height bars, climb gauge, trend glyph, the older turn's trail, the 25 / 50 m rings,
the shift distance and the fat arrow, the wind arrow. Their maths stays in `thermal3d` (lean, bands, trend, tests); the Profile page is the wind profile, so
there was no place to move them to: they are dropped from the display, not relocated. Ink at a glance: ring, wedge, triangle, one number (was about ten
elements). Frames: `center_off` (circling 24 m off the core), `docs/screens/center_cue.png`, `center_sequence.png` (the SVE climb, one frame per 2-4 s).

**Estimate (2):** walk back from the newest point along the circling run in progress (turn rate >= 4 deg/s, 8 s gaps bridged); from 180 degrees of
turn on, use all of the run up to one full turn (the newest 360 degrees afterwards). A part of a turn has its circle centre fitted (Kasa least squares,
radius 8-90 m, else the mean), because the mean of an arc is not the circle centre. Confidence 1: part of a turn; 2: a full turn; 3: a full turn whose direction
agrees (< 60 degrees) with the turn before.

**Gate (5):** the cue is shown only when the pushed core is at least 10 m from the centre (else you are centred: nothing) and the previous full turn's direction is
within 90 degrees (else the thermal has no consistent side: nothing). The older turns' spread was tried as the review proposed and does not predict anything
(below), so its gate is off (`secMaxSpreadM` = infinity). The 60 m cap on the shift was removed for the sector cue: it threw away the clearest cues (raw offsets
over 24 m); coverage went up with the hit rate unchanged.

**Measurement** (`build.sh cue lines`, whole flights, GPS altitude for Sopot; the lines are the ones of the review). Circling seconds = seconds of a run that reached 270
degrees (found afterwards). Hit = the cue's direction lies within 90 degrees of the next full turn's own lift offset (>= 3 m), one turn later; chance 50 %.

| | Sopot 5216 s (1562 circling s) | SVE 10905 s (4773 circling s) |
|---|---|---|
| before: cue in circling s (old cue, two turns; with its own >= 10 m rule) | 56 % (34 %) | 44 % (29 %) |
| before: cue in all flight s | 18.0 % | 20.8 % |
| before: hit rate vs next turn (n), baseline last turn | 68 % (236), 61 % (46) | 64 % (554), 55 % (303) |
| after: cue in circling s | 53 % | 60 % |
| after: cue in all flight s | 16.0 % | 26.5 % |
| after: hit rate unquantised (n) | 68.5 % (372) | 70.0 % (1655) |
| after: hit rate in 8 sectors | 68.8 % | 69.4 % |
| after: first half turn(s), before the 2-turn cue could exist: cue in circling s there / hit | 61 % / 70.9 % (n 278) | 63 % / 65.5 % (n 855) |
| after: partial windows only / full turns only, hit | 70.8 % / 62.1 % | 65.5 % / 74.6 % |

Reading: the early cue holds the 65 % bar on both flights (70.9 and 65.5 %), so it stays; it covers the first 50 s of a climb where the old cue was absent. Quantising to
8 sectors costs nothing (the review expected a fall to 35-40 %: that was for a tolerance of 45 degrees, not 90). The count of seconds with a cue in the
circling seconds is not larger than the old cue's *unconditional* figure, because the old one also showed 0-9 m shifts (centred) that the gate now hides: against
its own >= 10 m rule the coverage doubles (29 -> 60 %, 34 -> 53 %). The first 50 s of a climb: 455 -> 1498 seconds with a cue (SVE), 218 -> 519 (Sopot).

**Curves** (hit rate vs coverage of circling seconds, ungated start: SVE 68 % at 82 %, Sopot 66 % at 75 %): they are flat. Minimum shift 3 -> 20 m: SVE 68 -> 70 %, Sopot 66 -> 63 %
while coverage falls from 82 -> 60 % / 74 -> 42 %; the disagreement gate 90 degrees gains about +1 point and costs 10 points of coverage; the older turns' spread
gate (100 -> 20 m) gives SVE 67 -> 64 % and Sopot 69 -> 71 %, a loss of 40 points of coverage: no single gate buys accuracy on both flights. Window start: 150 degrees 67.9 / 67.7 %,
180 degrees 69.1 / 68.4 %, 200 degrees 69.8 / 69.9 % (SVE / Sopot, with the spread gate then still on); a fixed 180 or 270 degree window instead of "all of it, up to a turn" is worse (63 / 59 %).
Mean climb of the window as a gate: no gain. Full-turn cues 74.6 % on SVE but 62 % on Sopot (the broken Sopot 3100 thermal), so confidence 3 is not more reliable everywhere: the
darkness of the wedge means "more data", not a measured hit rate. The sample is the same two flights with every second counted (strongly autocorrelated): differences of 2-3 points are noise.
Not measured: a pilot flying with it (the review's item 7), and whether the cue helps the climb (still confounded, as in the review).

**Emulator tests:** `sector cue` in `thermal3d_tests.h` (direction 90 degrees = sector 2 for a circle 20 m west of the core, a part of a turn gives confidence 1, 108 degrees of
run gives nothing, centred gives no sector but a climb number, a climb that ended gives neither, wind does not matter). The other pages are untouched (`box_vario_matches_alone`, digests of
the replay on pages 0-3 equal before and after).

**Simulator test failures reported once (`sim_playability`, `sim_day_settings`, "ended without finishing"):** not reproduced in four full runs here (0 failures; the two tests take
44-78 s alone, they run first and each forks up to 8 children). "Ended without finishing" means the test process left no `@@` line, i.e. it was killed (a signal; the exit status is in
the message). The likeliest cause is the reviewer's environment (a time or process limit hitting the two longest tests, run together with everything else). The harness now runs such a test once
more on its own and prints a note, so a one-off kill does not fail the run and a real crash (twice) still does. Also found: three tests read the big terrain pack from a session scratchpad path
(`sim_tests.h`, `map_page_tests.h`, `intro_shots.h`, `play.cpp`, `smoke.sh`): their results depend on whether that file exists on the machine.

## Flight simulator: time acceleration (x1 / x3 / x5, default x3)

- **Why**: nobody waits in real time for a 20-minute glide. The player (F key) and the pause menu's Speed row cycle x1 / x3 / x5. Not
  persisted (nothing of the sim is yet); it lives in `SimApp`, so it survives New flight within a session.
- **How**: every 20 ms frame `SimApp::step` runs `scale` world substeps of dt = 20 ms each (x5 = 5 substeps), never one big dt. A
  substep is exactly what x1 computes, so x3 for T real seconds equals x1 for 3T (test `time_scale`). Rendering stays 50 fps: one
  top-down update and one draw per frame.
- **Scales (sim time)**: glider physics including the wing's lags (roll 0.4 s, speed 1.2 s, bar/brake 0.35 s, stall), air (thermal
  life cycles, wind drift, time of day), the flight clock `World::time()`, the vario lag, the trail (one point per sim second, so
  the 4 minute trail covers 4 sim minutes), landing/crash detection per substep (same checks as x1).
- **Does not scale (real time)**: the pilot's hands: button ramps (bank 0.33 s to full) and the tilt mapping, the STALL blink, the
  frame rate, the host's fixed step and catch-up (kMaxCatchUp 4 frames; each runs `scale` substeps, so at most 20 substeps per host
  iteration at x5).
- **Feel**: the command reaches the wing as before, so bank, turn rate (physical, omega = g tan(bank) / V) and sink are the real ones
  in sim time; in real time the turn is `scale` x faster (a circle in about 3 s at x5 and 35 degrees) and the roll lag is `scale` x
  shorter. Slowing the ramps by the scale would make a button tap unusable; scaling the wing lag would make the glider lazier
  than physics. So the physics is the physics and the hands stay real.
- **Cost**: 5 physics substeps per frame, about 0.005 ms on the PC; negligible next to the render.

## Wind EKF locked high (2026-10-17)

**Symptom.** Replay of `2026-08-16-XCT-SVE-09.igc` from 4000 s: the shown wind was 42-46 km/h from 205 s on and stayed above 30 km/h for
about 1000 s (truth 8-16 km/h toward 260-300). The sopot flight from 1500 s did the same (27 km/h shown, truth about 7).

**Reference** (`host/wind_ref.py`, shares no code with the firmware). Geometric (not algebraic) robust circle fit of the ground-velocity vectors
over 36 s windows in which the glider turned the same way by at least 300 degrees; gates: radius 25-50 km/h, residual 6 % of the radius,
at least 80 % of the points kept. The reference at a second is the median of the accepted windows within +-45 s, needing 3 that agree within
2.5 km/h (SVE: 3144 s with a reference, 549 windows; sopot: 1062 s, 318 windows). `build.sh windtrace lines.txt` writes the per-second
device state (raw fit, smoothed fit, EKF, shown wind, ages) and the script scores it at several start offsets, from 60 s after a reference run
starts (a causal estimator needs a turn first). Thermal drift of the circle centres is a sanity check only (`--table`).

**Cause** (traced second by second; minimal reproduction `wind_tests::test_glide_scatter_teaches_nothing`):
1. The EKF applied its pseudo-measurement `|g - w| = Va` every second, also in a straight glide. There the airspeed is not constant (the
   ground speed swings 29-50 km/h, the pilot is on and off the bar) and the course wanders +-15 degrees. With almost no heading change the wind is
   not observable, and the filter explained the speed scatter with a large wind along the track and an airspeed that sank to its 5 m/s floor
   (wind 45 km/h, Va 5.0 m/s).
2. Its covariance shrank anyway (the model says Va is constant, R = 1 per sample), so sigma went below 0.8 m/s and the filter was
   *confident* at 205 s, before any circle. wind() prefers a confident EKF.
3. The seeding rule added with the circle-fit seed ("only while the EKF is not confident") then made it permanent: the first circle fit
   (340 s) found the EKF confident and left it alone. A wrong state with a small covariance also rejects every correct measurement at the
   3 sigma outlier gate, so it could not recover; it crept back only through the process noise (about 1000 s).
4. A second face of the same lock-out, found while testing: with speed noise above about 1.6 m/s the old filter froze at a wrong wind even during
   circles (all measurements outliers, state never changing).
Not the cause: units, latency of course/speed (the IGC-derived lines are consistent), the circle fit (it was right in that episode: 10.8 km/h
toward 278), the seed value. Raising the airspeed floor to 7-8 m/s made it worse (66-82 km/h): the wind is the dumping ground.

**Fix** (`windekf.*`, `flight.*`; the circle fit is unchanged):
- *Learn only in a turn.* A measurement is applied only when the course has moved by at least 30 degrees within the last 20 usable
  seconds; otherwise only the covariance grows (wind sigma +0.0004 per second as before, so the wind stays "confident but old" through a long
  glide exactly as before). The course history is a 20-float ring in the filter. Tried 10/15/20 s windows and 15/25/30/45 degrees: all remove
  the lock; 30 degrees in 20 s keeps the S-turn tests (the apex seconds of a 100 degree S-turn still count).
- *Outlier balance.* Outliers in turning flight count +1, accepted updates -1. At 5 only the airspeed variance is reopened (the usual
  cause is a new speed regime on entering a thermal; the wind is kept), at 10 the whole covariance (P = diag(25, 25, 9)): the wind is then not
  confident and not shown from the EKF until the next turns pull it back (a change to 30 km/h after a settled 12 km/h estimate is followed
  within about 35 s of circling; before: never).
- *Restart from a persistent contradiction.* If the EKF is confident and 8 accepted circle fits in a row are more than 3 m/s (11 km/h) away
  from it, it is restarted from the smoothed fit (P = 4, not confident), so a wrong confident EKF cannot outlast the first circles. The first
  fit after a gap still leaves a confident EKF alone for those 8 s (an EKF that is right is not thrown away by one 30 s fit). On the two flights
  this rule never fired (the learning gate alone fixes them); it is the safety net for the failures the gate cannot see.
- Honesty: a restarted or inflated EKF is not confident, so `wind()` falls back to the circle fit (with its own age) or to
  invalid (`--`). The age rules are unchanged.

**Result** (km/h vector error of the shown wind against the reference, hard reference seconds only; "wrong" = shown with age <= 120 s and more
than 8 km/h off). Full tables: `host/wind_ref.py <igc> --starts ...`.

| flight | start | before: median / p90 / max | wrong | after: median / p90 / max | wrong |
|---|---|---|---|---|---|
| SVE | 0 | 2.2 / 3.5 / 12.6 | 64 s | 2.2 / 3.4 / 8.2 | 36 s (8.0-8.2) |
| SVE | 1500 | 2.2 / 3.6 / 12.6 | 64 s | 2.2 / 3.4 / 8.2 | 36 s |
| SVE | 4000 | 2.6 / **35.5 / 38.9** | **316 s** | 2.1 / 3.4 / 6.0 | 0 |
| SVE | 4185 | 2.2 / 3.5 / 6.4 | 0 | 2.1 / 3.4 / 5.8 | 0 |
| SVE | 6000 | 2.1 / 3.1 / 5.1 | 0 | 2.1 / 3.1 / 5.1 | 0 |
| sopot | 0 | 1.8 / 3.5 / 8.6 | 2 s | 1.6 / 3.5 / 6.4 | 0 |
| sopot | 1000 | 1.9 / 4.0 / 6.5 | 0 | 1.6 / 4.1 / 6.7 | 0 |
| sopot | 1500 | 13.1 / **32.7 / 33.5** | **254 s** | 1.4 / 2.5 / 3.2 | 0 |
| sopot | 2000 | 20.8 / **25.9 / 29.2** | **217 s** | 1.5 / 2.6 / 3.2 | 0 |
| sopot | 3000 | 2.1 / 2.6 / 3.2 | 0 | 2.1 / 2.5 / 3.2 | 0 |

All 15 SVE offsets (0-6500 s) and 11 sopot offsets (0-4000 s) were run. After the fix the worst error of the shown wind over all of them is
8.8 km/h (SVE, start 3000: the stretch where the wind changed by about 14 km/h between two thermals and the stiff EKF, Q_W = 0.0004, needs
about 60 s of circling to follow; the circle fit follows in about 30 s) and the median 1.4-2.3 km/h. The reference itself is good to about
2-3 km/h (spread between its windows), so differences below that are not meaningful.

**Cost.** A 20-float ring and a 20-step max loop (one `fmodf` each) once per data second in the EKF; 4 bytes in `FlightModel`; RAM about
+100 bytes. The EKF update itself runs less often (only in turns).

**Tests.** `wind_tests.h`: the glide-scatter reproduction (old code: confident at 50 km/h; new: never confident), recovery after a wrong
confident state (change to 30 km/h; old: stuck at 11 km/h), and replays of the SVE (from 4000 s) and sopot (from 1500 s) excerpts
(`wind_fixtures.h`, generated by `host/make_wind_fixture.py`): no confident EKF before the first circle, never more than 8 km/h
from the reference once a wind is shown. They fail on the old code (8 failures). `test_ekf_seed_from_circle_fit` also covers the restart from a
persistent contradiction. Whole flights: `host/wind_ref.py <igc>` (manual), `build.sh wind lines.txt` as before.


## Flight simulator: playable air, chase camera, Day settings (2026-10-17)

User feedback after playing: "I can't even reach a thermal". `build.sh simreach [PACK] [seeds] [t0..]` (`emu/sim_reach.cpp`, shared code
in `emu/sim_reach.h`) measures it: per takeoff, seed and start time (180 / 1800 / 3600 s) the thermals working at launch (core
>= 1.5 m/s with the life cycle applied, within 3.6 km), the nearest, how many a straight trim glide reaches (re-aimed at the moving
column, >= 30 m above the ground on arrival), how many of those are still reached when the air sinks 0.5 m/s more ("with margin"), and
what a scripted pilot (nearest reachable thermal, circle at 0.3 R, soar the slope when nothing is in reach) gains in 10 sim minutes.

| | old air (same launch 150 m, wind 5 m/s) | new air, medium day |
|---|---|---|
| thermals working within 3.6 km | 1.6 - 3.6 (Bansko 1.6) | 11.6 - 16.1 |
| nearest working thermal (median) | 870 - 2300 m | 160 - 400 m |
| reachable at trim | 0 - 1.2 (mean) | 5.0 - 10.6 |
| reachable with the 0.5 m/s margin | 0 - 1.0 | 3.4 - 8.3 |
| runs with fewer than 2 reachable with margin | 86 of 90 | 2 of 288 (Sopot, t0 180 / 1800) |
| scripted pilot gains >= 500 m in 10 min | 2 of 90 (2 %) | 264 of 288 (92 %), median gain 700 - 1600 m |

(the very first run, with the old 60 m launch height and the old measure, was 0 of 90). Per site, new air (12 seeds x 3 start times):
Sopot 33/36, Kalofer 32/36, Vratsa 32/36, Sliven 34/36, Bansko 36/36, Shumen 33/36, Madara 30/36, Nevsha 34/36.

**What was wrong.** (1) The launch was 60 m above the takeoff with a 5 m/s headwind: ground speed 5 m/s at 1.9 m/s sink is a
glide of 2.7 over the ground, so anything beyond 160 m was out of reach, and the nearest thermal was 0.9 - 2.3 km away. (2) The
ridge lift (0.9 x wind x slope, slope from 150 m differences on a 200 m grid: noisy, sign flips every 400 m) was cancelled by the
general sink of 0.5 - 1.0 m/s applied on top: net about zero on the slope, no soaring. (3) Thermals were few: one trigger per 1.8 km
cell fired with 25 - 90 % chance, a period of 900 - 1500 s against lives of 600 - 1200 s (about a third of the time active), cores
1 - 5 m/s with a median of 1.6, radius 50 - 150 m: about 3 working thermals within 9 km and most too weak for a turn (a turn costs
0.35 m/s more sink, the Gaussian core at 0.45 R only 0.8 of its peak). (4) Nothing tied a trigger to the launch (real sites have
known trigger spurs). (5) The table wind did not match the terrain (Sopot 190 degrees, the slope at the model's launch faces 138).
(6) Launch spots in gullies or behind a knob: Sopot's launch cell is a notch between two bumps of +125 m.

**Tuned (medium day).** Cloud base 2200 - 2800 m (seeded). General sink 0.3 - 0.6 m/s (seeded), faded to nothing where the ridge
lifts by 1.2 m/s, reduced in cores. Thermal cells: period 600 - 1000 s, life 900 - 1500 s (15 - 25 min, overlapping; the collector
looks 3 numbers back), fire probability (0.35 + 0.6 score), core S = 2.0 + 2.6 u^1.2 (0.5 + 0.5 score) = 2.0 - 4.6 m/s, radius 80 - 200 m
widening 60 % per 1000 m of climb, lean with the wind as before. House thermals (`Air::setHome`, 3 per takeoff): trigger spots
250 - 2000 m from the launch, 30 - 500 m below it, whose bubble (leaning downwind) crosses 150 - 800 m from the launch at launch
height, with a 4:1 glide from the launch that clears the terrain, best sun score, in different directions (a relaxed search if the
terrain has none); period 500 - 700 s, life 1100 - 1500 s (always >= 2 alive per house), S 2.6 - 4.4. Ridge lift = 1.0 x wind . slope,
gradient on 300 m, decay with height 260 m. The start is 150 m above the takeoff (`World::kLaunchAgl`: the first seconds after
leaving the slope), the wind blows onto the slope (direction from the 600 m aspect of the terrain model) at 5 m/s (18 km/h), 3
minutes into the sim day. Test limits in `sim_tests.h` follow (cores 2 - 4.6 m/s, radius 80 - 200 x 1.9, cloud base 2200 - 2800).
**Lift feel.** `AirSample.gx/gy` is the analytic gradient of the thermal part; the lift difference across the 10 m span (right minus
left wing tip) gives a roll kick of 0.35 rad per m/s, at most 8 degrees, lagged 0.5 s, added to the commanded bank (`World::rollKick`,
`Glider::step(..., rollKick)`). The tip on the stronger side rises, so the glider banks away from the core (physical): the pilot
turns toward the high wing. At a thermal's edge (0.8 R of a 3 m/s core) it is about 4 degrees.

**Playability test** (`playability()` in `sim_tests.h`, Sopot fixture 8 seeds x 2 start times, and when the real pack exists all 8
takeoffs x 4 seeds): every run has >= 2 house thermals, >= 2 thermals reachable with the 0.5 m/s margin, nearest < 1.5 km; the
scripted pilot gains >= 500 m in 10 min in >= 80 % of the runs (measured: fixture 14 of 16, real pack 31 of 32). The measured rates are
printed. Also `day_settings()`: levels order strength, density, cloud base and sink; the scripted pilot's mean 10 minute gain on the
fixture by level weak / medium / strong / epic = 507 / 916 / 1176 / 1387 m; wind direction flips ridge lift and lee sides; calm = no
ridge lift; a wind change eases in at 0.6 m/s per second.

**Day settings.** `SimApp` opens a "TODAY" screen (Site, Thermals, Wind from, Wind speed, Start; `showDayScreen(false)` for tests
and tools) and the pause menu has the same rows. Thermals (`Air::setLevel`): weak / medium / strong / epic scale the core strength
(x0.8 / 1 / 1.25 / 1.5), the fire probability (x0.7 / 1 / 1.15 / 1.25), the cloud base (-350 / 0 / +250 / +450 m) and the sink
(x0.7 / 1 / 1.3 / 1.6): more lift up goes with more sink in between. Measured means (thermals > 1 m/s) 2.2 / 2.7 / 3.3 / 3.9 m/s, cloud
base 2219 / 2569 / 2819 / 3019 m, sink 0.31 / 0.44 / 0.57 / 0.70. Decision for changes during a flight: the wind applies at once and eases in (the
air is a continuous function of the wind vector, so a ramp of 0.6 m/s per second never jumps under the glider); the thermal level and
the site regenerate the day and so apply at New flight (the menu row shows `*` while pending), because re-rolling the thermals under
a flying glider would pop columns in and out.

**Takeoffs added** (NE plateau): found with the terrain model around the rough positions (steepest convex edge cell: slope >= 0.15 at
400 m, >= 200 m of drop within 1.5 km downhill, the plateau behind it, wind = the way the edge faces): Shumen 43.2529 N 26.9272 E 460 m
(SE-facing, 2.9 km from the guess: the model's best edge near the town), Madara 43.2709 N 27.1188 E 405 m (NW, 0.2 km from the
guess), Nevsha 43.2256 N 27.4744 E 330 m (NW, 3.6 km east of the village: the model's plateau edge above the Provadiya valley, 40 m
floor to 300 m plateau). The pack covers them and the relief reads as plateau and valley (Shumen plateau 480 m over a 300 m valley;
Madara 405 m over 130 m; Nevsha 330 m over 40 m). All approximate.

**Chase camera** (`sim/chase.*`, replaces the first-person view; the enum is `View::CHASE`). 46 m behind, 20 m above, pitch 0.09 rad
(horizon about 15 px lower: more ground in front), heading smoothed with tau 0.5 s of sim time (the update runs in sim seconds, so
the lag looks the same at x1 / x3 / x5), roll = 0.35 of the bank (tau 0.3 s); the camera is lifted to 8 m above the ground behind
the glider. The sprite (half span 46 px, a real 5 m at this distance would be 19 px: exaggerated to read) is drawn where the glider
projects (lower middle, sideways in a turn), a black outline pass then a white fill: the canopy as a stack of arches with its seams,
the lines and the pilot, rolled by bank minus camera roll, the canopy thickness 9 px at trim growing to 17 px at full bar (nose
down). Shadow: a flat ellipse where the glider's position meets the ground, only while that is on the screen (AGL below about
60 m). With tilt steering the camera roll is the box roll (horizon counter-rotation as before). **Device estimate** (same two
methods as step 1, `build.sh simbench`): renderer unchanged 12.3 ms (cycle count; 0.78 M cycles), sprite + shadow 22 us on the PC =
1.4 ms with the calibration factor 60 (about 800 short line calls), camera maths negligible, HUD about 1 ms, sim step 0.5 ms: about
15 - 16 ms of the 20 ms frame (was 13 - 14), no margin for a second sprite. If it must shrink: halo outline only on the top arch
(saves about 0.5 ms).

## Flight simulator: clouds and thermals in the 3D view (2026-10-17)

User: "can we have the thermals visible in the 3D mode?" `sim/sky3d.*` (`SkyFx`), `Air::columns` / `columnPoint` (`ColumnInfo`), a depth buffer in
`VoxelView`. Renders: `docs/screens/sim_thermal_styles.png` (contact sheet: approaching a thermal at 2.3 km and circling in it, assist off and the
three styles), `sim_clouds_approach.png`, `sim_thermal_approach_bubbles.png`; `build.sh simdemo [PACK] [OUTDIR]` makes them (real pack),
`build.sh simtests [sky_fx]` runs the sim tests alone in seconds (no sanitizers).

- **Cumulus (always on).** One per thermal, from `Air::columns`: none until 20 % of the thermal's life, full from 40 % to 60 %, thinning to
  nothing at 95 % (a smoothstep up and down), centre over the column's top at the cloud base plus a downwind drift of 0.10 x wind x age since it formed,
  width (450 + 140 S) m x (0.3 + 0.7 x development). Drawn as 7 discs (5 on a flat base, 2 on top, shape from the thermal's hash) column by column:
  black outline (thicker where the edge is steep), white top, grey body, dark base band, black base line; the nearest 4 within 9 km and in front.
- **Depth against terrain (choice).** Not "clouds first, terrain over them": the terrain renderer writes spans front to back and the sky last, so
  drawing clouds before it would need the terrain to skip them. Instead `VoxelView` stores, per ray (120) and per bin of two steps (26 bins, 6 kB RAM), the
  highest screen row covered by terrain NEARER than that step (one store per two steps in the ray loop, about 0.3 ms estimated). A cloud, shaft or bubble at
  distance s is clipped to the rows above `nearerRow(column, step(s))`; exact at bin edges, conservative in between. Test: a 4 km wall hides a cumulus
  behind it (picture = bare terrain), the same cumulus is drawn from beyond the wall.
- **Thermals (assist on), `Thermals view` row of the pause menu** (default bubbles): *columns* = a curtain from the trigger to the cloud base along the
  leaning column (5 segments, 1.5 x core radius wide, dashed vertical hatching every 4 / 6 px by strength, dashed outline); *bubbles* = rings rising
  through the column at 8 x S m per sim second (a visual speed: the real 3 m/s is not seen to move at 1 km), 4 - 8 per thermal by strength, more of them
  near the axis; *both* = outline and bubbles. The thermal the glider is in always shows 16 larger bubbles around its altitude. The first attempt (XOR
  dots) was unreadable over the dithered sky and hillside; dashed black lines read on both.
- **Honest recommendation: bubbles as the default.** Cheapest (0.45 ms vs 1.2 ms counted), cleanest, and the one that carries strength (speed and number) and
  the lean (the chain of rings follows the column). Columns show the extent and the exact lean but their hatching reads as a band at a distance, fill
  the view when a thermal is close, and cost more. Both adds the outline to the bubbles and looks busier than either. Stills cannot judge the motion of
  the bubbles; this wants one flight on the device.
- **Budget** (`build.sh simbench`, approach scene with 4 clouds and 3 thermals in view, renderer unchanged at 12.3 ms by cycle count): the
  estimates are cycles counted inside `SkyFx` (per column evaluation, per span byte, per bubble) next to the PC time x 59 (calibration):

  | scene | clouds | thermals | counted | PC x 59 | 3D frame (16.3 + 0.14 columns + this) |
  |---|---|---|---|---|---|
  | assist off | 4 (134 cols) | | 0.35 ms | 0.29 ms | 16.8 ms |
  | columns | 4 | 3 shafts, 404 cols | 1.19 ms | 0.58 ms | 17.6 ms |
  | bubbles | 4 | 11 rings | 0.45 ms | 0.46 ms | 16.9 ms |
  | both | 4 | 3 shafts, 180 cols, 11 rings | 1.15 ms | 0.70 ms | 17.6 ms |

  The base 16.3 ms is the earlier 15 - 16 ms frame plus 0.3 ms for the depth buffer. Hard limits make the worst case bounded: clouds <= 1.0 ms
  (64 k cycles), assist <= 1.5 ms (96 k cycles) plus the glider's own bubbles (about 0.15 ms), `Air::columns` about 0.15 ms: worst frame about 19.1 ms, under
  the 20 ms budget but with little margin. Adaptive degradation (`SkyFx::draw`): cost is estimated from the geometry before drawing; over the budget the clouds use 4
  columns per evaluation instead of 2, the shafts use 3 segments instead of 5 and the thermals 4 bubbles instead of 8, and then the farthest clouds and
  thermals are left out (at most 3 shafts, 28 bubbles, 4 clouds). On the PC the counted numbers are the ones to trust (the calibration factor is a
  rough ratio, as for the sprite); to be measured on the device.
- **Tests** (`sky_fx` in `sim_tests.h`): cumulus factor 0 before 20 % of the life and from 95 %, 1 between 42 % and 60 %, width grows
  with strength, drifts downwind with age and thins in the decay; deterministic rendering (same picture twice, bubbles and shafts too); behind the wall
  hidden, beyond it drawn; the three styles differ; budgets respected; bubbles move with time; the depth buffer is empty at step 0 and full at the last.
  The menu gained a row (13 rows, row height 25), existing key-sequence tests moved by one row.

## RAM budget: the box image that did not boot (2026-10-18)

Found on the device: `feather_sense_box` showed a blank screen and no USB at all. Static RAM (.bss + .data) is linked first and
`.heap` is what is left of the 256 kB; FreeRTOS takes the loop task's 4 KB stack, the USB, BLE and callback tasks and the Bluefruit
buffers from it. The box had 231544 bytes of .bss and 2376 bytes of heap, so no task could start. `arm-none-eabi-size -A
.pio/build/<env>/firmware.elf` shows it.

| image | .bss before | .heap before | .bss after | .heap after |
|---|---|---|---|---|
| feather_sense (vario only) | 91592 | 142356 | 91592 | 142356 |
| feather_sense_box, as built | 231544 | 2376 | 136628 | 97316 |
| feather_sense_box with the sim, first try (arena 93 kB + terrain) | 231544 | 2376 | 189972 | 43288 |
| feather_sense_box, sim slimmed (final, current) | 231544 | 2376 | 183364 | 49896 |

Largest statics before: sim::SimApp 93480, vario App 43472, a second full vario App for the compass tool 43472 (36 kB of it
the FlightModel the tool never fills), laptop 12996, panel DMA buffer gTx 12482.

What changed: only one app runs at a time, so the apps other than the start-up vario share ONE arena
(`platform/lazy_app.h`: `AppArena`, `LazyApp<T>`; the app is placement-new built in `begin()` and destroyed in `end()`). Tetris
and the compass tool (now `apps/vario/app/compass_tool.h`: the tool's App with its null tracer and laptop as one object) sit in
the arena; the vario `app` stays a plain static (start-up app, flight-critical). Costs: the app's state starts afresh each
time it is opened (the Tetris best score and the settings go through the key store, so nothing is lost; a paused Tetris game
no longer survives a trip home); a few ms to construct. The flight sim is in the box image (home screen "Flight sim"), always. Measured on the device (arm-none-eabi, sizeof):

| object | before | after |
|---|---|---|
| SimApp | 93480 | 79792 |
| World (glider, air 4488, trail, HeightCache 49196) | 56740 | 55780 (trail without z: 240 x 8 B) |
| top-down view | 24500 | 23748 (band rows int8) |
| voxel view + sky | 9012 + 2976 | share the top-down view's block (union) |
| Terrain for the sim | (vario TerrainKit 47672) | 6484 (2 tiles, no relief scratch) |
| arena (max of sim 86276, kit 47672, compass, Tetris) | 47664 (no sim) | 86304 |
| heap, feather_sense_box | 89588 (no sim) | 49896 |
| heap, feather_sense / feather_sense_diag | | 91772 / 52292 |

What was shared or shrunk: the top-down view (map) and the 3D view (voxel + sky) are never drawn together, so they live in one block
and the view that is not alive is destroyed (`SimApp::top()/voxel()/sky()` build the one asked for; the map picture was rebuilt on
every switch to the map anyway). `terrain::Terrain` takes its tile cache from the owner (`TerrainWith<N, relief>`; `FullTerrain` =
9 tiles for the vario); the sim gets `sim::SimBox` = SimApp + a 2-tile Terrain, built in the arena by main_box.cpp and emu/box.h
alike. The vario's TerrainKit is NOT used by the sim. Height cache (area, resolution) untouched.

Cost of the 2-tile cache: a line of the 128-cell window crosses 5 tiles, so every new line decodes them again (about 5 per line,
~1 ms each on the device; with 9 tiles almost none). Filling the window at the start of a flight takes 693 decodes instead of 34
(about 0.7 s blocking on the device, estimated, not measured). To keep frames steady, `HeightCache::refresh` stops after the first cell
that made the source decode a tile (`IHeightSource::cost()`), and `SimApp::step` gives the cache its whole frame's budget in the first
substep: a line takes ~5 frames, at most 4 decodes (four tiles meeting) in one frame, usually 1. The simdemo renders (16 scenes) are
byte-identical to before; the change only affects how fast the cache fills while flying.

Not reached: the 56 kB heap target (49896, guard 49152, margin 0.7 kB). Further options, none done: nibble-pack the shade array
(-8 kB, about +1.1 ms per 3D frame by the sim_bench cycle model, over the 20 ms budget); a tile cache of 6 (no re-decode, +8 kB); a
smaller window (needs a non-power-of-two cache). The 13 kB `laptop` frame buffer and 12.5 kB panel buffer are static.

Guard: `scripts/ram_check.py` (a step of `scripts/check.sh`) prints .bss / .data / .heap of every built image and fails when
.heap is below 48 KB. A new big object either lives in the arena or the check fails.

### The vario in the arena too (2026-10-19)

User decision: in the box image the vario is just another app and must not sit in RAM while others run. `VarioBox`
(`apps/vario/app/vario_box.h`) is ONE object: the flight `App` + the `VarioApp` adapter + the `TerrainKit`, a `LazyApp<VarioBox>`
in the shared arena (main_box.cpp and emu/box.h alike). `BoxTerrain` stays for the boot-time pack status cache only (it borrows the
arena once at boot, before the start-up app). The kit is opened on the pack reader in the vario's `begin()`.

| | before | after |
|---|---|---|
| static vario App (+ adapter 28 B) | 43472 (.bss) | 0 |
| VarioBox in the arena (App 43472 + adapter 28 + kit 47672) | | 91192 |
| arena (max of sim 86432, VarioBox 91192, compass tool 43512, Tetris 504) | 86432 | 91216 |
| .bss, feather_sense_box | 183904 | 145184 |
| .heap, feather_sense_box | 49216 | 87936 (+38.7 kB) |
| feather_sense / feather_sense_diag | 91364 / 52252 | unchanged |
| stack check, loop chain, box | 2628 B | 2628 B |

Accepted costs: leaving the vario destroys it, so the flight state (trail, wind fit, thermal memory, map cache, page) is lost and
the vario comes back fresh (compass page, no trail); the tracer link is re-found through `ITracer::resume()` in the new App's
`begin()`, laptop commands queued meanwhile are dropped (`App::begin` now always drains them). Not lost: the BLE radios and their
queues (`TracerBle`, `LaptopBle`, static; callbacks never touch the App), `BoxSettings`, the `WallClock` (the new App gets its
pointer), the saved tracer, calibration. The serial status line (`diagnosticsSetApp`) prints a short `st vario=none ...` line
while the vario does not exist. The PC stack check mis-resolves virtual calls when a class with several bases has a polymorphic base
before `VarioApp` (it added 660 B of phantom chain): `VarioBoxCore` is deliberately not polymorphic, its terrain feed is a member.


## Map pack in the QSPI flash (2026-10-18)

The pack (1.72 MB) sits in the Feather Sense's on-board GD25Q16C (2 MB, JEDEC c8 40 15). Written without the device: nothing here
is measured on hardware yet.

**Layout.** Sector 0: 32-byte flash header (`BGFL`, version, pack base 4096, pack length, CRC-32 of the whole pack, header CRC;
`platform/storage/pack_header.h`). The pack starts at 4096, so the reader (`FlashBlockReader`) exposes exactly the pack bytes:
`Terrain` and `MapVectors` see offset 0 = the pack's own header. Room: 2 MB - 4 kB; the Bulgaria pack leaves 0.37 MB free.
The header is written last by the loader, after the CRC of the stored bytes matched: an interrupted load = blank header = "no pack".

**Driver choice.** The Adafruit nRF52 core ships the nrfx QSPI driver (`nrfx_qspi.c`, enabled for the 52840 in `nrfx_config.h`) and the
variant defines `PIN_QSPI_*` (SCK 27 / P0.19, CS 28 / P0.20, IO0..3 = 29..32 = P0.17, 22, 23, 21; mapped through `g_ADigitalPinMap`).
Using it directly adds no library and no `lib_deps` line. Adafruit_SPIFlash would bring a flash-device table, SdFat-style
filesystem layers and a transport abstraction we do not use, for the same reads. The driver is used in blocking mode (no handler,
no interrupt: the SoftDevice owns the low IRQ priorities, and a spin of about 10 us per transfer is nothing). nrfx wants 4-byte
aligned word-multiple RAM transfers: reads and page programs go through a 260-byte staging buffer when the caller's buffer or
address is not aligned (aligned ones go direct, up to 4 kB per call); programs are split at 256-byte pages and padded with 0xFF
(programming 0xFF changes nothing). Reads: quad I/O (0xEB) once the QE bit (status register 2, bit 1) is set - the driver sets it if
needed (WRSR 0x01 with both bytes, else 0x31) and checks that two reads (pack start, end of chip) give the same bytes in 4-wire mode
as in single-wire mode, else falls back to plain fast read. SCK 16 MHz (32 MHz / 2); `QspiFlash::begin(div)` takes another
divisor. Writes use single-wire page program (0x02): the chip's own 0.4-3 ms program time dominates. Erase is started and polled
(`startErase`, `busy()`), 64 kB blocks where they fit, else 4 kB sectors. WP#/HOLD# (IO2/IO3) are held high in custom instructions.

**Deep power-down** is implemented (`powerDown()`, `wake()`, `setIdleSleepMs()` + `idle()` in `PackService::service`), but OFF by
default: the wake path has to ignore the driver's start-up timeout (the status read of a chip in power-down is not answered) and the
effect (about 14 uA of chip standby, plus whatever the idle QSPI peripheral takes) is not measured. TODO(device): measure the battery
current with and without, then enable it (a few ms to wake on the first map read after the idle time).

**Loader.** USB serial (the `Serial` CDC the status lines already use). `host/load_pack.py` sends frames `A5 5A | cmd | len | payload |
crc32` (zlib CRC of cmd, len, payload); the box answers `pk ok ...` / `pk err ...` lines. Commands: HELLO ("LOADPACK": enters load mode; the
reader returns false for everything while it lasts), ERASE (pack length: the header sector first, so the old pack is gone at once), WRITE
(offset + up to 512 bytes; the box programs, reads back and compares before it acknowledges; the same chunk twice is harmless), FINISH
(length + CRC: the box reads the stored bytes in 4 kB slices, compares, checks the pack's own BGMP header, only then writes the flash
header and reads it back), STATUS (re-checks header + whole-pack CRC), EXIT (reboot; also after 30 s of silence). Noise on the line
(status text, a terminal) cannot start a load: sync bytes + length + CRC are needed, and HELLO must carry "LOADPACK". Only the loop
task touches the flash and the serial port (no callbacks involved); the loop keeps running during a load (display, tracer link), only
the map is unreadable, and after the load the box restarts so terrain always starts from a flash that is a whole pack or blank.
Expected speed on the device (not measured): 512 B per round trip, about 4-8 ms each (USB frame, program 2 pages, read back) = 60-120 kB/s,
so 15-30 s for the pack, plus 4-8 s of erase. Measured 2026-10-18 (box image, Bulgaria pack): erase 3.0 s, write 5 kB/s, 352 s in all,
0 retries, whole-pack CRC match. About 100 ms per 512-byte round trip, 10-20x slower than expected: the loader is polled once per loop
pass and the loop sleeps between frames, so each chunk waits a loop tick. Fix later (pump the loader in a tight loop while a load runs, or
bigger chunks); it works, and loading is rare.
End-to-end test without the device: `firmware/emu/build.sh loadtest [pack]` runs `load_pack.py` against the same loader code over an
in-memory flash (clean, and with a bit error injected into every 50th frame); Bulgaria pack: 9 s through the PC pipe.

**Boot.** `PackService::begin()`: flash up, header read, `packHeaderConsistent` (the BGMP file size equals the header's length), reader
attached; the vario image opens `TerrainKit` right away (header CRC and the CRC of every section up to 64 kB: towns, roads, strings; the
DEM tiles are checked per tile on read), so a damaged pack gives no map and no AGL, never wrong values. The whole-pack CRC is NOT computed
at boot (about 0.3 s of CRC-32 at ~12 cycles/byte plus the flash reads): while a terminal is attached it runs in 8 kB slices per loop pass,
starting 3 s after boot, and shows as `crc=ok|BAD(ms)` in the status line. A flight with no terminal never pays for it.
The status line: `pack hdr=ok len=1724652 crc=ok(NNNms) jedec=c84015 quad=1 mhz=16 seq=NNNNKB/s rnd400=NNNus terrain=open loader=idle rd=N fail=N`;
`seq` = 64 kB read as 512-byte blocks at boot, `rnd400` = mean of 64 scattered 400-byte reads (what a coded tile costs). The diagnostics
image adds `packbench read_kBps=.. crc_ms=.. crc_ok=..` (the whole pack read in 1 kB blocks, and its CRC) to the first status block.
Measured 2026-10-18 on the board: jedec=c84015 (GD25Q16), quad=1, 16 MHz, seq=6939 kB/s, rnd400=96 us, whole-pack CRC ok in 3449 ms
(sliced over loop passes, so wall time, not CPU time), terrain=open, fail=0.

**RAM and size.** `TerrainKit` is the big object (map page cache, tile cache, vector buffers). Vario image: a plain static (the vario is the
start-up app and flight-critical). Box image: the kit lives in the shared app arena only while the vario runs (`ITerrainProvider` in
`vario_app.h`; `BoxTerrain` in `main_box.cpp`), the arena grows to the kit's size. The home screen / About read a copy of the pack's area
and credits taken once at boot (the pack cannot change without a reboot). Costs: the map page's cache and ground track restart on each
return to the vario, and the vario comes back on the compass page if it was left on the map page. The loader itself is about 2 kB static
(frame buffer 520 B, read-back 512 B, CRC buffer 256 B x2, QSPI staging 260 B, reply line 128 B).

| image | text before | text after | .heap before | .heap after |
|---|---|---|---|---|
| feather_sense | 279544 | 290760 | 142356 | 91796 |
| feather_sense_box | 315160 | 329560 | 97316 | 89588 |
| feather_sense_diag | 293768 | 305400 | 103996 | 52316 |

Loop stack: worst static chain 2696 B (vario) / 2712 B (box) of 4096.

## Build and test time (2026-10-19)

`scripts/check.sh` on this Mac (10 cores, `sysctl -n hw.ncpu`), wall clock:

| | before | after |
|---|---|---|
| clean (no `emu/out`, no `.pio/build`) | 10 min 49 s (127 % CPU) | 2 min 26 s (640 % CPU) |
| rerun, nothing changed | about 11 min (it recompiled everything every time) | 1 min 45 s |

Where the time went: `build.sh` compiled every binary from all sources in one `c++` call (serial), then `out/tests`
ran one test after another, and `check.sh` ran its stages one after the other. The tests are 620 CPU seconds with the
sanitizers on, 570 of them in `sim_playability` and `sim_day_settings` (scripted-pilot flights), so the floor on 10
cores is about a minute.

What changed:
- `firmware/emu/Makefile`: one object per source and flag set (`out/obj/{san,o2,o2n,ld}`), header dependencies
  (`-MMD`), `make -j<cores>` from `build.sh`. A source several binaries share is compiled once per flag set.
- `out/tests --list` / `--only NAME`: `build.sh` runs one process per test function, all cores, prints the outputs in
  list order and adds the counts (same total, 136885 checks; a test that dies counts as a failure and is named).
- `sim_tests.h` `forkEach`: the slow flights of `playability` and `day_settings` run one per forked process (8 at a
  time); the children's CHECK counts are added back.
- `check.sh`: the three pio images build at once, the emulator binaries are built by one `build.sh prebuild`, then
  tests, loadtest and smoke run side by side, each into its own file, printed in the old order with the old messages.
- The fuzz test's "worst tick < 50 ms" now measures thread CPU time, not wall time: under a loaded machine (the old
  `check.sh` run while other work ran made it fail once) a descheduled tick looked slow. The printed timing lines of
  other tests (map render, decode us/tile) are wall clock and read high while the machine is busy; read them from a
  quiet `build.sh simbench` / single `out/tests --only NAME` run.
- Checked: same check count, all 69 pgm / 87 png in `emu/out` (including the smoke run's `box_*.png`) are byte for byte
  the same as before.

## Sim speed pass on the device (2026-10-19, WIP, branch worktree-agent-ad3c1ea7a384b0273)

Method: SIM_BENCH build, `gm` line, DWT micro-benchmark (`platform/hw/mbench.h`). Findings: the CPU runs 64 MHz and the flash cache is on, so there is
no system-level slowdown; the cost was libm and byte loops. Cycles on the nRF52840 (newlib): expf 1300, logf 1260, powf 2540, tanf 230, atan2f 300,
fmaxf 58, fmodf 170, sinf 80, sqrtf 17; memset 6.3 cycles/byte, memcpy 8.1 (byte loops).

| Part (ms, avg) | before | after |
| map: fps | 7-11 | 50 (tick max 16) |
| map: step (3 substeps) | 16-31 | 1.7 |
| map: draw | 25.7 | 13.3 (blit 7.4 -> 2.3, assist rings ~6, symbol 2.9, hud 2.5) |
| 3D: fps | 8-9 | 31 |
| 3D: draw | 38-40 | 22 (voxel 20 -> 11, sky 8 -> 3-4, sprite 6.7 -> 4.6, hud 3.9 -> 2.5) |
| frame clear | 1.1-1.6 | 0.1 |
| air sample per substep | 5 | 0.5 |

Done: fexp/fpow (sim_math.h), per-cell cached thermals, fastmem word helpers (clear, pack, snapshot, diff, blit), blit by words, catch-up cap 2,
draw overlaps the panel DMA, north-up map (no re-render on turns; 4 px stale threshold), quad-filled glider sprite, voxel 4 columns per ray / 16 bilinear
steps, integer HUD numbers. Left: 3D still ~31 fps (voxel 11, sprite 4.6, sky 3-4, hud 2.5 need about 7 ms more), map rings (30 per frame, 6 ms),
glider symbol 2.9 ms, HUD windBox 1 ms, `build.sh simbench` not recalibrated, BLE not stopped in the sim, scripts/check.sh not run on the final state
(emulator build.sh passed: 137010 checks, 0 failures).

## CPU options pass (2026-10-19) — no board on USB, so no device timing

The task was to measure compiler options, RAM functions and DSP intrinsics on the device. No board was attached (`/dev/cu.usbmodem*` absent),
so only build-side numbers exist; nothing that needs a timing to justify itself was kept.

| Step (feather_sense_box) | flash | `bl sqrtf` | loop stack | heap | verdict |
| baseline -O2 | 403776 | 47 | 2628 B | 82480 | |
| `-fno-math-errno` | 401840 | 1 | 2628 B | 82480 | KEPT (pure win: sqrtf inlines to vsqrt, NaN still propagates; semantics same except errno, unused) |
| `-flto` (whole image) | link error | - | - | - | DROPPED: not just QspiFlash; with the Arduino core / FreeRTOS / Adafruit_GFX archives nearly every symbol is undefined (needs ar plugin / lib flags work) |

Device (SIM_BENCH, avg of ~20 s of `gm` lines; map view is capped at 50 fps by the sim, 3D is the one that moves):

| Step | map fps / draw ms | 3D fps | 3D draw | voxel | sky | sprite | flash | heap |
| baseline (-O2, -fno-math-errno) | 49.9 / 12.2 | 36.9 | 21.7 | 11.7 | 3.4 | 4.3 | 401840 | 82480 |
| + per-file -O3 (`platform/util/hot.h` `HOT_FILE_O3`: voxel, sky3d, chase, glider, topdown, sim_hud, bitmap1, shapes, gray, fastmem, sharp_frame, linediff) | 49.9 / 11.8 | 40.7 | 19.6 | 9.8 | 3.5 | 4.0 | 410368 | 82480 |
| + voxel render/fillSpan/sky and fastmem in RAM (`.data.ramfn`, long_call) | 49.8 / 11.9 | 30.2 | 26.5 | 16.7 | 3.4 | 4.0 | 410244 | 80368 |

Kept: -O3 for those files (+8.5 kB flash, 3D 37 -> 41 fps, voxel -16%). Dropped: code in RAM; the flash cache already gives zero-wait fetches, RAM fetches
contend with the data loads on the same bus (voxel +70% slower, fastmem no change in blit 2.3 ms). Not done: DSP intrinsics. The voxel inner loop is float
(single-cycle FPU MACs); an integer SMLAD bilinear would not be bit-identical with the float result and so would change the renders; the other hot
loops (fastmem, pack, diff) are already word-wise memory-bound. Not worth a second code path. Note -O3 allows different FMA contraction than -O2 on the
device; renders on the PC are unaffected (the pragma is ARM only).

## Float map maths and integer number text (2026-10-19) — PC only, no device timing

Soft-double call sites (`bl __aeabi_d*`, `__extendsfdf2` ... in `arm-none-eabi-objdump -d` of `feather_sense_box`; static
counts, not executed counts; whole image 1199 -> 1067):

| function | before | after |
|---|---|---|
| `MapVectors::makeXform` | 49 | 9 |
| `MapVectors::boxForWindow` | 50 | 9 |
| `MapVectors::latLonToUnit` (per track point, per frame) | 14 | not called any more (`latLonToUnitE7`, 0) |
| `TerrainMap::update` (every frame) | 19 | 11 |
| `TerrainMap::windowCentre` (every frame) | 14 | 6 |
| `TerrainMap::project` (pilot, per frame) | 2 + 14 inside | 6, rounds to 1e-7 deg then float |
| ground track (up to 240 points x 2 passes per frame) | `trackAt` + `project` = about 20 calls per point | `projectTrack`: 0 |
| `Terrain::split` / `elevationE7` | 4 / 8 | 0 / 0 (float divide is exact, see below) |
| `sim::TerrainSource::at` (per height sample) | `elevation(double)` | `elevationE7`, offsets in float |

Findings:
- The per-pixel relief loop and the road / town vertex loops were already float. The double work was per redraw
  (about 100 calls, nothing next to 96000 pixels) and, the real hit, the **ground track on the map page**: every frame, up
  to 240 points x 2 passes, each through `trackAt` + `latLonToUnit` (about 20 soft-double calls per point, thousands per frame).
  It now goes straight from the stored e7 integers: `origin - point` is an exact integer difference, then float scales.
- `Terrain::split`: `(float)((double)rem / (double)den2)` equals `(float)rem / (float)den2` when both fit in 24 bits
  (a double has more than 2 x 24 + 2 bits, so double rounding cannot differ). `parseLayer` now rejects cell sizes
  above 2^20 e7 units so this always holds. Relief output identical.
- Kept double: the absolute lat/lon sum (`windowCentre`, `update`, window centre minus origin in `makeXform` /
  `boxForWindow`, `toE7` for the public `elevation(double)` / `ceiling(double)` and `project(double)`). `relief()` still has 10
  setup sites per call (centre to e7, `cosf` argument); left alone so its output stays bit-identical.
- Verification: all `emu/out/*.pgm` and `out/sim/*.pgm` byte-identical to main (map renders included, 0 differing
  pixels), replay digest of `host/xct_capture.log` (`--hz 3 --inject --thermal`) unchanged `1db49efae1a77476`, `box_vario_matches_alone` passes.
  New test `test_map_float_maths`: unit error < 0.003 units (3 cm), pixel error < 0.004 px against the old double formulas.
- Number text: `fmtFixed()` (`platform/util/numfmt.h`) formats `%.Nf` / `%+.Nf` (N 0..3) with integer maths on the exact
  binary value (ties to even, "-0" kept, truncation like snprintf; |v| >= 1e9 falls back to snprintf). `fnum()` parses its
  format and uses it; two remaining `snprintf("%.1f")` calls in the map page now go through `fnum()` too (a non-finite value
  there now prints "--" instead of "nan"). `test_numfmt` compares 1.5 million strings with snprintf (ties, every
  float of seven binades, random bit patterns, plausible readings): 0 differences.
- Item 3 (libm in wind / thermal): skipped. There is no `expf` / `powf` / `logf` in the flight wind or thermal code;
  those are in `core/sim.cpp` (the simulator tracer, not flight). What is left is one `atan2` (double) per sample for the wind
  direction, bearing to home and thermal headings, about 3 per second: a fast `atan2` would change the digests for no
  measurable gain.

## Map page: no more 1.9 s stall, sun-lit hillshade, paper-plane pilot (2026-10-19)

- **Problem (measured on the box image, `ctl.py sim on` + `page map`, `st` lines)**: a normal loop pass ~18 ms, but every 30-60 s
  one pass took ~1.9 s: `TerrainMap::update` rendered the whole picture (hillshade + contours + roads + towns) in one call. The
  float-maths change made no difference (18.7 -> 18.1 ms).
- **Fix**: the render is a job. `update()` (per frame) only decides and starts it; `App::tick` calls `TerrainMap::step()`, which
  works a time budget (5 ms, `fastMicros` set by main) per loop pass into a second 1-bit layer: one row of one relief strip per
  work unit (`Terrain::reliefStrip`, which keeps the three height rows between calls, so a strip costs one pass, not one per
  call; bit-identical to `relief()`, tested), then one road polyline (two passes), then one town. The old layer keeps being
  shown, the pilot and the track keep their true position relative to it; when the job ends the layers swap. The new picture is
  centred where the pilot was when the job started. RAM: +10.2 kB for the second layer, -10.4 kB for the old 24-row bands (a
  strip row is 64 px now): the box image heap is unchanged (82.4 kB). A pilot more than 95 px from the window's centre (long
  pause on another page) drops the picture (the screen says "reading the pack") instead of showing a wrong one.
- **Measured on the device (box image, sim, map page)**: normal tick 18.1 ms (unchanged), 18.1 with the new pilot symbol.
  Before: one tick of ~1900 ms every 30-60 s. After: worst tick while a job runs 24-25 ms (step max 9.4-9.6 ms for a 5 ms budget;
  the overshoot is one unit: a tile decode or a road block), no tick above ~25 ms from the map in 75 s + 130 s of reading
  (an isolated ~32 ms tick about every 50 s is not the map: it also shows with no job running). A picture takes ~330 steps, 8 s
  of wall clock at 5 ms per pass; at 100 km/h (0.28 px/s at 100 m/px) the pilot moves ~2 px in that time, the re-render
  threshold is 10 px and the window's half size 109 px. Heading-up while circling would still lag (a job per 5 degrees takes 8 s);
  north up is the default. The very first tick after boot is 115 ms (not the map). With the 8 ms budget the first try had
  ticks of 32-52 ms.
- **Sun** (`platform/sensors/sun.*`, `ReliefStyle::setSun`): azimuth/elevation from the tracer's UTC date + time (the `XCTRC` line has
  the date; `Sample::year/month/day`, 0 = unknown) and the position, Astronomical Almanac low-precision formulas, 0.83 ms
  on the device, recomputed every 30 s. The light vector goes into the relief's screen axes (north up: sun in the south =
  light from the bottom of the screen; the sun keeps its side of the world when the window turns). Sun below 2 degrees,
  or no date: the old fixed light from the upper left (picture byte-identical to before). A new picture is built when the
  sun moved more than 5 degrees (azimuth or elevation): about every 20 minutes. Sun shading uses a darker "darkest" level
  (6, was 10) so the shaded side reads; at midday the effect is faint (high sun), in the evening the slopes facing away are dark
  (see `out/map.png`, `map_evening.png` (16:40 UTC), `map_night.png` (fixed light)). The built-in sim flies on a fixed date
  (2026-10-19) from 12:00 UTC.
- **Railways**: the pack has only four road classes (motorway, trunk, primary, secondary); there is no rail layer. Drawing now
  skips any class above 3 anyway. What looked like train lines may be the secondary roads (1 px core in a 3 px white casing).
- **Pilot symbol**: bold paper plane seen from above (nose along the track, 25 px nose to 16 px tail, 36 px wide), two faces (left lit
  grey, right shaded black) in a black outline with a 3.5 px white halo; replaces the arrow in a circle. Drawn per pixel with
  incremental edge distances (first version with per-pixel multiplications cost ~6 ms per frame; now none measurable).

## Sim leftovers pass (2026-10-19, device, SIM_BENCH image, x3 time, box over the Sopot map pack)

| item | before | now |
|---|---|---|
| 3D frame rate | 31 fps (before the -O3 / fastmem work) | 43.7-46 fps (draw 19.6 ms) |
| voxel | 9.8-11 ms | 9-11.7 ms (shade only where the sample shows; no gain on this scene) |
| sprite (glider, 3D) | 4.6 ms | 2.6 ms (`lineGrown`: one vline per column instead of (2g+1)^2 lines) |
| glider symbol (map) | 2.9 ms | 0.7 ms |
| sky | 3-4 ms | 2.6-4 ms |
| HUD | 2.5 ms | 2.8 ms (two more boxes: time and distance; snprintf replaced by digit code) |
| map view | 30 fps with rings | 50.0 fps, draw 10-11 ms |
| flight restart after landing | 2.7 s stall (693 tile decodes) | 90-275 ms, 13 decodes in startFlight, the rest warms up over the next frames |

BLE (laptop advertising and link) is off while a game runs and on again when leaving it; the vario and tools keep it.
The map picture has an 8 px margin on every side, so a 4 px stale threshold plus the render time never shows a blank strip.

## Start-up animation (2026-10-19)

`apps/intro/intro_app.*`: 140 steps of 20 ms (2.8 s) at 50 fps in the host's game mode, an arena app (`LazyApp`) destroyed before the
start-up app is built. Reuses World (only its height cache and air model, never stepped), VoxelView, SkyFx and ChaseCam through their
public APIs; no sim file was changed. Scripted: the glider circles a thermal column (radius 16 m at 12 m/s, 41 degrees of bank, a turn
every 8.4 s: faster than a real one so the camera sweeps), climbs 190 m in 2.3 s drifting toward the cloud's centre; from 0.8 s a scripted
cloud front (6 large outlined puffs, shaded underside; was 9 small ones, 31.8 fps on the device) sweeps down over the picture (the sim's own cloud sprites stay where the thermal put them
and never fill the view); from 1.45 s the dithered white-out (ordered dither, ragged front from the top, whole bytes); the name (just "stavl.com" in FreeSansBold18pt, centred) dissolves in through the same dither from 2.15 s and holds. Without a pack, or when no sim takeoff is covered: 2D
contour layers under swelling clouds, the same white-out and name.
- **Scene choice** (deterministic): Kalofer if the pack covers it, else the first covered takeoff in the table's order (Sopot on the test
  fixture); sim day seed 13 (cloud base 2206 m, medium day); the thermal with a grown cumulus (>= 0.9) scored by strength and trigger height
  (relief under the cloud). Seeds 1-13 were compared; Kalofer on the Bulgaria pack gives about 500 m above a high ridge.
- **Height cache fill**: begin() fills the 128 x 128 window through `WindowSource`: real terrain within 6 km of the takeoff, a flat skirt
  300 m below the takeoff beyond (no pack access); 4 pack tiles stay decoded (the sim's 2 thrash on a fill). Fill and thermal search are
  timed with the device clock and printed in the intro's "gm" line (`begin_us`, `fill_us`, `search_us`). NOT measured on the device
  (it was not available to this run); the sim's whole-window fill was estimated at 0.1-0.3 s.
- **Expected frame time** from the sim device numbers (voxel ~10 ms, sky 2.6-4, sprite 2.6, no HUD; white-out, cloud front and name about
  1 ms; clear 0.1): about 16-18 ms against the 20 ms step. The host's catch-up keeps the animation at 2.8 s in real time if a frame is late.
- **Added time to the first vario frame**: the animation's length (2.8 s) plus the fill; nothing when skipped or switched off. The tracer
  scan starts in setup() as before. A switch press skips on its release, the D-pad / A / B keys on their down edge.
- **Memory**: the intro object is smaller than SimBox (a test checks it), so the arena and the heap (78.5 kB on the box image) are unchanged.
  The 32 candidate thermals are a member, not a local (the stack check flagged 2 kB in setup).
- Tests `box_intro`: length in real time, skip by press / keys / 3 s hold (never the home screen), arena freed and the tracer rule, off and
  the clock-sleep path, the Settings row, the 2D version, the 3D version deterministic on the Sopot fixture, end frame mostly white.
  Contact sheets: `emu/out/intro_sheet.png` (3D, study pack if present else the fixture) and `intro_flat_sheet.png`.
