#!/bin/sh
# Headless smoke run of the whole box (platform host + launcher + vario + Tetris + compass tool over mock ports).
# Renders the home screen, the flight sim (map and 3D view), the vario's live (with AGL), thermal, centring and map pages with replay data, the vario's device list, the system sheet over the vario / Tetris / launcher and the hold bars,
# Tetris started from the home screen, the compass tool, Settings (list, calibration, About) and the way back to the vario
# into out/box_*.png.
# Tracer data: an IGC file (SMOKE_IGC, default the flight in ~/Downloads), else the vario's built-in test flight.
# Fails when a screenshot is missing or the box is not in the app the script expects.
cd "$(dirname "$0")" || exit 1
IGC="${SMOKE_IGC:-$HOME/Downloads/2026-08-16-XCT-SVE-09.igc}"
# Map pack for AGL and the map page: SMOKE_PACK, else maps/bulgaria.pack, else the Sopot
# fixture (SMOKE_PACK=none: no terrain); none of them: the box runs without terrain and the map shot is skipped.
STUDY=../../maps/bulgaria.pack
PACK="${SMOKE_PACK:-}"
[ -z "$PACK" ] && [ -f "$STUDY" ] && PACK="$STUDY"
[ -z "$PACK" ] && [ -f fixtures/sopot.pack ] && PACK=fixtures/sopot.pack
[ "$PACK" = none ] && PACK=
if [ -n "$PACK" ]; then PACKARG="--pack $PACK"; else PACKARG="--pack none"; fi
if [ -f "$IGC" ]; then SRC="--igc $IGC --start 4185 --speed 12"; else SRC="--sim"; fi
KEYS="200 shot:out/box_vario_compass.png main 30 shot:out/box_vario_thermal.png main 15 shot:out/box_vario_center.png"
[ -n "$PACK" ] && KEYS="$KEYS main 40 shot:out/box_vario_map.png"
KEYS="$KEYS turn:135"
KEYS="$KEYS +main 66 shot:out/box_vario_holdbar.png -main 5"
KEYS="$KEYS long 20 shot:out/box_vario_list.png long 10"
KEYS="$KEYS +main 150 shot:out/box_sheet_poweroff.png -main 5 shot:out/box_sheet_vario.png main long 40 shot:out/box_launcher.png"
KEYS="$KEYS main main main 10 shot:out/box_launcher_tetris.png long 20 shot:out/box_tetris_title.png"
KEYS="$KEYS main 40 left left a up 30 left a down 20 shot:out/box_tetris_game.png"
KEYS="$KEYS off 10 shot:out/box_sheet_tetris.png sheet:home 20 shot:out/box_launcher_back.png main main main main long 30 shot:out/box_compass.png main 20 shot:out/box_compass_flat.png"
KEYS="$KEYS off sheet:home 20 main main main main long 30 shot:out/box_settings.png long 30 shot:out/box_settings_cal.png main"
KEYS="$KEYS main main main main main long 20 shot:out/box_settings_about.png main off sheet:home 20 off 10 shot:out/box_sheet_launcher.png sheet:back 20 shot:out/box_launcher_settings.png"
KEYS="$KEYS main main main long 150 shot:out/box_sim.png right 40 a 30 shot:out/box_sim_3d.png off sheet:home 20 main main long 300 shot:out/box_map.png +right 60 -right 300 shot:out/box_map_pan.png"
KEYS="$KEYS off sheet:home 20 main main long 120 shot:out/box_vario_back.png"
rm -f out/box_*.png
OUT=$(out/play box --no-intro $SRC $PACKARG --seed 3 --keys "$KEYS") || exit 1
echo "$OUT"
APPS=$(echo "$OUT" | sed -n 's/^wrote .*, \([a-z]*\))$/\1/p' | tr '\n' ' ')
WANT="vario vario vario vario vario vario vario launcher launcher tetris tetris tetris launcher compass compass settings settings settings launcher launcher flightsim flightsim map map vario "
[ -n "$PACK" ] && WANT="vario vario vario vario vario vario vario vario launcher launcher tetris tetris tetris launcher compass compass settings settings settings launcher launcher flightsim flightsim map map vario "
[ "$APPS" = "$WANT" ] || { echo "SMOKE FAIL: apps were [$APPS], expected [$WANT]"; exit 1; }
[ -n "$PACK" ] && { echo "$OUT" | grep -q "^terrain: " || { echo "SMOKE FAIL: pack $PACK was not loaded"; exit 1; }; }
for f in vario_compass vario_thermal vario_center vario_holdbar vario_list sheet_poweroff sheet_vario launcher launcher_tetris tetris_title tetris_game sheet_tetris launcher_back compass compass_flat settings settings_cal settings_about sheet_launcher launcher_settings sim sim_3d map map_pan vario_back; do
  [ -s "out/box_$f.png" ] || { echo "SMOKE FAIL: out/box_$f.png missing"; exit 1; }
done
[ -n "$PACK" ] && { [ -s out/box_vario_map.png ] || { echo "SMOKE FAIL: out/box_vario_map.png missing"; exit 1; }; }
echo "SMOKE OK: $(ls out/box_*.png | wc -l | tr -d ' ') screenshots in firmware/emu/out/"
