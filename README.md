# Handheld BLE display

A handheld device whose screen is driven entirely over BLE from a laptop script.
The firmware is a thin display terminal. Layout and logic live on the host.

## Hardware

- MCU: Adafruit Feather nRF52840 Sense (product 4516). LiPo on the JST connector.
- Display: Adafruit SHARP Memory Display breakout, 2.7" 400x240 monochrome (product 4694).
  Write-only SPI, CS active HIGH, needs a 13.5 KB framebuffer on the MCU.
- 4 front buttons: planned, **not wired yet**. Out of scope for now.

## Current wiring (as built)

| Display pin | Feather pad | Arduino pin | nRF GPIO |
|---|---|---|---|
| CLK | SCK | 26 | P0.14 |
| DI (MOSI) | MO | 25 | P0.13 |
| CS (active high) | RX | 1 | P0.24 |
| EXT (EXTCOMIN) | MI | 24 | P0.15 |
| DISP, EXTMODE | not recorded | ? | ? |

Pins are defined in one place: `firmware/src/pins.h`. Change the wiring there only.

Open item: where DISP and EXTMODE (EMD) are connected is unconfirmed. The display works,
so they are presumably tied to 3V3. EXTMODE high is the assumption that makes the
EXTCOMIN toggle meaningful. Verify against the Adafruit guide.

## Verified board facts (Adafruit nRF52 BSP, `feather_nrf52840_sense`)

- User switch: `PIN_BUTTON1` = Arduino pin 7 (P1.02), active low.
- Hardware SPI: SCK = 26, MOSI = 25, MISO = 24.
- I2C: SDA = 22, SCL = 23. NeoPixel = 8. LED1 = 13, LED2 = 4.
- Battery sense is A6 (pin 20). PDM mic = 34/35. APDS IRQ = 36 (not on the header).
- Arduino pin numbers are not nRF GPIO numbers. The map is `g_ADigitalPinMap` in the BSP `variant.cpp`.

## Software stack

- PlatformIO, platform `nordicnrf52`, board `adafruit_feather_nrf52840_sense`, Arduino framework.
- Libraries: Adafruit SHARP Memory Display, Adafruit GFX.
- Python tools in `.venv`: platformio, pyserial, bleak, pillow.
- Chosen over CircuitPython for BLE and sleep power, and because LVGL is not needed
  (the host renders the UI).

Gotchas:
- The current SharpMem library does not define `BLACK`/`WHITE`. `main.cpp` defines `BLACK = 0`, `WHITE = 1`.
- VCOM is toggled by a 1 Hz FreeRTOS `SoftwareTimer` on EXTCOMIN (500 ms per toggle).
  The nRF52840 hardware PWM cannot go as low as 1 Hz (about 4 Hz minimum).

## Commands

Run from `firmware/`:

    ../.venv/bin/pio run               # build
    ../.venv/bin/pio run -t upload     # flash (1200 bps touch, nrfutil DFU over USB)

Serial is USB CDC at 115200 on `/dev/cu.usbmodem*`. `ready` prints once at boot, so a
monitor opened after boot will not see it. Python that reads from the board's serial port can use
`.venv/bin/python -I`.

## Status

- Done: toolchain, build, flash, bring-up sketch (text, rectangle, filled block on the display).
  User switch prints `pressed` / `released` over serial.
- Done: BLE display terminal. Firmware advertises `SharpDisplay` (service `D15A0001-...`,
  command characteristic `D15A0002-...`). Wire protocol is in `firmware/src/protocol.h`.
  Verified on hardware: full 400x240 frame from the laptop renders correctly.
- Host client: `host/display_link.py` (bleak + Pillow). Demo: `host/send_test.py`
  (run without `-I`, it imports from its own folder).
- Original demo source was not found on this machine. It is not needed.

## Design decisions

- One-way link: laptop to device. No notify characteristic for now.
- The device is a BLE peripheral with a custom GATT service. It shows "waiting for host" until
  the first frame arrives and keeps the last frame if the link drops (the memory LCD holds
  the image at near-zero power).
- Commands: `CLEAR`, `BLIT` (x, y, w, h), `DATA` (1-bit pixels, MSB-first, 1 = black), `FLUSH`.
  No RLE yet; add it only if frame time matters.
- The host renders with Pillow to 1-bit and sends changed rectangles. It finds the device by
  name or service UUID (macOS has no MAC addresses).
- Power: slow advertising when disconnected, relaxed connection interval, redraw only when
  data arrives. Keep VCOM toggling independent of BLE logic.
- Out of scope for now: buttons, sleep modes, sensors, a reverse channel.

## Vario display (current firmware)

The box is an XC Tracer Mini III GPS display. It is a BLE central: it finds the tracer by
name filter (default `*`, set with `host/ctl.py filter`), remembers it in flash (address or
name) and reconnects at boot. The tracer streams `$XCTRC` lines on service `0xFFE0`,
characteristic `0xFFE1` (3 Hz, checksummed):

    $XCTRC,Y,M,D,h,m,s,cs,lat,lon,alt,speed,course,vario,,,,pressure,battery%*CS

lat = lon = 0 means no GPS fix. Speed is assumed km/h (unverified until a fix is available).

- Screens (short press cycles): Standard, Simple, Thermal (the Maxx II layouts). The thermal
  screen shows automatically while circling and switches back 16 s after straight flight.
- Hold ~1 s: device list. Hold 3 s: power off (banner at 1.5 s). Hold 1 s at boot to power on.
- Every float shown goes through `fnum()` in `ui.cpp`: non-finite values print `--`.
- The laptop can still draw frames (`host/dashboard.py`); the vario UI resumes 10 s after the
  last draw command.
- Bench: `python host/ctl.py sim on` runs a built-in glide/thermal/glide flight.
  `python host/ctl.py tz 180` sets the clock offset from UTC (minutes).
- `host/xct_probe.py` captures the raw tracer stream (log in `host/xct_capture.log`).

The standalone sensor dashboard (environment, compass with calibration, motion, light and
gestures, sound) was removed from the firmware. It is kept under the git tag
`sensor-dashboard`: `git checkout sensor-dashboard`.
