# Review and improvement plan

Review of the firmware and tools as of commit `7f3aea1` (written by one model, reviewed by another).
The layering (core / view / app / hw, ports, PC emulator with 207 checks) is sound and worth keeping.
The gaps below are what stands between "works on the bench" and "trustworthy in flight".

## Findings

Severity: **H** = can show wrong data in flight or corrupt state, **M** = robustness or maintainability
with a real failure mode, **L** = hygiene.

| # | Sev | Finding | Where |
|---|-----|---------|-------|
| 1 | H | No watchdog. If the main loop hangs, the memory LCD keeps the last frame and the VCOM timer keeps it alive, so stale numbers look live. A hang already happened once during development. | `main.cpp` |
| 2 | H | Laptop drawing runs in the BLE callback task: it writes the shared framebuffer, calls `clear()` and `setRotation(0)` while the main loop may be drawing the vario screen (108 ms per frame). Interleaved writes and a rotation change mid-draw corrupt the frame. | `hw/laptop_ble.cpp` |
| 3 | M | The callback-to-loop queues use `volatile` indices only. Non-volatile payload stores may be reordered after the index store, so the consumer can read a half-written line or command. | `hw/tracer_ble.*`, `hw/laptop_ble.*` |
| 4 | M | `Bluefruit.Scanner` is driven from two tasks: `onDisconnect` (callback task) calls `startScan()` while the loop calls it from `update()` / `listOpen()`. The scan list `items_` is written in the scan callback and read in the loop without synchronisation; `cfg_.updateAddr()` runs in the callback. | `hw/tracer_ble.cpp` |
| 5 | M | Reconnect after a dropped link uses the low-duty scan (50 ms window per 1 s), so in flight the display can stay on "SEARCHING" for many seconds. | `hw/tracer_ble.cpp` |
| 6 | M | Pure logic lives inside hardware adapters and has no tests: name glob, device-matching policy (the same-name bug came from here), RSSI sort (duplicated twice), laptop packet decoding and its length checks. | `hw/tracer_ble.cpp`, `hw/laptop_ble.cpp` |
| 7 | M | Diagnostic benchmarks are compiled into the production firmware: a second `FlightModel` (about 10 KB RAM) and a one-off 120-sample run that blocks the loop when a terminal attaches. | `main.cpp` |
| 8 | M | Config files are rewritten by remove-then-write; a power loss in between loses the saved tracer or calibration. | `hw/config_store.cpp` |
| 9 | M | No single check command; a commit (`3cd90a7`) did not build for the device. | repo |
| 10 | L | README describes the early "laptop renders everything" design (one-way link, host renders, out-of-scope sensors). NOTES.md is current. | `README.md` |
| 11 | L | Legacy laptop-drawing tools (`dashboard.py`, `send_test.py`, `bench.py`) are undocumented as such. | `host/` |
| 12 | L | Strict warnings: only int to int16_t narrowing in Adafruit_GFX calls (harmless). No unused code, no shadowing. | `view/screens.cpp` |

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

1. `core/spsc_ring.h`: header-only single-producer/single-consumer ring (`std::atomic` indices,
   release on publish, acquire on consume). Unit tests in the emulator.
2. Use it for the tracer line queue and the laptop command queue.
3. Laptop frames: decode into a private 12 KB buffer in the panel's native layout inside the callback;
   the loop copies it to the panel on FLUSH (new `IPanel::showRaw()`). The callback never touches the
   panel, the canvas or the rotation.
4. Tracer: the scan callback only queues scan reports; the loop merges them into the list, does the
   matching and calls `connect()`. `onDisconnect` only sets a flag; the loop restarts scanning.
5. Fast reconnect: after a disconnect, scan at full duty for 30 s, then fall back to low duty.
6. Move `glob`, the RSSI sort and the auto-connect policy into `core/devmatch.*`, and laptop packet
   decoding into `core/command_decode.*`, with tests (same-name devices, private addresses, short
   and overlong packets, unknown commands).

### B. Resilience and build hygiene (findings 1, 7, 8)

1. `hw/watchdog.*`: nRF52 WDT, 8 s timeout, paused while halted by a debugger, fed once per loop and in
   every blocking wait (power-on gate, wait-for-release before System OFF).
2. Diagnostics: move the status line and benchmarks out of `main.cpp` into `hw/diagnostics.*`; the
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

## Multi-use box (proposed 2026-10-16, not started)

The same hardware as a flight display, a game console and a generic gadget, with firmware switched
between seasons.

**Firmware structure**

    platform/   hw (panel + DMA, sensors, BLE, power, flash settings), gfx renderer, input (switch,
                later 4 buttons), fused compass, app lifecycle + switching, settings UI
    apps/vario  the flight display (today's core/ maths + views)
    apps/games  breakout, soaring sim, ...
    apps/tools  standalone compass + temp/humidity, level, ...

- An app implements start / stop / tick / draw / input; the platform owns screen, cadence and power.
- Two images from one tree: `vario` (flight app + standalone compass, no game code: less to go wrong in
  the air) and `console` (launcher with games and tools, optionally the vario too).
- Switching: UF2 drag and drop (double-tap reset -> FTHR840BOOT drive), plus a "Switch firmware" menu
  item calling `enterUf2Dfu()`, so the reset button can stay inside the case. BLE OTA DFU later.
  Settings live in the internal LittleFS area, which a UF2 app update should not touch (verify on
  the device). Release both .uf2 files on GitHub.

**Order:** watchdog first; then move code into platform/ and apps/vario (pixel-identical screens, all
tests green); app interface + launcher; two build envs + switch item; game engine basics + Breakout.
