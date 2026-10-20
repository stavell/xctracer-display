#!/bin/sh
# Pre-commit check: emulator build and tests, headless smoke run of the whole box, device builds (vario image,
# diagnostics image, box image), static stack check of the device images.
# Exit status is non-zero if any step fails.
#
# The stages are independent, so they run side by side on all cores, each with its output in its own file; the
# outputs are printed in the order below as each stage finishes. The device builds (one pio process per image) start
# first, the emulator binaries are built in one parallel make before the stages that run them start (they share
# object files, so nothing may build them twice at the same time).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="$ROOT/firmware/emu/build.sh"
OUT="$(mktemp -d)"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$OUT"' EXIT

# device builds: one pio process per environment, all at once
ENVS="$(sed -n 's/^\[env:\(.*\)\]$/\1/p' "$ROOT/firmware/platformio.ini")"
PIDS=""
for e in $ENVS; do
  (cd "$ROOT/firmware" && "$ROOT/.venv/bin/pio" run -e "$e" > "$OUT/pio_$e" 2>&1; echo $? > "$OUT/pio_$e.rc") &
  PIDS="$PIDS $!"
done

# emulator binaries (all of them, once), then the three stages that run them
"$EMU" prebuild > "$OUT/prebuild" 2>&1
PRE_RC=$?
if [ "$PRE_RC" -eq 0 ]; then
  ( "$EMU" > "$OUT/emu" 2>&1; echo $? > "$OUT/emu.rc" ) &
  EMU_PID=$!
  ( "$EMU" loadtest > "$OUT/load" 2>&1; echo $? > "$OUT/load.rc" ) &
  LOAD_PID=$!
  if command -v sdl2-config > /dev/null 2>&1; then
    ( "$EMU" smoke > "$OUT/smoke" 2>&1; echo $? > "$OUT/smoke.rc" ) &
    SMOKE_PID=$!
  fi
fi

echo "=== 1/4 Emulator: build with sanitizers, tests, screen renders ==="
if [ "$PRE_RC" -ne 0 ]; then
  cat "$OUT/prebuild"
  echo "FAIL: emulator step exited with status $PRE_RC"
  exit 1
fi
wait $EMU_PID
RC="$(cat "$OUT/emu.rc")"
cat "$OUT/emu"
if [ "$RC" -ne 0 ]; then
  echo "FAIL: emulator step exited with status $RC"
  exit 1
fi
if ! grep -q ", 0 failures$" "$OUT/emu"; then
  echo "FAIL: emulator output does not report \"0 failures\""
  exit 1
fi

echo
echo "=== 1b Map loader: host/load_pack.py against the firmware's loader code over an in-memory flash ==="
wait $LOAD_PID
cat "$OUT/load"
if [ "$(cat "$OUT/load.rc")" -ne 0 ]; then
  echo "FAIL: map loader end-to-end test"
  exit 1
fi

echo
echo "=== 2/4 Box smoke run: launcher, vario with replay data, Tetris, compass tool, the way back ==="
if [ -n "$SMOKE_PID" ]; then
  wait $SMOKE_PID
  RC="$(cat "$OUT/smoke.rc")"
  grep "^wrote \|^SMOKE\|^tracer" "$OUT/smoke"
  if [ "$RC" -ne 0 ] || ! grep -q "^SMOKE OK" "$OUT/smoke"; then
    cat "$OUT/smoke"
    echo "FAIL: box smoke run"
    exit 1
  fi
else
  echo "skipped: SDL2 not installed (brew install sdl2)"
fi

echo
echo "=== 3/4 Device build: pio run (vario, diagnostics, box images) ==="
wait $PIDS
FAILED=""
for e in $ENVS; do
  echo "--- $e ---"
  cat "$OUT/pio_$e"
  if [ "$(cat "$OUT/pio_$e.rc")" -ne 0 ]; then FAILED="$FAILED $e"; fi
done
if [ -n "$FAILED" ]; then
  echo "FAIL: device build ($FAILED )"
  exit 1
fi

echo
echo "=== 4/4 Stack check: worst static call chain from loop() and setup() ==="
if ! "$ROOT/.venv/bin/python" "$ROOT/scripts/stack_check.py"; then
  echo "FAIL: stack check"
  exit 1
fi

echo
echo "=== RAM budget: .heap of every image (FreeRTOS tasks, USB and BLE buffers live there) ==="
if ! "$ROOT/.venv/bin/python" "$ROOT/scripts/ram_check.py"; then
  echo "FAIL: RAM budget (an image with too little heap does not boot)"
  exit 1
fi


echo
echo "=== host: serial port detection ==="
if ! "$ROOT/.venv/bin/python" "$ROOT/host/test_find_port.py"; then
  echo "FAIL: host/test_find_port.py"
  exit 1
fi
echo
echo "CHECK OK: emulator tests 0 failures, box smoke run, device builds succeeded, stack check, RAM budget and host port test passed"
