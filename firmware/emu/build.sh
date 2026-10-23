#!/bin/sh
# Build the PC emulator (with sanitizers), run the tests, render the standard screens.
# Compilation runs on all cores through the Makefile next to this script (one object per source and flag set, header
# dependencies tracked, so a rerun rebuilds only what changed); the tests run one process per test function, all cores.
#   ./build.sh                      tests + screens
#   ./build.sh replay <args...>     run the emulator with the given arguments (see emu_main.cpp)
#   ./build.sh count [outdir] [rot]  per-section draw work of the live page (pixel/span call counts)
#   ./build.sh wind <lines.txt>     EKF vs circle-fit wind over a real flight (see emu_main.cpp)
#   ./build.sh windtrace <lines.txt> per-second CSV of circle fit, EKF and shown wind (host/wind_ref.py reads it)
#   ./build.sh cue<lines.txt>      hit rate of the centring cue against the next turn (see emu_main.cpp cmdCue)
#   ./build.sh logeval <log.bin> [--latency MS] [--wind]  flight log (host/logdl.py) through the dead reckoner: lag, error vs holding the last fix
#   ./build.sh soak [lines.txt]     long session through the App, about a minute (see emu_main.cpp)
#   ./build.sh play <app> [opts]    play an app on the PC in an SDL window (see play.cpp): tetris, or box
#                                   (the whole box: home screen + vario + Tetris + compass tool + Settings). The box loads a map pack
#                                   (--pack FILE; default maps/bulgaria.pack if present, else the Sopot
#                                   fixture; --pack none for a box without terrain)
#   ./build.sh loadtest [PACK]      host/load_pack.py against the loader code over an in-memory flash (clean and with damaged frames)
#   ./build.sh simbench [PACK]      flight sim frame timing on the PC, scaled to the device (see sim_bench.cpp)
#   ./build.sh voxdigest --save|--cmp FILE [PACK]  3D renderer pictures + depth buffer against a saved set (voxel_digest.cpp)
#   ./build.sh simreach [PACK] [seeds] [t0..]  reachability of thermals from every takeoff + scripted-pilot climb (sim_reach.cpp)
#   ./build.sh smoke               headless run of the whole box: renders the launcher, vario pages, Tetris
#                                   started from the launcher and the way back (emu/out/box_*.png)
#   ./build.sh prebuild            only build every binary (scripts/check.sh does this first, then runs its stages side by side)
set -e
cd "$(dirname "$0")"
mkdir -p out
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
mk() { make -s -j"$JOBS" "$@"; }

# Every test function in its own process (out/tests --only NAME), all cores at once. Then the outputs are printed in
# list order and the counts added up, so the summary line is what one process running them all would print.
run_tests() {
  D=out/tests.d
  rm -rf "$D"; mkdir -p "$D"
  out/tests --list > "$D/list"
  # the slow ones first, so they do not end up alone at the tail
  SLOW='^(sim_playability|sim_day_settings)$'
  { grep -E "$SLOW" "$D/list" || true; grep -vE "$SLOW" "$D/list" || true; } > "$D/order"
  xargs -P "$JOBS" -I{} sh -c 'out/tests --only "$1" > "out/tests.d/$1.log" 2>&1; echo $? > "out/tests.d/$1.rc"' _ {} < "$D/order"
  # A test that left no "@@" line was killed (a signal under load, the machine's process or memory limits: the two slow sim
  # tests were reported like that once and never again). Run such a test once more on its own and say so, so a real crash
  # (same exit status twice) stays a failure and a one-off does not.
  while read -r n; do
    if ! grep -q '^@@ ' "$D/$n.log"; then
      echo "  note: test $n ended without finishing (exit status $(cat "$D/$n.rc")); running it again on its own"
      out/tests --only "$n" > "$D/$n.log" 2>&1; echo $? > "$D/$n.rc"
    fi
  done < "$D/list"
  checks=0; failures=0; bugs=0; bad=0
  while read -r n; do
    rc="$(cat "$D/$n.rc")"
    grep -v '^@@ ' "$D/$n.log" || true
    line="$(grep '^@@ ' "$D/$n.log" || true)"
    if [ -z "$line" ]; then
      echo "  FAIL test $n ended without finishing (exit status $rc)"
      failures=$((failures + 1)); bad=1
      continue
    fi
    set -- $line
    checks=$((checks + $2)); failures=$((failures + $3)); bugs=$((bugs + $4))
    if [ "$rc" -ne 0 ]; then echo "  FAIL in test $n (exit status $rc)"; bad=1; fi
  done < "$D/list"
  if [ "$bugs" -ne 0 ]; then printf '\n%d known firmware bugs hit (see BUG: comments in robust_tests.h)\n' "$bugs"; fi
  printf '\n%d checks, %d failures\n' "$checks" "$failures"
  [ "$bad" -eq 0 ] && [ "$failures" -eq 0 ]
}

if [ "$1" = "prebuild" ]; then
  T="out/emu out/tests out/sim_demo out/terrain_demo out/loader_sim"
  if command -v sdl2-config > /dev/null 2>&1; then T="$T out/play"; fi
  mk $T
  exit 0
fi
if [ "$1" = "loadtest" ]; then
  # End to end: host/load_pack.py against the real loader code over an in-memory flash (pipes instead of USB).
  mk out/loader_sim
  PACK="${2:-fixtures/sopot.pack}"
  for mode in "" "--drop"; do
    ../../.venv/bin/python ../../host/load_pack.py "$PACK" --port "exec:out/loader_sim $mode" > out/loadtest.log 2> out/loadtest.err || { cat out/loadtest.log out/loadtest.err; echo "LOADTEST FAIL"; exit 1; }
    tail -3 out/loadtest.log
    cat out/loadtest.err
    grep -q "SIM header=ok .* crc_ok=1" out/loadtest.err || { echo "LOADTEST FAIL"; exit 1; }
  done
  # the flight log download (host/logdl.py) against the same firmware code: a synthetic ride, CRC-checked reads, CSV, erase
  rm -f out/logtest*
  ../../.venv/bin/python ../../host/logdl.py out/logtest --port "exec:out/loader_sim --log" --erase > out/logtest.log 2> out/logtest.err || { cat out/logtest.log out/logtest.err; echo "LOGTEST FAIL"; exit 1; }
  grep -q "1 sessions, 400 lines" out/logtest.log && grep -q "log erased" out/logtest.log || { cat out/logtest.log; echo "LOGTEST FAIL"; exit 1; }
  ../../.venv/bin/python ../../host/logdl.py --decode out/logtest.bin out/logtest2 > /dev/null && cmp out/logtest_lines.csv out/logtest2_lines.csv && cmp out/logtest_yaw.csv out/logtest2_yaw.csv || { echo "LOGTEST FAIL"; exit 1; }
  mk out/emu
  out/emu logeval out/logtest.bin | head -3
  echo "LOGTEST OK"
  echo "LOADTEST OK"
  exit 0
fi
if [ "$1" = "voxdigest" ]; then
  # 3D renderer regression oracle: --save FILE before a change, --cmp FILE after (voxel_digest.cpp)
  mk out/voxel_digest
  shift
  exec out/voxel_digest "$@"
fi
if [ "$1" = "simbench" ]; then
  # Frame timing of the flight sim, calibrated against vario pages of known device time (no sanitizers, -O2).
  mk out/sim_bench
  shift
  exec out/sim_bench "${1:-fixtures/sopot.pack}"
fi
if [ "$1" = "simtests" ]; then
  # The flight sim tests alone, no sanitizers (fast): ./build.sh simtests [sky_fx]
  mk out/sim_tests
  shift
  exec out/sim_tests "$@"
fi
if [ "$1" = "simdemo" ]; then
  # Sim scenario renders only (fast): ./build.sh simdemo [PACK] [OUTDIR] -> OUTDIR/sim_*.pgm and .png (2x), thermal contact sheet
  mk out/sim_demo
  D="${3:-out/sim}"
  mkdir -p "$D"
  out/sim_demo "${2:-fixtures/sopot.pack}" "$D"
  ../../.venv/bin/python sheet.py "$D"
  exit 0
fi
if [ "$1" = "simreach" ]; then
  # Playability of the air from every takeoff (see sim_reach.cpp).
  mk out/sim_reach
  shift
  exec out/sim_reach "$@"
fi
if [ "$1" = "play" ] || [ "$1" = "smoke" ]; then
  # The player: no sanitizers (speed), SDL2 from Homebrew, the whole box over mock ports.
  mk out/play
  if [ "$1" = "smoke" ]; then exec ./smoke.sh; fi
  shift
  exec out/play "$@"
fi
case "$1" in
  replay|count|wind|windtrace|cue|soak|logeval) mk out/emu ;;
  *) mk out/emu out/tests out/sim_demo out/terrain_demo ;;
esac
if [ "$1" = "replay" ]; then shift; exec out/emu replay "$@"; fi
if [ "$1" = "count" ]; then shift; exec out/emu count "${1:-out}" "${2:-1}"; fi
if [ "$1" = "wind" ]; then shift; exec out/emu wind "$@"; fi
if [ "$1" = "windtrace" ]; then shift; exec out/emu windtrace "$@"; fi
if [ "$1" = "cue" ]; then shift; exec out/emu cue "$@"; fi
if [ "$1" = "soak" ]; then shift; exec out/emu soak "$@"; fi
if [ "$1" = "logeval" ]; then shift; exec out/emu logeval "$@"; fi
run_tests
out/emu shots out ${ROT:-1}
out/terrain_demo fixtures/sopot.pack 42.62 24.85 0 100 out/terrain_sopot.pgm
../../.venv/bin/python - <<'PY'
from PIL import Image
import glob, os
for p in glob.glob("out/*.pgm"):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
PY
../../.venv/bin/python compass3d_sheet.py out out/compass3d_sheet.png
../../.venv/bin/python weather_grid.py out out/weather_grid.png
echo "screens: $(ls out/*.png | tr '\n' ' ')"
