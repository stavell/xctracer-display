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
  `toDeviceMag` in `src/hw/compass_lis.cpp` are still identity. `host/orient_log.py` records a session and
  `host/orient_analyze.py` solves both mappings (self-tested on synthetic data) and prints the code to paste.
  First reading from the unit: accelerometer gravity along -z at rest in its current pose.

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
