#!/bin/sh
# Pre-commit check: emulator build and tests, headless smoke run of the whole box, device builds (vario image,
# diagnostics image, box image), static stack check of the device images.
# Exit status is non-zero if any step fails.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

echo "=== 1/4 Emulator: build with sanitizers, tests, screen renders ==="
"$ROOT/firmware/emu/build.sh" > "$OUT" 2>&1
RC=$?
cat "$OUT"
if [ "$RC" -ne 0 ]; then
  echo "FAIL: emulator step exited with status $RC"
  exit 1
fi
if ! grep -q ", 0 failures$" "$OUT"; then
  echo "FAIL: emulator output does not report \"0 failures\""
  exit 1
fi

echo
echo "=== 2/4 Box smoke run: launcher, vario with replay data, Tetris, compass tool, the way back ==="
if command -v sdl2-config > /dev/null 2>&1; then
  "$ROOT/firmware/emu/build.sh" smoke > "$OUT" 2>&1
  RC=$?
  grep "^wrote \|^SMOKE\|^tracer" "$OUT"
  if [ "$RC" -ne 0 ] || ! grep -q "^SMOKE OK" "$OUT"; then
    cat "$OUT"
    echo "FAIL: box smoke run"
    exit 1
  fi
else
  echo "skipped: SDL2 not installed (brew install sdl2)"
fi

echo
echo "=== 3/4 Device build: pio run (vario, diagnostics, box images) ==="
if ! (cd "$ROOT/firmware" && "$ROOT/.venv/bin/pio" run); then
  echo "FAIL: device build"
  exit 1
fi

echo
echo "=== 4/4 Stack check: worst static call chain from loop() and setup() ==="
if ! "$ROOT/.venv/bin/python" "$ROOT/scripts/stack_check.py"; then
  echo "FAIL: stack check"
  exit 1
fi

echo
echo "CHECK OK: emulator tests 0 failures, box smoke run, device builds succeeded, stack check passed"
