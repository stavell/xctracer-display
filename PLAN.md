# Plan

## Status: v1.0 (2026-10-20)

The box is a finished platform. One firmware image (`feather_sense_box`) holds the home screen, the vario, the compass and weather app,
the flight sim, Tetris, Settings and the start-up animation; `feather_sense` is the vario alone. The frame loop, input grammar (press,
long press, 3 s system sheet, 6 s power off), ports, motion service, 3D library (`platform/gfx3d`) and a developer guide with an
example app (`docs/PLATFORM.md`) are in place. The clock sleep shows the time and a weather station; the pressure history and the
clock survive resets and System OFF (retained RAM). The emulator, its tests and `scripts/check.sh` cover everything that does not need
the board. The history of how it got here (reviews, measurements, decisions) is in `NOTES.md`.

## Open / needs hardware

Not yet verified on the device:

- System OFF retention: needs a 6 s hold, then wake, and `wth ... ret=ok` with the history intact.
- Sim 3D at about 44 fps against the 50 fps target (voxel about 11.5 ms of a 19.6 ms draw; 50 fps needs about 9 ms). The map view runs 50 fps.
- Intro cloud-front fps (measured 31.8 fps before the puff cut; re-measure).
- Clock-sleep current with a PPK2 (the estimate of 50-250 uA is from datasheets), including the QSPI deep power-down.
- The six front buttons are not wired (`platform/hw/pins.h`: `PIN_KEY_*` undefined); case test plate not printed.
- Loader speed-up (pump the loader in a tight loop during a pack load).
- Gyro ground test with `host/logdl.py` and `build.sh logeval` (README, "Flight log").
- Compass bus recovery (`cmp ... rec=N`): a frozen compass was seen once and not reproduced; whether the recovery heals it is unconfirmed.
- Watchdog: wired but not started (`watchdog::start` is commented out in `main_vario.cpp` and never called in `main_box.cpp`); the hang test
  (reset within 8 s, `rst=0x2`) and the interaction with the bootloader are not verified.
- Tracer speed unit with a real GPS fix; `DISPLAY_ROTATION` once mounted; DISP / EXTMODE wiring.

## Parked ideas

- Vario heading display in the compass app's style (the vario stays as it is: "it is doing its job").
- Thermal assistant (`docs/thermal_review.md`): item 4 (gyro circle phase; log raw IMU and tracer receive times first), 6 and 7.
- Auto-switch to the thermal page while circling.
- Sun shading on the sim's top-down map (the vario's map page has it).
- Check the weather-model wind of the sim against real records.
- Flash backup of the barometer history (a battery-out loses it today).
- Sim: thermal camera. While circling, the chase cam eases back and up (wider, about 35 degrees down, slow yaw lag) so the glider and the
  thermal helpers are visible from outside; steering stays bank-relative (left / right = bank), so it does not depend on the view.
  Prototype on the PC first.
