#!/bin/bash
# Flash the firmware over USB, safely.
#   scripts/flash.sh [env]          (default env: feather_sense)
#
# Why this exists (2026-10-15): uploads failed mid-transfer and left the board in the bootloader.
# A forgotten serial logger held the port and swallowed the bootloader's replies; PlatformIO still
# printed SUCCESS. So: refuse while anything holds the port, and trust only "Device programmed.".
set -u
ENV="${1:-feather_sense}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIO="$ROOT/.venv/bin/pio"
LOG="$(mktemp -t flash)"

holders() { lsof /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null | awk 'NR>1 {print $2, $1}' | sort -u; }

if ! ls /dev/cu.usbmodem* >/dev/null 2>&1; then
  echo "flash: no board on USB (/dev/cu.usbmodem*)"; exit 1
fi
H="$(holders)"
if [ -n "$H" ]; then
  echo "flash: the serial port is in use; stop these first (serial monitor, logger):"
  echo "$H" | while read -r pid cmd; do echo "  $pid $(ps -o command= -p "$pid" | cut -c1-120)"; done
  exit 1
fi

for attempt in 1 2 3; do
  echo "flash: attempt $attempt ($ENV)"
  (cd "$ROOT/firmware" && "$PIO" run -e "$ENV" -t upload) >"$LOG" 2>&1
  if grep -q "Device programmed." "$LOG"; then
    echo "flash: Device programmed."; rm -f "$LOG"; exit 0
  fi
  grep -iE "error|fail" "$LOG" | tail -3
  # A failed transfer leaves the board in the bootloader; give it time to re-enumerate.
  sleep 5
  H="$(holders)"
  if [ -n "$H" ]; then echo "flash: port taken during upload by: $H"; exit 1; fi
done
echo "flash: FAILED after 3 attempts (log: $LOG). The board is probably in the bootloader:"
echo "  unplug USB and battery, plug USB back in (no reset), run this again."
exit 1
