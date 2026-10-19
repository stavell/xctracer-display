# Review of the thermal assistant (thermal page + centring page)

## Verdict
The ideas are sound, but the centring page is not yet something a pilot can use at 40 degrees of bank. Three problems:
1. It needs two full turns (~50 s) of data and it shows up in only about 20 % of circling seconds.
2. On whole flights its accuracy is much lower than the NOTES.md numbers (those came from three hand-picked climbs).
3. The page carries about ten elements. The pilot's one decision (flatten and shift this way, now) is not one of them.

Keep the maths. Replace the page with a one-glance cue, and use the box's own gyro for timing (see the last section).

## Theory, short (web research was thin; sources at the end)
- Tighten on the surge: when the lift is strongest, or the wing is pushed up, tighten and stay in that core. Widen or straighten when it weakens.
- If the lift is clearly stronger on one side and the average is dropping, a new core is probably nearby. Widen out through the strong side, wait a moment, then tighten again.
- Reichmann-style rules: flatten the bank when the climb improves, steepen when it worsens. They work better in large thermals and are hurt by turbulence.
- The wing responds late, so weight shift comes first and the brake follows about 2 s later.
- A surge is felt at the wing first, then seen on the vario, so pilots act on feel and sound. Delay is the central problem.
- Thermals are bubbles or several cores, not clean columns. Cochrane warns that chasing every vario fluctuation puts you out of phase because of the lags of vario, pilot and glider.
- I could not retrieve the XCSoar, SeeYou, Oudie, Flymaster, Skytraxx or XCTrack thermal-assistant documentation or any pilot ratings. I do not claim anything about how they rate. Dealers only say the XCTracer's own display shows a thermal assistant and an altitude curve of the last 30 s.
- What I know from general experience, unverified here: assistants that show a track trail with a colour-coded climb are liked because they cost one glance. Complaints are lag, jitter and clutter.

## What we have (code and NOTES.md)
`core/thermal3d.*` finds circles in the air-mass frame and takes the lift-weighted centroid of the newest turn. It pushes this by gain 2.5 from the circle centre to get a core. The cue is "move the circle this far, in this direction". `core/thermal.cpp` is a separate thermal memory with age and drift. The page draws the last two turns, bead, core target, a fat arrow, 25 / 50 m rings, a climb gauge, a lean arrow, a trend glyph and strength-by-height bars.

## Data (this session, `build.sh cue`, whole flights)
I used the IGC files with GPS altitude. Sopot has pressure altitude 0, so I swapped the altitude field: it gives 939 cue-seconds, where the pressure-altitude run gave a median cue of 0 m and no pairs at all.

| flight | cue available | cue vs next turn's lift (90 deg) | baseline | pilot moved toward the cue, next-turn climb change |
|---|---|---|---|---|
| Sopot, full 5216 s | 18 % | 68 % (n 236), median angle error 58 deg, distance error 37 m | 61 % | -0.33 m/s (toward), -0.75 m/s (away) |
| SVE, full 10905 s | 21 % | 64 % (n 554), 64 deg, 43 m | 55 % | -0.04 (toward), -0.14 (away) |

- A random direction scores 50 %. So the cue is about 15 points better than chance and 7-9 points better than "last turn only". That is useful but weak. The NOTES figures of 85-100 % were on selected climbs.
- The median angle error is 58-64 degrees. A 60 degree error on an arrow is a coin flip about which half-circle to shift into.
- Following the cue gave no measurable gain (-0.33 vs -0.75 m/s is worse in both cases). That comparison is confounded, because a pilot who moves is usually in a worse bit. Only a flight with the pilot using the cue can answer it.
- The distance error of about 40 m is larger than a typical paraglider thermal core, so the "shift by X m" number and the rings promise precision the data cannot support.
- The rendered page (`docs/screens/center_cue.png`) is busy: ten elements, two circle trails, three overlaid rings and a lean arrow. Nothing says "flatten now, go left".
- The emulator's full test run also reports 2 failures (`sim_playability`, `sim_day_settings`, "ended without finishing"). They are in the flight simulator, not the vario, and may be only an artefact of my environment. I did not investigate.

## What matches good practice
- The lift-weighted centroid is the machine form of "shift toward the strongest lift".
- Working in the air-mass frame (wind removed) is correct.
- Ground course instead of the air-frame heading for circle detection is a robust choice.
- Trying to extrapolate the lean and blending older turns, then dropping both when the data got worse, was the right call.
- The strength-by-height / trend idea (building, fading, top) is a thermal-structure cue that pilots do value.
- The honest measurement habit (hit rate against the next turn) is better than anything the commercial products publish.

## What is likely useless or misleading in the air
1. **Latency.** Data is 1 to 3 points per second plus tracer filtering. A turn takes 24-30 s, so a 1 s delay is a 12-15 degree error in position in the circle. After two turns, the cue describes where the lift was 25-50 s ago, in a thermal that is bubbly. The next turn's centre moved 10-30 m on the real climbs.
2. **Numbers that imply precision**: metre distances, 25 / 50 m rings, a lean arrow in m/100 m. The wind error alone can make the lean wrong by hundreds of m/100 m (NOTES). The lean arrow is not actionable at all.
3. **Two older-turn trails, the bar chart and the gauge.** The pilot can act on one thing: which side is stronger and when. The rest is a log-reader's display.
4. **Cue available only 18-21 % of the time**, absent exactly when the pilot is entering the thermal and needs it most (first 50 s).
5. **Arrow shown when the pilot is already centred** (median cue 7-21 m) invites chasing noise. The cue tool already shows "toward" and "away" end up similar.
6. **It needs the pilot to look away from the sky** at 40 degrees of bank. Vision is needed for traffic and cloud.

## What experienced pilots value that is missing
- A **clock-face position in the turn** with the strongest lift marked on it: "the best lift was at 3 o'clock, I am at 5 now, so straighten when I reach the 3-ish". This is what the 1/4-turn delay rule needs and what the thermal trail encodes only indirectly.
- A **timing cue**: an audible or visual tick when the best-lift bearing is about 90 degrees ahead. The shift is made one quarter turn after the surge, which is the one number the box can know better than the pilot.
- **Earlier output**: a half-turn estimate with a visible "low confidence" state, rather than nothing for 50 s.
- **Average climb per turn, large** (the existing +2.0 number and the gauge are good), plus "better / worse than last turn".
- **Thermal memory of the last thermals** for a re-entry after a loss: exists in `thermal.cpp`; keep it small.

## Ranked improvements
1. **Simplify the page to one cue**: the best-lift bearing as a single sector on a ring, drawn at about 40 mm size, plus the climb average. Remove lean, bars, gauge, trend and the older trail from the page (move them to the profile page). Why: glance cost. Validate: pilot test, and a count of elements. No cue-tool change.
2. **Show the cue from the first half turn, with a confidence mark** (grey sector, low spread). Why: it is missing in the first 50 s. Validate: `build.sh cue` on whole flights with a half-turn mode; keep it only if the hit rate stays above about 65 %. Beware 18 to 40 % availability changing the sample.
3. **Report direction in 8 sectors, not metres.** The data supports about 45 degree resolution at best (median error 58-64 deg). Validate: hit rate within 45 degrees on the same flights; expect about 35-40 %, which is the honest number.
4. **Fix the circle phase with the box's gyro** (details below). Validate: compare the gyro heading with GPS course at the tracer's sample times; the delay seen in the cue then becomes measurable.
5. **Suppress the cue when centred or when the thermal is broken up.** Use the spread (median 62-74 m) as a gate: if the spread is bigger than the shift, show nothing. Validate: hit rate vs coverage curve in the cue tool; pick the knee.
6. **Run the cue tool on whole flights routinely**, and add a "pilot followed the cue and climb improved" statistic that excludes broken thermals. It currently mixes them (Sopot 3100 vs 1100).
7. **Fly it.** The NOTES say "not measured: a pilot flying with the cue". Do this before any more tuning.

## What the box's own sensors can add (theory; not measured here)
The box has an LSM6DS3 / DS33 accelerometer and gyro at 104 Hz and a LIS3MDL magnetometer at 80 Hz (`platform/hw/compass_lis.cpp`), a BMP280, an SHT30, an APDS9960 and a PDM mic. An AHRS exists (`platform/sensors/ahrs.*`). I could not find published experience in this session, so the following is engineering reasoning, ranked.

1. **Gyro yaw rate and integrated heading as the circle phase (best value).** GPS course at 1 to 3 Hz lags by about 1 s and is noisy at 10-14 deg/s turn rates. The gyro gives the turn rate with no lag and about 10 ms resolution, so the exact bearing at which the vario sample was taken is known. This removes the position-in-circle error (12-15 degrees per second of delay) and allows the 1/4-turn timing cue. Caveat: the gyro axis must be the world vertical, so use the AHRS gravity vector, not the board z axis. Pilot pendulum swing is a few Hz and averages out of a 24 s circle; filter at 1 Hz. Validate: log gyro z at 104 Hz and the raw `$XCTRC` lines with the box clock; check the circle period and the phase offset between gyro heading and GPS course across a flight.
2. **Local vario from the BMP280 plus vertical acceleration (complementary filter).** Gain: a 10-20 Hz vario without the tracer's BLE delay (the tracer already fuses baro with accelerometer and gyro; the BLE line arrives about 3 times per second with its own latency). The honest limit: the BMP280 noise is about 0.1 to 0.2 m in the best mode, which makes the derivative noisy below about 2 Hz, and the accelerometer on a pilot swinging under a wing carries large non-thermal vertical motion. It would help only if the BLE path has a delay of more than about 0.5 s; measure that first. Validate: log baro pressure, vertical acceleration and gyro at 25-50 Hz with the box clock, plus tracer lines with the receive time, and compare the cross-correlation lag between the local and the tracer vario.
3. **Load factor / vertical acceleration as a bank and surge cue.** With a gravity estimate, |a| in a steady turn gives the bank (1/cos(bank), 1.3 g at 40 degrees). A surge shows as a short vertical acceleration spike at the moment the wing enters the strong air, before the vario responds. This is the real "feel" signal pilots use, and the box could in principle mark where on the circle it happened. Hard part: the box sits on the pilot or cockpit, so it sees harness pendulum motion and turbulence at the same magnitude as a surge (about 0.1 to 0.3 g). Expect it to work as an average per sector, not as a single event. Validate: log a(t) at 50 Hz in flight; check whether the per-sector mean vertical acceleration correlates with the next-turn lift at the same bearing.
4. **Bank angle from the gyro and gravity**, to show if the pilot is flying too wide or too tight versus the thermal size. Low value without a target; only useful in a post-flight analysis.
5. **Magnetometer**: poor in the cockpit (steel, electronics, vario speaker); keep as heading support only.
6. **Gimmicks**: SHT30 temperature (time constant of tens of seconds, in a case that warms, cannot see a thermal, though it may flag the cloud base by humidity, which is a separate feature); APDS9960 light and proximity; mic (wind noise).

Mounting: a cockpit or chest mount moves with the pilot swinging on the risers. A riser mount sees the harness roll. Either way, use only low-pass-filtered quantities (about 1 Hz) and gravity from the AHRS, never raw accelerometer axes.

What to log for validation: raw IMU at 50-100 Hz (accel, gyro, mag), BMP280 at its maximum rate, the receive timestamp of every tracer line, and a marker for button presses. Then replay in the emulator as for the tracer lines.

## Sources
- Skynomad, better thermalling: https://www.skynomad.com/articles/better_thermalling.htm
- XC Magazine, how to thermal better: https://xcmag.com/news/how-to-thermal-better/
- Cochrane, Flying Faster (lags of vario, pilot and glider): https://www.johnhcochrane.com/s/FlyingFaster2.pdf
- MIT Technology Review on Reichmann rules vs direction changing in thermals of different size: https://www.technologyreview.com/2010/12/06/198611/peregrine-falcons-inspire-new-thermal-soaring-strategy-for-uavs/
- XC Tracer Maxx II dealer page (thermal screen has an assistant): https://406paragliding.com/product/xc-tracer-maxx-2/
- Repo: NOTES.md "Climb column and the centring page", PLAN.md "Centring page"
