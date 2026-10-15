#!/bin/sh
# Pre-commit check: emulator build and tests, then the device build.
# Exit status is non-zero if either step fails.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

echo "=== 1/2 Emulator: build with sanitizers, tests, screen renders ==="
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
echo "=== 2/2 Device build: pio run ==="
if ! (cd "$ROOT/firmware" && "$ROOT/.venv/bin/pio" run); then
  echo "FAIL: device build"
  exit 1
fi

echo
echo "CHECK OK: emulator tests 0 failures, device build succeeded"
