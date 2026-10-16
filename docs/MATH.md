# The maths inside the display

This is a guide to the calculations in the firmware, written for a pilot or developer who wants to know
what a number on the screen means and how far to trust it. Everything here is taken from the code; the
file that holds each piece is named in its section. Where `NOTES.md` has a measurement that explains a
choice, it is quoted as a measurement, not as a promise.

Conventions used throughout: positions are WGS84 degrees (double), distances metres, speeds m/s inside
the maths and km/h on the wire and on screen, angles degrees with 0 = north, clockwise. A wind vector
is stored as the direction it blows **toward**; the screen shows the direction it comes **from**
(`toDeg + 180`).

Contents: [Input](#1-the-xctrc-line) / [Flight model](#2-flight-model) / [Wind, circle fit](#3-wind-from-circling-circle-fit) /
[Wind, Kalman filter](#4-wind-kalman-filter) / [Wind and temperature by altitude](#5-wind-and-temperature-by-altitude) /
[Thermals](#6-thermal-memory-and-the-air-mass-map) / [Home](#7-home) / [Compass](#8-compass) /
[Heading choice and smoothing](#9-heading-source-and-smoothing) / [Rendering](#10-rendering) / [Numerics](#11-numerics)

## 1. The `$XCTRC` line

`firmware/src/core/xctrc.cpp`

```
$XCTRC,Y,M,D,h,m,s,cs,lat,lon,alt,speed,course,vario,,,,pressure,battery%*CS
```

* **Checksum**: XOR of all bytes between `$` and the last `*`, compared with the two hex digits after it.
  A bad checksum rejects the line. Lines of 160 characters or more are rejected.
* **Fields**: at least 19 comma-separated fields (it splits at most 24), the first must be `$XCTRC`.
* **Time**: fields 4, 5, 6 (h, m, s) must be whole integers in 0..23, 0..59, 0..60, otherwise the whole
  line is rejected. The date fields are not used.
* **Ranges**: each value outside its range becomes NaN ("missing"), the rest of the line is kept.

| Value | Field | Accepted range | Notes |
|---|---|---|---|
| lat, lon | 8, 9 | $\lvert lat\rvert\le 90$, $\lvert lon\rvert\le 180$, finite | |
| altitude | 10 | -1000 .. 30000 | metres |
| speed | 11 | 0 .. 400 | **assumed km/h, not verified outdoors** (`NOTES.md`) |
| course | 12 | -720 .. 720 | normalised to 0..360 |
| vario | 13 | -50 .. 50 | m/s |
| pressure | 17 | 100 .. 1200 | |
| battery | 18 | integer 0..100 | else -1 (unknown) |

**Fix detection.** The tracer sends `lat = lon = 0` when it has no GPS fix. The code sets
`fix = finite && on the globe && !(lat == 0 && lon == 0)`. Without a fix, position is stored as 0,
and speed and course are forced to NaN even if the line carried numbers. Altitude and vario (from the
barometer) stay usable without a fix.

**Staleness.** `App::hasData` (`firmware/src/app/app.cpp`) is true only while the last valid line arrived
less than `freshMs` = 3000 ms ago by the box's own clock. Stale data is shown as no data and is not fed
to the heading selector as GPS. This is the only place the wall clock matters; all flight maths below
use the data's time stamps.

## 2. Flight model

`firmware/src/core/flight.cpp`

**Flight clock.** Each line has a time of day $t = 3600h + 60m + s$. A line with the same $t$ as the
previous one is not a new data second and is ignored by the history (so at 3 lines/s one point per second
is kept). The flight clock advances by $\Delta t=(t-t_{prev}) \bmod 86400$ seconds, **clamped to 5 s**,
which handles midnight and data gaps. Replays at any speed therefore behave like real time. Takeoff,
landing, circling, thermal age and wind age are all measured on this clock.

**History.** One `TrackPoint` per data second, 180 kept (3 minutes).

**Average vertical speed.** Mean of the vario of the last 20 history points (`AVG_S`), skipping NaN.
It is a mean over 20 *points*, not 20 seconds, so a data gap makes the window longer in time. Zero if
there are no valid values.

**Glide ratio.** Over the last up to 20 points that have a fix and finite vario and speed
(at least 5 of them, else NaN):

$$GR = \frac{\bar v_{ground}}{-\bar w_z},\qquad \bar v_{ground}=\text{mean speed in m/s},\ \bar w_z=\text{mean vario}$$

It is NaN (`--` on screen) with fewer than 5 usable points, and whenever the mean vario is above
$-0.2$ m/s (not sinking, so the ratio is meaningless or infinite). It is capped at 99. It is ground
glide, not air glide: wind is in it. The vario comes straight from the tracer, not from the 20-point
average above.

**Takeoff and landing.** From GPS speed (0 without a fix). Flying starts when speed has been above
10 km/h for more than 5 s (flight time counts from the start of that fast run), and stops when it has
been at or below 10 km/h for more than 60 s. `flightSec` is the flight clock since the start while flying.

**Circling flag.** True when the mean absolute change of course over the last 8 s is above 6 deg/s and
speed is above 8 km/h; it drops 16 s after the last such moment. (The flag is computed but no screen reads it.)

## 3. Wind from circling (circle fit)

`firmware/src/core/wind.cpp`, called from `FlightModel::updateWind`.

While turning at constant airspeed $V$ in wind $\mathbf w$, the ground velocity is $\mathbf g = \mathbf w + V(\sin\theta,\cos\theta)$.
The ground-velocity vectors therefore lie on a circle whose **centre is the wind** and whose **radius is the
airspeed**.

**Data.** Each second the last 30 points (`WIND_S`) are used. Each point gives
$\mathbf g_i=(v_i\sin c_i,\ v_i\cos c_i)$ from speed $v_i$ (m/s) and course $c_i$. If any point in the
window has no fix, a non-finite value, or a speed under 8 km/h, the window is rejected.

**Fit.** The Kasa (algebraic) least-squares circle: minimise $\sum_i\left(|\mathbf g_i|^2 - 2ax_i - 2by_i - c\right)^2$
over $(a, b, c)$, with $(a,b)$ the centre and $c = r^2 - a^2 - b^2$. This is linear; the 3x3 normal equations

$$\begin{pmatrix}\sum x^2&\sum xy&\sum x\\ \sum xy&\sum y^2&\sum y\\ \sum x&\sum y&n\end{pmatrix}
\begin{pmatrix}2a\\2b\\c\end{pmatrix}=
\begin{pmatrix}\sum x z\\ \sum y z\\ \sum z\end{pmatrix},\quad z=x^2+y^2$$

are solved with Cramer's rule in double precision. Then $r=\sqrt{c+a^2+b^2}$, the residual is
$\mathrm{rms}=\sqrt{\frac1n\sum(|\mathbf g_i-(a,b)|-r)^2}$, and the wind is $(a,b)$ (east, north).

**Acceptance gates** (all must hold):

| Gate | Value |
|---|---|
| Window length | 30 points (function accepts 15..64) |
| Speed of every point | at least 8 km/h |
| Turning at the start | at least 4 of the first 5 one-second course changes are $\ge 5^\circ$ |
| Turning at the end | at least 4 of the last 5 course changes are $\ge 5^\circ$ |
| Heading coverage | $360^\circ -$ largest gap between the sorted courses $\ge 200^\circ$ |
| Determinant | $\lvert D\rvert \ge 10^{-6}$, and $r^2>0$ |
| Fitted airspeed $r$ | 14 .. 72 km/h |
| Residual | $\mathrm{rms}\le 0.2\,r$ |
| Wind speed | at most 72 km/h |

Why the start/end gate: a window that begins or ends in a straight glide mixes in vectors flown at a
different airspeed and bent the estimate by about 1 km/h; with the gate it stayed within 0.1 km/h in the
test (`NOTES.md`). Why a loose residual: real fits have a typical residual of about 7.5 % of airspeed,
so a tighter gate would throw away good circles. On a real 3 h flight the fitted airspeed was 32-38 km/h
in nearly every thermal.

**Smoothing.** An accepted fit is blended: $\mathbf w \leftarrow 0.88\,\mathbf w + 0.12\,\mathbf w_{fit}$
(about 8 s time constant). If the last accepted fit is more than 120 s old, the estimate restarts from
the new fit. The reported age is the flight seconds since the last accepted fit.

## 4. Wind Kalman filter

`firmware/src/core/windekf.cpp`

The circle fit needs a full turn. The extended Kalman filter (EKF) learns from ordinary heading changes.

* **State**: $\mathbf x=(w_E,\,w_N,\,V_a)$: wind east and north (m/s) and airspeed (m/s).
* **Measurement**: a pseudo-measurement of zero each data second: $0 = h(\mathbf x) = \lVert\mathbf g-\mathbf w\rVert - V_a$.
  With $\mathbf d=\mathbf g-\mathbf w$ and $r=\lVert\mathbf d\rVert$, the Jacobian is
  $H=(-d_x/r,\ -d_y/r,\ -1)$ and the innovation is $y=-(r-V_a)$.
* **Predict**: the state is constant; only the covariance grows, by $Q\,\Delta t$ per step.
* **Update**: standard Kalman gain $K = PH^T/S$, $S=HPH^T+R$, then $P \leftarrow P-K\,HP$, symmetrised and
  floored at $10^{-6}$ on the diagonal.
* **Observability**: with constant airspeed this is only informative once the heading has changed. In
  straight flight the covariance stays large and the filter reports not confident.

| Constant | Value |
|---|---|
| Wind process noise $Q_W$ | 0.0004 (m/s)$^2$ per second |
| Airspeed process noise $Q_{V_a}$ | 0.01 (m/s)$^2$ per second |
| Measurement noise $R$ | 1.0 |
| Initial state | wind 0, 0 and $V_a$ = 10 m/s |
| Initial covariance | diag(25, 25, 9) |
| Step skipped if | no fix, non-finite input, speed < 3 m/s, or $r<0.5$ m/s |
| Outlier gate | skip the update if $y^2 > 9S$ (3 sigma) |
| Clamps | $V_a$ in 5..25 m/s, wind magnitude at most 25 m/s |
| Time step | $\Delta t$ clamped to 1..5 s |

**Confidence**: $\sigma_W=\sqrt{P_{00}+P_{11}}$, confident when $\sigma_W<0.8$ m/s.

**Which wind is used.** `FlightModel::wind()` and the internal `windXY()` return the EKF wind when it is
confident, otherwise the smoothed circle-fit wind. This one vector feeds the wind text and arrow, the
home calculation, the air-mass map and the thermal drift. When the EKF is the source, the reported age
is 0. The wind profile (next section) deliberately uses the circle fit only, because the EKF adapts over
minutes on purpose and would smear how the wind changes with height.

## 5. Wind and temperature by altitude

`firmware/src/core/windprofile.cpp`, page drawn in `firmware/src/view/screens.cpp`.

* 60 bands of 100 m, from 0 to 6000 m. Below 0 goes to band 0, above the top to band 59.
* **Wind**: one entry each time a circle fit is accepted in that second, stored at the current altitude
  as east and north components. Each band is an exponential average of the components with
  $\alpha = 0.25$: $\mathbf w_b\leftarrow\mathbf w_b+0.25(\mathbf w-\mathbf w_b)$. The first sample sets it.
  Speed and direction are computed from the averaged components.
* **Temperature**: the box's own thermometer; once per data second, if the reading is less than 5 s old,
  it is averaged into the band at the current altitude with the same $\alpha = 0.25$.
* **Expiry**: a band whose last update is more than 1 hour old is dropped from queries and restarts from
  its next sample. On the page, a wind value older than 20 minutes is shown in parentheses.

**Lapse rate.** Least-squares slope of temperature on altitude over the bands that have a temperature,
in degC per 100 m:

$$\Gamma = 100\,\frac{\sum(z_i-\bar z)(T_i-\bar T)}{\sum(z_i-\bar z)^2}$$

(centred sums, to keep float precision at 6000 m). NaN with fewer than 3 such bands or less than 200 m
between the lowest and highest.

**Inversion.** Band $i$ is marked as an inversion when it has a temperature, the band exactly 100 m below
is in the list with a temperature, and $T_i - T_{i-1} > 0.2$ degC (warmer air above cooler air). The page
shades such bands grey and draws the temperature trace thick across them.

## 6. Thermal memory and the air-mass map

`firmware/src/core/thermal.cpp`, `FlightModel::airTrail` / `updateThermals` in `flight.cpp`.

**Air-mass frame.** A thermal is stationary in the air, not over the ground. To undo the drift, a point
flown $a$ seconds ago is moved downwind by $a$ seconds of wind:

$$x = (\lambda-\lambda_0)\,k_x + w_E\,a,\qquad y = (\varphi-\varphi_0)\,k_y + w_N\,a$$

with $(\varphi_0,\lambda_0)$ the current position, $k_y=111320$ m/deg and $k_x=111320\cos\varphi_0$. So the
trail is relative to the pilot, in metres east/north, with the wind taken out. Circling in a steady
thermal then draws a closed ring that does not smear (in a unit test: 0.0 m radius spread compensated
versus 32 m for the ground track, `NOTES.md`). The wind here is the one from section 4. The same
function records, per point, the course change in deg/s (`turn`).

**Finding a thermal in the trail** (`findThermal`, runs every second on the 3-minute air trail):

| Rule | Value |
|---|---|
| Climbing point | vario $\ge 0.5$ m/s |
| Episode | run of climbing points, gaps of up to 6 s bridged |
| Minimum length | 8 points |
| Minimum gain | 15 m, where gain = sum of positive vario values (1 s each) |
| Choice | among episodes with at least 60 % of the best gain, the most recent |
| Centre | vario-weighted centroid of the episode's points |
| Circling part only | if at least 8 points in the episode turn at $\ge 5$ deg/s, only those points are used |
| Average climb | gain / episode length in seconds |

Using only the turning points fixed a bias from the straight climb on the way in; in a simulated drifting
thermal the marker was within 4 m of the truth 40 s after leaving it (`NOTES.md`).

**Memory.** Up to 6 marks. A mark stores a ground position valid at time `atMs` plus its climb and gain.
Positions are converted back to the pilot-relative frame each draw by adding $\mathbf w\,(t_{now}-t_{at})$,
so a remembered thermal drifts with the wind after you leave it.

* A new detection within 50 m of a mark (after drifting the mark to now) is the same thermal: it replaces
  the position if its gain is at least 0.9 of the stored gain, otherwise the old position is only moved
  with the wind. The stored gain only ever increases.
* A full table replaces the mark whose climb ended longest ago.
* **Expiry**: a mark disappears 15 minutes (`LIFETIME_MS`) after its climb ended.
* Drawing order is newest first.

**Map drawing** (`drawThermalMap` in `screens.cpp`). Heading-up: the map is rotated by the direction of
the last ~3 s of the air-frame trail (if it moved more than 3 m; otherwise the GPS course). The scale
snaps to fixed steps of 2.2, 1.6, 1.1, 0.7, 0.45, 0.28, 0.18, 0.11 px/m, the largest that fits the last
30 s of track (and the newest thermal when it is within 800 m), so the picture does not breathe.

## 7. Home

`firmware/src/core/home.cpp`

**Which point.** The first fix is a provisional home. When takeoff is detected (section 2) the current
position and altitude replace it, once; later slow flying does not move it.

**Distance and bearing.** A flat-earth (equirectangular) approximation, not haversine. With home
$(\varphi_h,\lambda_h)$ and the pilot $(\varphi,\lambda)$:

$$\Delta y=(\varphi_h-\varphi)\cdot111320,\qquad \Delta x=(\lambda_h-\lambda)\cdot111320\cdot\cos\!\tfrac{\varphi+\varphi_h}{2}$$

$$d=\sqrt{\Delta x^2+\Delta y^2},\qquad \beta=\operatorname{atan2}(\Delta x,\Delta y)\bmod 360^\circ$$

The relative bearing (arrow on screen) is $\beta-\text{heading}$ wrapped to -180..180. No home distance is
shown when there is no current fix.

**Required glide.** $GR_{req}=d/h$ with $h$ = altitude above the home altitude, only when $h>1$ m. It is
a still-air number.

**Arrival height.** Assumes a glider at trim: airspeed $V_a$ = 10 m/s (36 km/h) and sink 1.1 m/s
(`HOME_AIRSPEED_MS`, `HOME_SINK_MS`; a typical glider, the real one differs). With the wind split along
and across the line home ($along>0$ is tailwind):

$$g_s=\sqrt{V_a^2-cross^2}+along,\qquad \text{arrival}=h-\text{sink}\cdot\frac{d}{g_s}$$

(crab angle is solved exactly: the air velocity is chosen so that the cross component is cancelled).
If $cross\ge V_a$ or $g_s\le 0.5$ m/s the wind leaves no progress, `reachable` is false and no arrival
height is given. The wind used is `FlightModel::wind()`; with no wind estimate, still air.

## 8. Compass

`firmware/src/core/compass.cpp`. Pure maths; the hardware layer maps the sensor axes to the device frame
(x right of the screen, y top of the screen, z out of the screen).

**Smoothing before the maths.** Each reading is blended into a running vector,
$\mathbf a\leftarrow\mathbf a+0.5(\mathbf a_{new}-\mathbf a)$ for the accelerometer and
$\mathbf m\leftarrow\mathbf m+0.4(\mathbf m_{new}-\mathbf m)$ for the (calibrated) magnetometer.

**Validity.** No heading while the accelerometer magnitude $g$ is outside 6..14 m/s$^2$ (free fall or hard
acceleration, so gravity is not a good "down") or the field is below 5 uT.

**Tilt compensation.** The accelerometer at rest reports $+g$ along the axis pointing up, so
$\mathbf u=\mathbf a/\lvert\mathbf a\rvert$ is "up". Then

$$\mathbf e=\mathbf m\times\mathbf u\ (\text{east}),\qquad \mathbf n=\mathbf u\times\mathbf e\ (\text{north}),\qquad
\text{heading}=\operatorname{atan2}(e_y,\ n_y)\bmod360^\circ$$

Only the y components are needed because the heading is the direction the **top of the screen** (device
y axis) points to. This removes pitch and roll without separate angle estimation.

**Hard-iron calibration.** Metal and magnets on the board add a constant offset $\mathbf c$ to the field.
The undisturbed field has constant length, so the readings should lie on a sphere centred at $\mathbf c$:
$\lvert\mathbf m-\mathbf c\rvert^2=r^2$. Expanding gives something linear:

$$2\,\mathbf c\cdot\mathbf m+(r^2-\lvert\mathbf c\rvert^2)=\lvert\mathbf m\rvert^2$$

During calibration each raw reading adds the row $(2m_x,2m_y,2m_z,1)$ with right-hand side
$\lvert\mathbf m\rvert^2$ to a 4x4 set of normal equations, solved at the end by Gaussian elimination with
partial pivoting (double precision). Then $r=\sqrt{x_3+\lvert\mathbf c\rvert^2}$. The result is kept only if:

| Check | Value |
|---|---|
| Samples | at least 100 |
| Sum of the three per-axis spans (max - min) | at least 30 uT |
| Pivot | non-singular (> $10^{-9}$) |
| Fitted radius | 10 .. 100 uT (Earth's field is roughly 25-65 uT) |

Otherwise the old calibration is kept. The sphere fit is used instead of min/max because it tolerates
incomplete coverage better. There is no soft-iron (ellipsoid) correction and no declination: the heading
is **magnetic** north (`README.md`, `NOTES.md`).

## 9. Heading source and smoothing

**Selector** (`firmware/src/core/heading.cpp`, thresholds from `AppConfig` in `app.h`). GPS track is
used once ground speed reaches **4 km/h** and is released only when it falls below **2.5 km/h**; between the
two it keeps its current state (hysteresis, so it does not flap at walking speed). GPS counts only when the
data is fresh (section 1), has a fix, and speed and course are finite. Otherwise the magnetic heading is
used if valid, otherwise there is no heading and the dial shows `--`. The screen labels the source `TRK` or
`MAG`.

**Smoother** (`firmware/src/core/heading_smoother.cpp`). The dial shows a glided value, advanced once per
UI tick using the UI clock (this is display animation, not flight data). With target $\psi_t$, shown value
$\psi$, shortest signed difference $\delta\in(-180,180]$ and elapsed time $\Delta t$:

$$\psi\leftarrow\psi+\delta\,\bigl(1-e^{-\Delta t/\tau}\bigr),\qquad \tau=250\ \text{ms}$$

The exponential form makes the result independent of the frame rate, and the shortest arc means 350 to 10
degrees turns through 0, not the long way. It **snaps** to the target (no animation) when: the shown value is
not yet known (first value or after NaN), more than 2000 ms have passed since the last update, or the
remaining difference is under 0.5 deg. A non-finite target resets it. While it has not settled the app
redraws every 50 ms.

## 10. Rendering

Grey levels, partial refresh and pre-rotated glyphs exist because this chip is slow at per-pixel work
(Adafruit GFX cost 1.46 us per pixel; a full thermal frame took 103 ms before the fast path and 22 ms
after, `NOTES.md`).

**Ordered dither** (`firmware/src/gfx/gray.h`, `gray.cpp`). The panel is 1 bit per pixel. Grey is made
with an 8x8 Bayer matrix. The threshold $T(x,y)\in[0,63]$ is built by bit interleaving of $x\oplus y$ and $y$
(`bayer8`). A grey level $L\in[0,16]$ (0 black, 16 white, 17 steps) lights a pixel when $T(x,y)<4L$, so
level 8 lights exactly 32 of every aligned 64 pixels. The threshold uses **screen** coordinates, not the
shape's origin, so touching grey areas have no seam and a shape moved by one pixel does not shimmer.
Grey sprites store 4 bits per pixel; nibble $n$ becomes level $\lfloor(16n+7)/15\rfloor$. The views use two
shades: LIGHT = level 13 and MID = level 8 (`screens.cpp`).

**Partial refresh** (`firmware/src/core/linediff.cpp`, `firmware/src/hw/panel_sharp.cpp`). The frame is 240
native lines of 50 bytes. Each present compares every line with the snapshot of what was last sent
(`memcmp`) and sends only the changed lines; each line is sent with its 1-based address. If more than 200
lines changed (or the panel was marked dirty) the whole frame is sent, and if nothing changed nothing is sent.
Measured at 8 MHz SPI: about 14 ms for a full refresh, 2-9 ms for a partial one (`NOTES.md`). The panel's
VCOM inversion is toggled every 500 ms by a timer, i.e. a 1 Hz polarity flip.

**Pre-rotated glyph atlases** (`firmware/src/gfx/atlas.cpp`, `font.cpp`). The panel is physically 400x240
and used rotated into portrait, so a row of text in the picture is a column in memory, which would need one
bit written at a time. An `Atlas` stores each glyph already rotated into the panel's native orientation, so
drawing is a shift-and-mask copy of whole bytes per native row. `Font1` builds one atlas per ASCII glyph
(32..126) once, for one rotation. Measured: "1234" in FreeSansBold24pt 2783 us before, 332 us after.

## 11. Numerics

**Float vs double** (`CLAUDE.md`). The nRF52840 FPU is single precision, so ordinary maths is `float`. Double
(software, slow) is used for latitude/longitude,
and for sums that need it: the wind circle fit and the compass calibration normal equations. The wind EKF
is float only.

**Missing data is NaN.** Every missing or out-of-range value becomes NaN, derived values propagate it, and
nothing is derived from what is missing. The single place a number becomes text is `fnum()` in
`firmware/src/view/screens.cpp`, which prints `--` for any non-finite value. Angles passed to drawing code
are also checked, because converting NaN to int is undefined behaviour.

**The `-Ofast` pitfall.** The Adafruit nRF52 core builds with `-Ofast`, which implies `-ffinite-math-only`.
The compiler then assumes NaN never occurs and folds `isfinite()` / `isnan()` to constants, so every guard
above was dead code on the device while the PC emulator (no `-Ofast`) looked fine. It was found when the
heading smoother stuck at NaN (2026-10-15). `platformio.ini` now unflags `-Ofast` and builds with `-O2`, and
`firmware/src/core/types.h` stops the build with `#error` if `__FINITE_MATH_ONLY__` is set.

**Stack and time.** Large work buffers (history, trails, view model) are members of long-lived objects, not
locals, because the loop task has a 4 KB stack. Flight time never reads the wall clock (section 2).
