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

`firmware/src/apps/vario/core/xctrc.cpp`

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

**Staleness.** `App::hasData` (`firmware/src/apps/vario/app/app.cpp`) is true only while the last valid line arrived
less than `freshMs` = 3000 ms ago by the box's own clock. Stale data is shown as no data and is not fed
to the heading selector as GPS. This is the only place the wall clock matters; all flight maths below
use the data's time stamps.

## 2. Flight model

`firmware/src/apps/vario/core/flight.cpp`

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

`firmware/src/apps/vario/core/wind.cpp`, called from `FlightModel::updateWind`.

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

**Cost.** The fit runs once per new data second, and only with a fix at the newest point. `fitWind`
makes its cheap rejections first (fix, speed, turning at both ends, heading coverage) and evaluates the
sin/cos of the 30 velocity vectors only for a window that can still pass, so straight flight costs no
trigonometry. The results are bit-identical to evaluating them first.

## 4. Wind Kalman filter

`firmware/src/apps/vario/core/windekf.cpp`

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
* **Learning gate**: the update is applied only in a turn: the course must have moved by at least 30 degrees within the
  last 20 usable seconds (a ring of the last 20 courses of seconds with a fix and speed >= 3 m/s;
  $\max_i|c_{now}-c_i|\ge 30^\circ$). Otherwise the step only grows the covariance. Why: in a near-straight glide
  the airspeed is not constant (speed bar, turbulence) and the course wanders, so there is no wind information, yet
  the filter explained the speed scatter with a large wind and a tiny airspeed while its covariance shrank (the
  model says $V_a$ is constant) and it reported confident. The constant-$V_a$ assumption fails exactly where
  the wind is unobservable.

| Constant | Value |
|---|---|
| Wind process noise $Q_W$ | 0.0004 (m/s)$^2$ per second |
| Airspeed process noise $Q_{V_a}$ | 0.01 (m/s)$^2$ per second |
| Measurement noise $R$ | 1.0 |
| Initial state | wind 0, 0 and $V_a$ = 10 m/s |
| Initial covariance | diag(25, 25, 9) |
| Step skipped if | no fix, non-finite input, speed < 3 m/s, or $r<0.5$ m/s |
| Outlier gate | skip the update if $y^2 > 9S$ (3 sigma). Outlier balance $k$: +1 per outlier, -1 per accepted update; $k = 5$ sets $P_{22}=9$ and the wind-airspeed covariances to 0; $k \ge 10$ sets $P=\mathrm{diag}(25,25,9)$ and $k=0$ |
| Learning gate | course spread $\ge 30^\circ$ within the last 20 usable seconds |
| Clamps | $V_a$ in 5..25 m/s, wind magnitude at most 25 m/s |
| Time step | $\Delta t$ clamped to 1..5 s |

**Confidence**: $\sigma_W=\sqrt{P_{00}+P_{11}}$, confident when $\sigma_W<0.8$ m/s.

**Which wind is used.** `FlightModel::wind()` and the internal `windXY()` return the EKF wind when it is
confident, otherwise the smoothed circle-fit wind. This one vector feeds the wind text and arrow, the
home calculation, the air-mass map and the thermal drift.

**Age of the EKF wind.** The reported age is the flight seconds since the EKF last received an
*informative* update: a measurement update that was applied (not skipped, not rejected as an outlier)
while the course had moved by at least 15 degrees within the last 10 s. A straight glide changes nothing
the filter can know (constant airspeed makes it unobservable) yet keeps applying updates and keeps the
covariance low, so "time since the last update" or "time since confident" would never grow and the
screen's `old` marker (age > 300 s) could never show. The filter's sigma grows only by $Q_W\Delta t$
(about 0.0004 per second per axis), so it stays confident for many minutes of straight flight: the wind
is then shown as `old` rather than dropped. If sigma does pass the gate the circle fit takes over with
its own age.

**Seeding.** The EKF starts at zero wind with $P=25$. The first accepted circle fit (and the first after a
gap of more than 120 s) re-initialises it to the fit wind with $P=\mathrm{diag}(4,4,9)$ and $V_a=10$,
but only while the EKF is not confident; a confident EKF is left alone, since it has averaged much
more data than one 30 s fit. The seed happens before that tick's EKF step, so the sample still counts.

**Restart on contradiction.** While the EKF is confident and the smoothed circle-fit wind is more than 3 m/s away from
it, a counter of accepted fits in a row grows (it resets when they agree); at 8 the EKF is re-initialised from the smoothed fit
with $P=4$ (not confident, so the circle fit is shown meanwhile). The fit measures the wind directly from a whole turn
with strict gates; the EKF can be pulled off by a model assumption it cannot check, and its small covariance then
rejects the correct data at the 3 sigma gate. Without this a wrong confident EKF lasted about 1000 s
(NOTES.md, "Wind EKF locked high"). The wind profile (next section) deliberately uses the circle fit only, because the EKF adapts over
minutes on purpose and would smear how the wind changes with height.

## 5. Wind and temperature by altitude

`firmware/src/apps/vario/core/windprofile.cpp`, page drawn in `firmware/src/apps/vario/view/screens.cpp`.

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

`firmware/src/apps/vario/core/thermal.cpp`, `FlightModel::airTrail` / `updateThermals` in `flight.cpp`.

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

**Motion between samples** (`apps/vario/core/map_motion.cpp`, `App::updateMap`). Samples arrive about 3 per second, so
the map is drawn as of a display time $t_d = t_{now} - (\bar\Delta + 50\,\text{ms})$, where $\bar\Delta$ is
an exponential average of the arrival gaps. The pilot position is the linear interpolation between the
two samples around $t_d$ (never extrapolated; $t_d$ is held at the newest sample when data stops, and
catches up at twice real time after a pause). In the trail's frame (origin = the sample that started the
newest data second) the view centre is

$$\mathbf c = \mathbf g(t_d) - \mathbf g_o - \mathbf w\,\rho\,(t_d - t_o)$$

with $\mathbf g$ the interpolated ground position, $\rho$ the measured data-time / wall-time ratio (1 live,
3 in a 3x replay; the time stamps only resolve seconds) and $\mathbf w$ the wind of `airTrail`. Trail
points newer than $t_d$ are dropped and $\mathbf c$ is appended, so the line ends under the arrow. The
map rotation is the direction of $\mathbf c(t_d) - \mathbf c(t_d - 3\,\text{s}/\rho)$ on the same
interpolated path (GPS course, interpolated on the shortest arc, when it is shorter than 3 m). A gap above
2 s, a jump above 150 m or a lost fix snaps to the newest sample. The shown scale approaches the fixed
zoom step exponentially, $\tau = 300$ ms. The app redraws at 33 ms while the centre moved more than half a
pixel (at the current scale), the rotation more than $0.5/104$ rad, or the scale is still gliding, since
the last drawn frame.

## 6b. The climb column and the centring cue

`firmware/src/apps/vario/core/thermal3d.cpp` (`ThermalColumn`). One point per data second (ground position, altitude, vario),
600 points kept (10 min), fed by `FlightModel::tick`.

**Air frame.** As in section 6: a point flown $a$ s ago is moved downwind by $a\,\mathbf w$, then everything is relative to the newest point.

**Circles.** The turn rate is the change of the *ground* course of the step (not of the air-frame velocity: with a wind estimate a few m/s
off, the air-frame heading spins wildly at 8-10 m/s airspeed; the ground course keeps turning 360 degrees per circle while the wind is
below the glider speed). Smoothed over 5 points, a point turns when $|\bar r| \ge 4$ deg/s; a run is one direction, gaps up to 8 s bridged, kept
when it turns $\ge 270$ degrees. The latest climb is the newest run plus earlier runs less than 30 s before it; it is shown up to 120 s after
it ended. The runs are cut every 360 degrees (signed sum of the turn rate, so heading noise cancels): a circle.

**Per circle.** Centre $\mathbf c$ = mean position, mean altitude, mean climb, best sector = maximum of the 3-point smoothed vario and its position.
Core point $= \sum w_i \mathbf p_i / \sum w_i$ with $w_i = \max(v_i, 0)$ (the lift-weighted centroid; $\mathbf c$ if the circle climbs less than 0.3 m/s).

**Lean.** Least squares of the core points of the newest 10 circles, $x(z)$ and $y(z)$ (at least 3 circles over 30 m; one pass drops points
farther than $\max(2.5\sigma, 8\,\text{m})$). Lean = $100\sqrt{s_x^2+s_y^2}$ m per 100 m, direction $\operatorname{atan2}(s_x, s_y)$.
It is only as good as the wind: a wind error $\delta w$ adds $100\,\delta w / \text{climb}$ m per 100 m (0.5 m/s at 2.5 m/s climb: 20 m/100 m,
a unit test). More than 60 m/100 m is not shown.

**Strength by height.** Mean vario in bands of 25 m (50, 100, 200 for taller climbs, at most 24 bands), at least 3 points per band.

**Trend.** Newest two circles against the best two before them: ratio $<0.4$ (or $<0.4$ m/s after $\ge 0.8$) top, $<0.7$ fading, $\ge 1.15$ building.

**The cue** (`CoreCue`, once per second while circling, from two full turns). Window = the newest 360 degrees of heading change (sliding, not
aligned with the circles). Its centre $\mathbf c_0$ and lift-weighted centroid $\mathbf q$ give the core estimate
$$\mathbf k = \mathbf c_0 + g\,(\mathbf q - \mathbf c_0),\quad g = 2.5,\qquad \text{shift} = \mathbf k - \mathbf c_0 .$$
The gain exists because the centroid of a circle only gets part of the way to the core: for a Gaussian core of radius $\sigma$ and a circle of
34 m the centroid is 95 % (sigma 20), 50 % (30), 25 % (45), 11 % (70) of the true offset. Shifts over 60 m are not believed (no cue).
Blending the two older turns (weights 0.5 / 0.3 / 0.2) and extrapolating along the lean to the current altitude were tried and measured
worse (NOTES.md); the older turns only give `spread`. The view draws it heading-up: the up direction is the air-frame direction of the last 3 s of track.

## 7. Home

`firmware/src/apps/vario/core/home.cpp`

**Which point.** The first fix is a provisional home. Takeoff is detected 5 s after the speed passed
10 km/h (section 2), by which time the pilot is already along the track and in the air, so the position and
altitude where speed *first crossed* the threshold are remembered as the candidate start (the same
instant as `flightStart_`) and replace the provisional home, once, when takeoff is confirmed. If the speed
falls below the threshold again before that, the candidate is discarded. Later slow flying does not
move home.

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

`firmware/src/platform/sensors/compass.cpp`, `ahrs.cpp`, `mount.cpp`. Pure maths. The hardware layer maps each chip's axes to
the board frame; the app rotates board to device (x right of the screen, y top of the screen, z out of the
screen) with the mounting index (8.2). Two heading paths: the **gyro fusion** (8.1, used when the gyro works)
and the **plain tilt-compensated compass** (the rest of this section; the fallback, and always the source of
the calibration and the field strength).

**Smoothing before the maths.** Each reading is blended into a running vector with a time-based
exponential low-pass, $\mathbf a\leftarrow\mathbf a+\alpha(\mathbf a_{new}-\mathbf a)$ with
$\alpha=1-e^{-\Delta t/\tau}$, $\tau_a=60$ ms for the accelerometer and $\tau_m=50$ ms for the (calibrated)
magnetometer. $\Delta t$ is the time since the previous reading (clamped to 500 ms; the first reading seeds
the filter), so the response does not depend on the sensor rate (magnetometer 80 Hz, accelerometer 104 Hz).

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

### 8.1 Gyro fusion (Mahony filter)

`Ahrs` keeps a quaternion $q$ (device to world: east, north, up) and the matching matrix $R$ (row $i$ = world
axis $i$ in device coordinates). Inputs in the device frame: gyro $\boldsymbol\omega$ (rad/s), accelerometer
$\mathbf a$, hard-iron corrected field $\mathbf m$. Mahony rather than Madgwick because the correction is a PI
controller on a rotation vector: the magnetic part can be applied about the vertical axis only, the bias
comes from the integral term, and the gains are time constants.

**Errors** (rotation vectors in the device frame). $\mathbf u=R_{3,\cdot}$ is the predicted "up",
$\hat{\mathbf a}=\mathbf a/|\mathbf a|$:

$$\mathbf e_a=\hat{\mathbf a}\times\mathbf u\quad(\text{only if }\bigl||\mathbf a|-g\bigr|<0.12\,g),\qquad
\mathbf m_w=R\,\mathbf m,\quad\varphi=\operatorname{atan2}(m_{w,e},\,m_{w,n}),\quad \mathbf e_m=w\,\varphi\,\mathbf u$$

$\mathbf e_m$ uses only the **horizontal** direction of the field, as seen with the current tilt, and rotates
about the vertical: a disturbed field twists the heading but cannot tilt the attitude. It is used only if the
horizontal field is above 8 uT, $|\mathbf m|$ is within 20% of a slow (5 s) reference and the dip
$m_{w,u}/|\mathbf m|$ within 0.2 of its reference (after 5 s of rejection the new field is adopted). The weight $w$ is the **trust** in
the field: with $d=\max(\bigl||\mathbf m|-m_{ref}\bigr|/m_{ref},\ |dip-dip_{ref}|)$, $w=1$ for $d\le0.05$ and falls
linearly to 0 at $d=0.20$ (where the old hard gate was), so a field that starts to disagree is followed less
instead of being dropped at a threshold.

**Update** each sample, $\Delta t$ from the timestamps (clamped to 0.1 s; a gap over 0.5 s re-seeds):

$$\boldsymbol\omega'=\boldsymbol\omega-\mathbf b+\beta\,(k_{p,a}\mathbf e_a+k_{p,m}\mathbf e_m),\qquad
q\leftarrow q\otimes\exp(\boldsymbol\omega'\Delta t),\qquad
\mathbf b\leftarrow\mathbf b-\Delta t\,(k_{i,a}\mathbf e_a+k_{i,m}\mathbf e_m)$$

| Gain | Value |
|---|---|
| $k_{p,a}$ (gravity, tau 1 s) / $k_{i,a}$ | 1.0 /s / 0.05 /s$^2$ |
| $k_{p,m}$ (field, tau 10 s) / $k_{i,m}$ ($=k_p^2/4$) | 0.1 /s / 0.0025 /s$^2$ |
| $\beta$ (start boost) | gravity 8, field 40 (4 /s) at the (re)start, linearly to 1 over 2 s; the integral is off during it |

The field gain is low on purpose: the learned gyro bias carries the heading, so the field only removes slow
drift, and its noise (a few degrees per sample at 24 uT horizontal field) is averaged over about 10 s instead
of showing as a wander of several tenths of a degree. The boost is repeated for each of the first three
bias estimates (the heading drifted by the old bias error until then) so a box that starts on a desk settles in
seconds. With a bias error $\Delta b$ left over the heading sits $\Delta b/k_{p,m}$ off (0.05 deg/s gives 0.5 deg).

The bias is bounded to 12 deg/s per axis. The integral is paused while the still detector (next) owns the bias.

**Bias from standing still.** Per 0.5 s window: gyro standard deviation about the window mean below 0.6 deg/s,
gravity usable, and the mean gravity and field directions within 0.4 and 0.8 degrees of the previous window's.
Two such windows in a row (1 s or more) and $\mathbf b\leftarrow\mathbf b+r\,(\bar{\boldsymbol\omega}-\mathbf b)$ with
$r=\max(0.15,\,0.6/(1+n))$, $n$ = windows used so far (quick at first, then a slow average).

**Seed.** The first usable sample (and the first after a gap) sets $R$ from gravity and the field with the same
basis as the plain compass: rows east $=\hat{\mathbf m}\times\mathbf u$ (normalised), north $=\mathbf u\times$ east, up. The
bias is kept across a gap.

**Output.** Heading $=\operatorname{atan2}(R_{1,2},\,R_{2,2})$ (east and north components of the device y axis), degrees,
0..360. Valid while gravity was usable within the last 10 s and the field within the last 30 s. A gyro that
repeats the same three values 80 times in a row counts as failed: no fusion until it moves again. `App` uses the
fused heading while the gyro delivers data and the filter is valid, otherwise the plain compass.

### 8.2 Board mounting

Two rotations: per chip, chip axes to the board frame (fixed by the PCB, in `platform/hw/compass_lis.cpp`), then one of
the 24 signed axis permutations with determinant +1, selected by an index 0..23 (flash file `/mount.bin`,
`ctl.py orient`), applied to accelerometer, gyro and field alike: $\text{device}_r=s_r\,\text{board}_{p_r}$. The
table in `platform/sensors/mount.cpp` is ordered like `itertools.permutations` of the axes times the sign triples
$(+,+,+),(+,+,-),\dots$, keeping $\det=+1$:

| Index | perm | signs | Index | perm | signs | Index | perm | signs |
|---|---|---|---|---|---|---|---|---|
| 0 | xyz | +++ | 8 | yxz | ++- | 16 | zxy | +++ |
| 1 | xyz | +-- | 9 | yxz | +-+ | 17 | zxy | +-- |
| 2 | xyz | -+- | 10 | yxz | -++ | 18 | zxy | -+- |
| 3 | xyz | --+ | 11 | yxz | --- | 19 | zxy | --+ |
| 4 | xzy | ++- | 12 | yzx | +++ | 20 | zyx | ++- |
| 5 | xzy | +-+ | 13 | yzx | +-- | 21 | zyx | +-+ |
| 6 | xzy | -++ | 14 | yzx | -+- | 22 | zyx | -++ |
| 7 | xzy | --- | 15 | yzx | --+ | 23 | zyx | --- |

("perm" lists which board axis feeds device x, y, z; "signs" are $s_x s_y s_z$.) `host/orient_analyze.py` has the
same table and its self-test parses `mount.cpp` to check it. The hard-iron offset is stored in the device frame,
so changing the index rotates the stored offset along (and the fusion restarts). Mountings that keep the y axis
(index 0 and 2) give the same heading and differ only in tilt.

## 9. Heading source and smoothing

**Selector** (`firmware/src/apps/vario/core/heading.cpp`, thresholds from `AppConfig` in `app.h`). GPS track is
used once ground speed reaches **4 km/h** and is released only when it falls below **2.5 km/h**; between the
two it keeps its current state (hysteresis, so it does not flap at walking speed). GPS counts only when the
data is fresh (section 1), has a fix, and speed and course are finite. Otherwise the magnetic heading is
used if valid, otherwise there is no heading and the dial shows `--`. The screen labels the source `TRK` or
`MAG`.

**Smoother** (`firmware/src/platform/sensors/heading_smoother.cpp`). Source `MAG` has two tunings: with the gyro fusion
$\tau=30$ ms (the fused heading is already smooth, so this is nearly pass-through), without it $\tau=90$ ms;
the 0.4 degree deadband (below) applies to the compass alone only. The fused source has **no deadband** and does not snap
at 0.5 deg (it snaps at 0.01 deg): a slow turn is a continuous glide. The dial shows a glided value, advanced once per
UI tick using the UI clock (this is display animation, not flight data). With target $\psi_t$, shown value
$\psi$, shortest signed difference $\delta\in(-180,180]$ and elapsed time $\Delta t$:

$$\psi\leftarrow\psi+\delta\,\bigl(1-e^{-\Delta t/\tau}\bigr),\qquad \tau=30\ \text{ms (fused compass)},\ 90\ \text{ms (compass alone)},\ 250\ \text{ms (GPS track)}$$

The exponential form makes the result independent of the frame rate, and the shortest arc means 350 to 10
degrees turns through 0, not the long way. It **snaps** to the target (no animation) when: the shown value is
not yet known (first value or after NaN), more than 2000 ms have passed since the last update, or the
remaining difference is under 0.5 deg (0.01 deg for the fused source). A non-finite target resets it. For the
GPS track and the compass alone, while it has not settled the app redraws every 33 ms on the compass page and
the standalone compass (50 ms on the home and thermal pages). For the **fused** source "settled" is
meaningless (it always glides a little), so the rule is: the app remembers the heading of its last live draw and
uses the animation cadence whenever the shown heading is more than half a pixel of arc away from it,
$\tfrac{0.5}{104}\,\tfrac{180}{\pi}\approx0.28^\circ$ (104 px is the dial ring radius), otherwise the slow cadence (333 ms).
Sub-pixel wander therefore never redraws, and a turn of $\omega$ deg/s redraws about $\omega/0.28$ times per
second (7/s at 2 deg/s, capped by the 33 ms cadence), each step half a pixel.

The app picks $\tau$ by source: the GPS track updates about 3 times per second and needs the long glide;
the magnetic heading arrives at 80 Hz already filtered (section 8), so 90 ms is enough. For the magnetic
source a **deadband** keeps sensor noise from running the animation forever: the target moves only when the
new heading is more than 0.4 deg from it, or when it has stayed more than 0.15 deg to one side for over
300 ms (a slow real drift). A retarget that lands within 0.8 deg of the shown value snaps instead of gliding.
With the box still the dial settles and the slow redraw cadence resumes. The GPS source has no deadband.
(The deadband was dropped for the fused source because it turned slow motion into rest-and-jump steps, and the
redraw rate dropped between the jumps.)

## 10. Rendering

Grey levels, partial refresh and pre-rotated glyphs exist because this chip is slow at per-pixel work
(Adafruit GFX cost 1.46 us per pixel; a full thermal frame took 103 ms before the fast path and 22 ms
after, `NOTES.md`).

**Ordered dither** (`firmware/src/platform/gfx/gray.h`, `gray.cpp`). The panel is 1 bit per pixel. Grey is made
with an 8x8 Bayer matrix. The threshold $T(x,y)\in[0,63]$ is built by bit interleaving of $x\oplus y$ and $y$
(`bayer8`). A grey level $L\in[0,16]$ (0 black, 16 white, 17 steps) lights a pixel when $T(x,y)<4L$, so
level 8 lights exactly 32 of every aligned 64 pixels. The threshold uses **screen** coordinates, not the
shape's origin, so touching grey areas have no seam and a shape moved by one pixel does not shimmer.
Grey sprites store 4 bits per pixel; nibble $n$ becomes level $\lfloor(16n+7)/15\rfloor$. The views use two
shades: LIGHT = level 13 and MID = level 8 (`screens.cpp`).

**Partial refresh** (`firmware/src/platform/panel/linediff.cpp`, `firmware/src/platform/hw/panel_sharp.cpp`). The frame is 240
native lines of 50 bytes. Each present compares every line with the snapshot of what was last sent
(`memcmp`) and sends only the changed lines; each line is sent with its 1-based address. If more than 200
lines changed (or the panel was marked dirty) the whole frame is sent, and if nothing changed nothing is sent.
Measured at 8 MHz SPI: about 14 ms for a full refresh, 2-9 ms for a partial one (`NOTES.md`). The panel's
VCOM inversion is toggled every 500 ms by a timer, i.e. a 1 Hz polarity flip.

**Pre-rotated glyph atlases** (`firmware/src/platform/gfx/atlas.cpp`, `font.cpp`). The panel is physically 400x240
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
`firmware/src/apps/vario/view/screens.cpp`, which prints `--` for any non-finite value. Angles passed to drawing code
are also checked, because converting NaN to int is undefined behaviour.

**The `-Ofast` pitfall.** The Adafruit nRF52 core builds with `-Ofast`, which implies `-ffinite-math-only`.
The compiler then assumes NaN never occurs and folds `isfinite()` / `isnan()` to constants, so every guard
above was dead code on the device while the PC emulator (no `-Ofast`) looked fine. It was found when the
heading smoother stuck at NaN (2026-10-15). `platformio.ini` now unflags `-Ofast` and builds with `-O2`, and
`firmware/src/apps/vario/core/types.h` stops the build with `#error` if `__FINITE_MATH_ONLY__` is set.

**Stack and time.** Large work buffers (history, trails, view model) are members of long-lived objects, not
locals, because the loop task has a 4 KB stack. Flight time never reads the wall clock (section 2).
