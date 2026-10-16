#!/bin/sh
# Headless smoke run of the whole box (platform host + launcher + vario + Tetris + compass tool over mock ports).
# Renders the launcher, the vario's live and thermal pages with replay data, the device list with its "Apps..." row,
# Tetris started from the launcher, the compass tool and the way back to the vario into out/box_*.png.
# Tracer data: an IGC file (SMOKE_IGC, default the flight in ~/Downloads), else the vario's built-in test flight.
# Fails when a screenshot is missing or the box is not in the app the script expects.
cd "$(dirname "$0")" || exit 1
IGC="${SMOKE_IGC:-$HOME/Downloads/2026-08-16-XCT-SVE-09.igc}"
if [ -f "$IGC" ]; then SRC="--igc $IGC --start 4185 --speed 12"; else SRC="--sim"; fi
KEYS="200 shot:out/box_vario_compass.png main 30 shot:out/box_vario_thermal.png turn:135"
KEYS="$KEYS long 20 shot:out/box_vario_list.png main main long 40 shot:out/box_launcher.png"
KEYS="$KEYS main 10 shot:out/box_launcher_tetris.png long 20 shot:out/box_tetris_title.png"
KEYS="$KEYS main 40 left left a up 30 left a down 20 shot:out/box_tetris_game.png"
KEYS="$KEYS off 20 shot:out/box_launcher_back.png main long 30 shot:out/box_compass.png"
KEYS="$KEYS off 20 main main long 120 shot:out/box_vario_back.png"
rm -f out/box_*.png
OUT=$(out/play box $SRC --seed 3 --keys "$KEYS") || exit 1
echo "$OUT"
APPS=$(echo "$OUT" | sed -n 's/^wrote .*, \([a-z]*\))$/\1/p' | tr '\n' ' ')
WANT="vario vario vario launcher launcher tetris tetris launcher compass vario "
[ "$APPS" = "$WANT" ] || { echo "SMOKE FAIL: apps were [$APPS], expected [$WANT]"; exit 1; }
for f in vario_compass vario_thermal vario_list launcher launcher_tetris tetris_title tetris_game launcher_back compass vario_back; do
  [ -s "out/box_$f.png" ] || { echo "SMOKE FAIL: out/box_$f.png missing"; exit 1; }
done
echo "SMOKE OK: $(ls out/box_*.png | wc -l | tr -d ' ') screenshots in firmware/emu/out/"
