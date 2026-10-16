#!/bin/sh
# Build the PC emulator (with sanitizers), run the tests, render the standard screens.
#   ./build.sh                      tests + screens
#   ./build.sh replay <args...>     run the emulator with the given arguments (see emu_main.cpp)
#   ./build.sh count [outdir] [rot]  per-section draw work of the live page (pixel/span call counts)
#   ./build.sh wind <lines.txt>     EKF vs circle-fit wind over a real flight (see emu_main.cpp)
#   ./build.sh soak [lines.txt]     long session through the App, about a minute (see emu_main.cpp)
#   ./build.sh play <app> [opts]    play an app on the PC in an SDL window (see play.cpp): tetris, or box
#                                   (the whole box: home screen + vario + Tetris + compass tool + Settings). The box loads a map pack
#                                   (--pack FILE; default the Bulgaria pack from the study if present, else the Sopot
#                                   fixture; --pack none for a box without terrain)
#   ./build.sh simbench [PACK]      flight sim frame timing on the PC, scaled to the device (see sim_bench.cpp)
#   ./build.sh smoke                headless run of the whole box: renders the launcher, vario pages, Tetris
#                                   started from the launcher and the way back (emu/out/box_*.png)
set -e
cd "$(dirname "$0")"
G="../.pio/libdeps/feather_sense/Adafruit GFX Library"
mkdir -p out
# 64-bit host: keep the full-width pgm_read_pointer from our stub (the library's is 32-bit)
grep -v "define pgm_read_pointer" "$G/Adafruit_GFX.cpp" > out/Adafruit_GFX_host.cpp
V=../src/apps/vario
P=../src/platform
T=../src/apps/games/tetris
L=../src/apps/launcher
S=../src/apps/settings
TERRAIN="$P/terrain/crc32.cpp $P/terrain/tile_codec.cpp $P/terrain/terrain.cpp $P/terrain/relief.cpp $P/terrain/map_vectors.cpp"
TETRIS="$T/tetris.cpp $T/tetris_view.cpp $T/tetris_app.cpp"
M=../src/apps/games/sim
SIM="$M/glider.cpp $M/height_cache.cpp $M/air.cpp $M/world.cpp $M/voxel.cpp $M/topdown.cpp $M/sim_hud.cpp $M/sim_app.cpp"
LAUNCHER="$L/launcher_app.cpp $L/launcher_view.cpp"
SETTINGS="$S/settings_app.cpp $S/settings_view.cpp $P/settings/box_settings.cpp $P/settings/compass_cal.cpp $P/settings/cal_view.cpp"
GFX="$P/gfx/bitmap1.cpp $P/gfx/gray.cpp $P/gfx/atlas.cpp $P/gfx/font.cpp $P/gfx/shapes.cpp $P/gfx/ui.cpp $P/gfx/icons.cpp"
VARIO="$V/core/xctrc.cpp $V/core/wind.cpp $V/core/windekf.cpp $V/core/thermal.cpp $P/sensors/compass.cpp $P/sensors/ahrs.cpp $P/sensors/mount.cpp $V/core/heading.cpp $P/sensors/heading_smoother.cpp $V/core/map_motion.cpp $V/core/flight.cpp $V/core/windprofile.cpp $V/core/home.cpp $V/core/sim.cpp $P/panel/linediff.cpp $P/panel/sharp_frame.cpp $V/core/devmatch.cpp $V/app/command_decode.cpp $V/view/screens.cpp $V/view/terrain_map.cpp $V/view/prof.cpp $V/app/app.cpp"
if [ "$1" = "simbench" ]; then
  # Frame timing of the flight sim, calibrated against vario pages of known device time (no sanitizers, -O2).
  c++ -std=c++17 -O2 -g -DARDUINO=100 -Wall -Wno-unused-function -DGFX_COUNT -Istubs -I../src -I. -I"$G" \
    $SIM $GFX $VARIO $SETTINGS $TETRIS $LAUNCHER $TERRAIN out/Adafruit_GFX_host.cpp sim_bench.cpp -o out/sim_bench
  shift
  exec out/sim_bench "${1:-fixtures/sopot.pack}"
fi
if [ "$1" = "play" ] || [ "$1" = "smoke" ]; then
  # The player: no sanitizers (speed), SDL2 from Homebrew, the whole box over mock ports.
  c++ -std=c++17 -O2 -g -DARDUINO=100 -Wall -Wno-unused-function -DGFX_COUNT -Istubs -I../src -I. -I"$G" $(sdl2-config --cflags) \
    $TETRIS $SIM $LAUNCHER $SETTINGS $GFX $VARIO $TERRAIN out/Adafruit_GFX_host.cpp play.cpp $(sdl2-config --libs) -o out/play
  if [ "$1" = "smoke" ]; then exec ./smoke.sh; fi
  shift
  exec out/play "$@"
fi
SRC="$VARIO $GFX $TETRIS $SIM $LAUNCHER $SETTINGS $TERRAIN out/Adafruit_GFX_host.cpp"
FLAGS="-std=c++17 -g -O1 -DARDUINO=100 -Wall -Wno-unused-function -DGFX_COUNT -Istubs -I../src -I.
       -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all"
c++ $FLAGS -I"$G" $SRC emu_main.cpp -o out/emu
c++ $FLAGS -I"$G" $SRC tests.cpp -o out/tests
# Flight sim scenario renderer and timing tool (no sanitizers: the timings are meant to be read)
c++ -std=c++17 -O2 -g -DARDUINO=100 -Wall -Wno-unused-function -DGFX_COUNT -Istubs -I../src -I. -I"$G" $SIM $GFX $TERRAIN out/Adafruit_GFX_host.cpp sim_demo.cpp -o out/sim_demo
# Map page preview tool: hillshade + contours + roads + towns from a map pack (no sanitizers, it is a render tool)
c++ -std=c++17 -O2 -g -DARDUINO=100 -Wall -Wno-unused-function -Istubs -I../src -I. -I"$G" $GFX $TERRAIN terrain_demo.cpp -o out/terrain_demo
if [ "$1" = "replay" ]; then shift; exec out/emu replay "$@"; fi
if [ "$1" = "count" ]; then shift; exec out/emu count "${1:-out}" "${2:-1}"; fi
if [ "$1" = "wind" ]; then shift; exec out/emu wind "$@"; fi
if [ "$1" = "soak" ]; then shift; exec out/emu soak "$@"; fi
out/tests
out/emu shots out ${ROT:-1}
out/terrain_demo fixtures/sopot.pack 42.62 24.85 0 100 out/terrain_sopot.pgm
../../.venv/bin/python - <<'PY'
from PIL import Image
import glob, os
for p in glob.glob("out/*.pgm"):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
PY
echo "screens: $(ls out/*.png | tr '\n' ' ')"
