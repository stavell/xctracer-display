#!/bin/sh
# Headless smoke run of the whole box (platform host + launcher + vario + Tetris + compass tool over mock ports).
# Renders the home screen, the flight sim (map and 3D view), the vario's live (with AGL), thermal and map pages with replay data, the vario's menu with its "Home" row,
# Tetris started from the home screen, the compass tool, Settings (list, calibration, About) and the way back to the vario
# into out/box_*.png.
# Tracer data: an IGC file (SMOKE_IGC, default the flight in ~/Downloads), else the vario's built-in test flight.
# Fails when a screenshot is missing or the box is not in the app the script expects.
cd "$(dirname "$0")" || exit 1
IGC="${SMOKE_IGC:-$HOME/Downloads/2026-08-16-XCT-SVE-09.igc}"
# Map pack for AGL and the map page: SMOKE_PACK, else the Bulgaria pack of the terrain study, else the Sopot
# fixture (SMOKE_PACK=none: no terrain); none of them: the box runs without terrain and the map shot is skipped.
STUDY=/private/tmp/claude-501/-Users-stavel-dev-display/da365ad1-40f2-499d-917a-3a6befca5ade/scratchpad/terrain/bulgaria.pack
PACK="${SMOKE_PACK:-}"
[ -z "$PACK" ] && [ -f "$STUDY" ] && PACK="$STUDY"
[ -z "$PACK" ] && [ -f fixtures/sopot.pack ] && PACK=fixtures/sopot.pack
[ "$PACK" = none ] && PACK=
if [ -n "$PACK" ]; then PACKARG="--pack $PACK"; else PACKARG="--pack none"; fi
if [ -f "$IGC" ]; then SRC="--igc $IGC --start 4185 --speed 12"; else SRC="--sim"; fi
KEYS="200 shot:out/box_vario_compass.png main 30 shot:out/box_vario_thermal.png"
[ -n "$PACK" ] && KEYS="$KEYS main 60 shot:out/box_vario_map.png"
KEYS="$KEYS turn:135"
KEYS="$KEYS long 20 shot:out/box_vario_list.png main long 40 shot:out/box_launcher.png"
KEYS="$KEYS main 10 shot:out/box_launcher_tetris.png long 20 shot:out/box_tetris_title.png"
KEYS="$KEYS main 40 left left a up 30 left a down 20 shot:out/box_tetris_game.png"
KEYS="$KEYS off 20 shot:out/box_launcher_back.png main long 30 shot:out/box_compass.png"
KEYS="$KEYS off 20 main long 30 shot:out/box_settings.png long 30 shot:out/box_settings_cal.png main"
KEYS="$KEYS main main main main long 20 shot:out/box_settings_about.png main off 20 shot:out/box_launcher_settings.png"
KEYS="$KEYS main long 150 shot:out/box_sim.png right 40 a 30 shot:out/box_sim_3d.png off 20 main long 120 shot:out/box_vario_back.png"
rm -f out/box_*.png
OUT=$(out/play box $SRC $PACKARG --seed 3 --keys "$KEYS") || exit 1
echo "$OUT"
APPS=$(echo "$OUT" | sed -n 's/^wrote .*, \([a-z]*\))$/\1/p' | tr '\n' ' ')
WANT="vario vario vario launcher launcher tetris tetris launcher compass settings settings settings launcher flightsim flightsim vario "
[ -n "$PACK" ] && WANT="vario vario vario vario launcher launcher tetris tetris launcher compass settings settings settings launcher flightsim flightsim vario "
[ "$APPS" = "$WANT" ] || { echo "SMOKE FAIL: apps were [$APPS], expected [$WANT]"; exit 1; }
[ -n "$PACK" ] && { echo "$OUT" | grep -q "^terrain: " || { echo "SMOKE FAIL: pack $PACK was not loaded"; exit 1; }; }
for f in vario_compass vario_thermal vario_list launcher launcher_tetris tetris_title tetris_game launcher_back compass settings settings_cal settings_about launcher_settings sim sim_3d vario_back; do
  [ -s "out/box_$f.png" ] || { echo "SMOKE FAIL: out/box_$f.png missing"; exit 1; }
done
[ -n "$PACK" ] && { [ -s out/box_vario_map.png ] || { echo "SMOKE FAIL: out/box_vario_map.png missing"; exit 1; }; }
echo "SMOKE OK: $(ls out/box_*.png | wc -l | tr -d ' ') screenshots in firmware/emu/out/"
